#include "TestCommon.h"
#include <UnwindInfo.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }

	void AddU32(Bytes& b, uint32_t v) {
		for (int i = 0; i < 4; i++)
			b.push_back((uint8_t)(v >> (i * 8)));
	}

	// version 1, the flags, the size of the prolog, the slots, the frame register and offset
	Bytes Header(uint8_t flags, uint8_t prolog, uint8_t slots, uint8_t frameReg = 0, uint8_t frameOffset = 0) {
		return { (uint8_t)(1 | (flags << 3)), prolog, slots, (uint8_t)(frameReg | (frameOffset << 4)) };
	}

	void AddCode(Bytes& b, uint8_t offset, uint8_t op, uint8_t opInfo) {
		b.push_back(offset);
		b.push_back((uint8_t)(op | (opInfo << 4)));
	}

	void AddSlot(Bytes& b, uint16_t value) {
		b.push_back((uint8_t)value);
		b.push_back((uint8_t)(value >> 8));
	}
}

TEST_CASE("A typical prolog", "[unwind]") {
	// push rbx; push rbp; sub rsp, 0x28 (codes are in reverse order)
	auto data = Header(0, 6, 3);
	AddCode(data, 6, 2, 4);		// ALLOC_SMALL 4*8+8 = 0x28
	AddCode(data, 2, 0, 5);		// PUSH_NONVOL rbp
	AddCode(data, 1, 0, 3);		// PUSH_NONVOL rbx
	AddSlot(data, 0);			// alignment

	auto info = DecodeUnwindInfo(AsBytes(data), 0x5000);
	REQUIRE(info.Valid());
	CHECK(info.Version == 1);
	CHECK(info.Flags == 0);
	CHECK(info.SizeOfProlog == 6);
	REQUIRE(info.Codes.size() == 3);
	CHECK(info.Codes[0].Operation == L"ALLOC_SMALL");
	CHECK(info.Codes[0].Details == L"0x28 bytes");
	CHECK(info.Codes[0].CodeOffset == 6);
	CHECK(info.Codes[1].Operation == L"PUSH_NONVOL");
	CHECK(info.Codes[1].Details == L"rbp");
	CHECK(info.Codes[2].Details == L"rbx");
	CHECK_FALSE(info.HasHandler());
	CHECK_FALSE(info.ChainedBegin.has_value());
}

TEST_CASE("Codes with operands in the following slots", "[unwind]") {
	auto data = Header(0, 20, 8, 5, 2);
	AddCode(data, 20, 1, 0);	// ALLOC_LARGE, size / 8 in the next slot
	AddSlot(data, 0x40);
	AddCode(data, 12, 1, 1);	// ALLOC_LARGE, 32-bit size in the next two slots
	AddSlot(data, 0x0010);
	AddSlot(data, 0x0002);
	AddCode(data, 8, 4, 12);	// SAVE_NONVOL r12, offset / 8 in the next slot
	AddSlot(data, 3);
	AddCode(data, 4, 3, 0);		// SET_FPREG

	auto info = DecodeUnwindInfo(AsBytes(data), 0x1000);
	REQUIRE(info.Valid());
	REQUIRE(info.Codes.size() == 4);
	CHECK(info.Codes[0].Details == L"0x200 bytes");
	CHECK(info.Codes[1].Details == L"0x20010 bytes");
	CHECK(info.Codes[2].Operation == L"SAVE_NONVOL");
	CHECK(info.Codes[2].Details == L"r12 at [rsp + 0x18]");
	CHECK(info.Codes[3].Details == L"rbp = rsp + 0x20");
	CHECK(info.FrameRegister == 5);
}

TEST_CASE("An exception handler follows the codes", "[unwind]") {
	SECTION("an odd number of slots is padded") {
		auto data = Header(1, 1, 1);	// EHANDLER
		AddCode(data, 1, 0, 3);
		AddSlot(data, 0xFFFF);			// padding, not a code
		AddU32(data, 0x1234);
		auto info = DecodeUnwindInfo(AsBytes(data), 0x8000);
		REQUIRE(info.Valid());
		CHECK(info.Codes.size() == 1);
		CHECK(info.HandlerRva == 0x1234);
		CHECK(info.HandlerDataRva == 0x8000 + 12);
		CHECK(UnwindFlagsToString(info.Flags) == L"EHANDLER");
	}
	SECTION("no codes") {
		auto data = Header(3, 0, 0);	// EHANDLER | UHANDLER
		AddU32(data, 0x4321);
		auto info = DecodeUnwindInfo(AsBytes(data), 0x100);
		REQUIRE(info.Valid());
		CHECK(info.HandlerRva == 0x4321);
		CHECK(info.HandlerDataRva == 0x108);
		CHECK(UnwindFlagsToString(info.Flags) == L"EHANDLER, UHANDLER");
	}
}

TEST_CASE("Chained unwind information", "[unwind]") {
	auto data = Header(4, 0, 0);
	AddU32(data, 0x1000);
	AddU32(data, 0x1080);
	AddU32(data, 0x9000);
	auto info = DecodeUnwindInfo(AsBytes(data), 0x100);
	REQUIRE(info.Valid());
	CHECK(info.ChainedBegin == 0x1000u);
	CHECK(info.ChainedEnd == 0x1080u);
	CHECK(info.ChainedUnwindInfo == 0x9000u);
	CHECK_FALSE(info.HasHandler());
}

TEST_CASE("Bad unwind information is reported, not read past", "[unwind]") {
	SECTION("too short") {
		Bytes data{ 1, 0 };
		CHECK_FALSE(DecodeUnwindInfo(AsBytes(data), 0).Valid());
	}
	SECTION("an unknown version") {
		auto data = Header(0, 0, 0);
		data[0] = 5;
		CHECK_FALSE(DecodeUnwindInfo(AsBytes(data), 0).Valid());
	}
	SECTION("more slots than data") {
		auto data = Header(0, 0, 10);
		AddCode(data, 1, 0, 3);
		CHECK_FALSE(DecodeUnwindInfo(AsBytes(data), 0).Valid());
	}
	SECTION("a code whose operand is missing") {
		auto data = Header(0, 4, 1);
		AddCode(data, 4, 4, 3);		// SAVE_NONVOL needs two slots
		AddSlot(data, 0);
		CHECK_FALSE(DecodeUnwindInfo(AsBytes(data), 0).Valid());
	}
	SECTION("a handler outside the data") {
		auto data = Header(1, 0, 0);
		CHECK_FALSE(DecodeUnwindInfo(AsBytes(data), 0).Valid());
	}
}

TEST_CASE("The unwind information of a system DLL", "[unwind][system]") {
	PEFile pe;
	REQUIRE(pe.Open(L"C:\\Windows\\System32\\kernel32.dll"));
	REQUIRE(pe.GetFileInfo()->IsPE64);
	auto const& entries = *pe.GetExceptions();
	REQUIRE(entries.size() > 100);
	size_t valid = 0, handlers = 0, chained = 0;
	for (auto const& e : entries) {
		auto info = DecodeUnwindInfo(pe, e.RuntimeFuncEntry.UnwindInfoAddress);
		if (!info.Valid())
			continue;
		valid++;
		handlers += info.HasHandler();
		chained += info.ChainedBegin.has_value();
		CHECK(info.SizeOfProlog < 0xFF);
	}
	CHECK(valid == entries.size());
	CHECK(handlers > 0);
	CHECK(chained > 0);
}
