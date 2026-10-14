#include "TestCommon.h"
#include <ExceptionScopes.h>
#include <cstdlib>
#include <Windows.h>
#include <map>

namespace {
	// An image made of pieces at RVAs
	struct Image {
		std::map<uint32_t, std::vector<uint8_t>> Pieces;

		void Put(uint32_t rva, std::vector<uint8_t> bytes) {
			Pieces[rva] = std::move(bytes);
		}
		ImageReader Reader() const {
			return [this](uint32_t rva, void* buffer, size_t size) {
				for (auto const& [at, bytes] : Pieces)
					if (rva >= at && rva + size <= at + bytes.size()) {
						memcpy(buffer, bytes.data() + (rva - at), size);
						return true;
					}
				return false;
			};
		}
	};

	void Add32(std::vector<uint8_t>& out, uint32_t value) {
		for (int i = 0; i < 4; i++)
			out.push_back((uint8_t)(value >> (8 * i)));
	}

	// the compressed encoding of __CxxFrameHandler4
	void AddCompressed(std::vector<uint8_t>& out, uint32_t value) {
		if (value < 0x80)
			out.push_back((uint8_t)(value << 1));
		else if (value < 0x4000) {
			uint32_t v = value << 2 | 1;
			out.push_back((uint8_t)v);
			out.push_back((uint8_t)(v >> 8));
		}
		else if (value < 0x200000) {
			uint32_t v = value << 3 | 3;
			for (int i = 0; i < 3; i++)
				out.push_back((uint8_t)(v >> (8 * i)));
		}
		else if (value < 0x10000000) {
			uint32_t v = value << 4 | 7;
			for (int i = 0; i < 4; i++)
				out.push_back((uint8_t)(v >> (8 * i)));
		}
		else {
			out.push_back(0x0F);
			Add32(out, value);
		}
	}

	std::vector<uint8_t> TypeDescriptor(std::string const& name) {
		std::vector<uint8_t> out(16, 0);
		out.insert(out.end(), name.begin(), name.end());
		out.push_back(0);
		return out;
	}
}

TEST_CASE("Handlers are known by their names", "[eh]") {
	CHECK(ExceptionHandlerKindOf("__C_specific_handler") == ExceptionHandlerKind::Seh);
	CHECK(ExceptionHandlerKindOf("__GSHandlerCheck_SEH") == ExceptionHandlerKind::Seh);
	CHECK(ExceptionHandlerKindOf("__CxxFrameHandler3") == ExceptionHandlerKind::Cxx3);
	CHECK(ExceptionHandlerKindOf("__GSHandlerCheck_EH") == ExceptionHandlerKind::Cxx3);
	CHECK(ExceptionHandlerKindOf("__CxxFrameHandler4") == ExceptionHandlerKind::Cxx4);
	CHECK(ExceptionHandlerKindOf("__GSHandlerCheck_EH4") == ExceptionHandlerKind::Cxx4);
	CHECK(ExceptionHandlerKindOf("__GSHandlerCheck") == ExceptionHandlerKind::None);
	CHECK(ExceptionHandlerKindOf("") == ExceptionHandlerKind::None);
}

TEST_CASE("The compressed integers of __CxxFrameHandler4 are read", "[eh]") {
	for (uint32_t value : { 0u, 1u, 0x7Fu, 0x80u, 0x3FFFu, 0x4000u, 0x1FFFFFu, 0x200000u, 0xFFFFFFFu, 0x10000000u, 0xFFFFFFFFu }) {
		std::vector<uint8_t> bytes;
		AddCompressed(bytes, value);
		bytes.push_back(0xAA);	// (what follows is not read)
		size_t used = 0;
		uint32_t read = 0;
		INFO(value);
		REQUIRE(ReadCompressedUnsigned(bytes, used, read));
		CHECK(read == value);
		CHECK(used == bytes.size() - 1);
	}
	size_t used = 0;
	uint32_t read;
	CHECK_FALSE(ReadCompressedUnsigned({}, used, read));
	std::vector<uint8_t> cut{ 0x07, 0x01 };		// 4 bytes, 2 there
	CHECK_FALSE(ReadCompressedUnsigned(cut, used, read));
}

TEST_CASE("A scope table gives __except and __finally regions", "[eh]") {
	Image image;
	std::vector<uint8_t> table;
	Add32(table, 4);
	// an inner __finally, an __except with a filter function in two pieces, one with EXCEPTION_EXECUTE_HANDLER
	for (auto [begin, end, handler, target] : std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>>{
		{ 0x1010, 0x1020, 0x3000, 0 }, { 0x1008, 0x1030, 0x3100, 0x1080 }, { 0x1040, 0x1050, 0x3100, 0x1080 }, { 0x1060, 0x1070, 1, 0x1090 } }) {
		Add32(table, begin);
		Add32(table, end);
		Add32(table, handler);
		Add32(table, target);
	}
	image.Put(0x5000, table);
	auto eh = ReadExceptionHandling(image.Reader(), ExceptionHandlerKind::Seh, 0x2000, 0x5000, 0x1000, 0x1100);
	CHECK(eh.Error.empty());
	REQUIRE(eh.Scopes.size() == 3);
	// inner ones (smaller) first
	CHECK(eh.Scopes[0].Kind == ExceptionScope::Kinds::Finally);
	CHECK(eh.Scopes[0].HandlerRva == 0x3000);
	CHECK(eh.Scopes[1].Kind == ExceptionScope::Kinds::Except);
	CHECK(eh.Scopes[1].FilterRva == 0);		// EXCEPTION_EXECUTE_HANDLER
	CHECK(eh.Scopes[1].HandlerRva == 0x1090);
	auto const& split = eh.Scopes[2];
	CHECK(split.FilterRva == 0x3100);
	CHECK(split.HandlerRva == 0x1080);
	REQUIRE(split.Ranges.size() == 2);
	CHECK(split.Ranges[0] == std::pair<uint32_t, uint32_t>(0x1008, 0x1030));
	CHECK(split.Ranges[1] == std::pair<uint32_t, uint32_t>(0x1040, 0x1050));
}

TEST_CASE("A damaged scope table is said to be", "[eh]") {
	Image image;
	std::vector<uint8_t> table;
	Add32(table, 3);	// three entries, one there
	for (uint32_t v : { 0x1000u, 0x1010u, 1u, 0x1020u })
		Add32(table, v);
	image.Put(0x5000, table);
	auto eh = ReadExceptionHandling(image.Reader(), ExceptionHandlerKind::Seh, 0x2000, 0x5000, 0x1000, 0x1100);
	CHECK_FALSE(eh.Error.empty());
	CHECK(eh.Scopes.size() == 1);
	// a count that cannot be
	Image huge;
	std::vector<uint8_t> bad;
	Add32(bad, 0x7FFFFFFF);
	huge.Put(0x5000, bad);
	CHECK_FALSE(ReadExceptionHandling(huge.Reader(), ExceptionHandlerKind::Seh, 0x2000, 0x5000, 0x1000, 0x1100).Error.empty());
}

TEST_CASE("The try blocks of __CxxFrameHandler3 come from the states of the code", "[eh]") {
	Image image;
	std::vector<uint8_t> data;
	Add32(data, 0x6000);	// the FuncInfo
	image.Put(0x5000, data);
	std::vector<uint8_t> info;
	for (uint32_t v : { 0x19930522u, 3u, 0u, 1u, 0x6100u, 4u, 0x6200u, 0u, 0u, 1u })
		Add32(info, v);
	image.Put(0x6000, info);
	// try: states 0 to 1, two catches
	std::vector<uint8_t> tries;
	for (uint32_t v : { 0u, 1u, 2u, 2u, 0x6300u })
		Add32(tries, v);
	image.Put(0x6100, tries);
	std::vector<uint8_t> map;
	for (auto [ip, state] : std::vector<std::pair<uint32_t, int32_t>>{ { 0x1000, -1 }, { 0x1010, 0 }, { 0x1020, 1 }, { 0x1030, -1 } }) {
		Add32(map, ip);
		Add32(map, (uint32_t)state);
	}
	image.Put(0x6200, map);
	std::vector<uint8_t> handlers;
	for (uint32_t v : { 9u, 0x6400u, 0x28u, 0x7000u, 0x38u, 0u, 0u, 0u, 0x7100u, 0x38u })
		Add32(handlers, v);
	image.Put(0x6300, handlers);
	image.Put(0x6400, TypeDescriptor(".?AVexception@std@@"));

	auto eh = ReadExceptionHandling(image.Reader(), ExceptionHandlerKind::Cxx3, 0x2000, 0x5000, 0x1000, 0x1040);
	CHECK(eh.Error.empty());
	REQUIRE(eh.Scopes.size() == 1);
	auto const& scope = eh.Scopes[0];
	CHECK(scope.Kind == ExceptionScope::Kinds::Try);
	REQUIRE(scope.Ranges.size() == 1);
	CHECK(scope.Ranges[0] == std::pair<uint32_t, uint32_t>(0x1010, 0x1030));
	REQUIRE(scope.Catches.size() == 2);
	CHECK(scope.Catches[0].Type == ".?AVexception@std@@");
	CHECK(scope.Catches[0].Adjectives == 9);
	CHECK(scope.Catches[0].HandlerRva == 0x7000);
	CHECK(scope.Catches[1].Type.empty());	// catch (...)
	CHECK(scope.Catches[1].HandlerRva == 0x7100);
}

TEST_CASE("The try blocks of __CxxFrameHandler4 are read from its compressed data", "[eh]") {
	Image image;
	std::vector<uint8_t> data;
	Add32(data, 0x6000);
	image.Put(0x5000, data);
	// FuncInfo4: an unwind map, a try block map, the IP-to-state map
	std::vector<uint8_t> info{ 0x18 };
	Add32(info, 0x6050);
	Add32(info, 0x6100);
	Add32(info, 0x6200);
	image.Put(0x6000, info);
	std::vector<uint8_t> tries;
	AddCompressed(tries, 1);	// one try
	AddCompressed(tries, 0);	// states 0..1
	AddCompressed(tries, 1);
	AddCompressed(tries, 2);
	Add32(tries, 0x6300);
	image.Put(0x6100, tries);
	// the code's states, from the function's start: -1, then 0 at +0x10, 1 at +0x20, -1 at +0x200
	std::vector<uint8_t> map;
	AddCompressed(map, 4);
	for (auto [delta, state] : std::vector<std::pair<uint32_t, int32_t>>{ { 0, -1 }, { 0x10, 0 }, { 0x10, 1 }, { 0x1E0, -1 } }) {
		AddCompressed(map, delta);
		AddCompressed(map, (uint32_t)(state + 1));
	}
	image.Put(0x6200, map);
	std::vector<uint8_t> handlers;
	AddCompressed(handlers, 2);
	// const std::exception& (adjectives, a type), its continuation an offset
	handlers.push_back(0x01 | 0x02 | 0x10);
	AddCompressed(handlers, 9);
	Add32(handlers, 0x6400);
	Add32(handlers, 0x7000);
	AddCompressed(handlers, 0x40);
	// catch (...): its continuation an RVA
	handlers.push_back(0x08 | 0x10);
	Add32(handlers, 0x7100);
	Add32(handlers, 0x1300);
	image.Put(0x6300, handlers);
	image.Put(0x6400, TypeDescriptor(".?AVexception@std@@"));

	auto eh = ReadExceptionHandling(image.Reader(), ExceptionHandlerKind::Cxx4, 0x2000, 0x5000, 0x1000, 0x1400);
	CHECK(eh.Error.empty());
	REQUIRE(eh.Scopes.size() == 1);
	auto const& scope = eh.Scopes[0];
	REQUIRE(scope.Ranges.size() == 1);
	CHECK(scope.Ranges[0] == std::pair<uint32_t, uint32_t>(0x1010, 0x1200));
	REQUIRE(scope.Catches.size() == 2);
	CHECK(scope.Catches[0].Type == ".?AVexception@std@@");
	CHECK(scope.Catches[0].Adjectives == 9);
	CHECK(scope.Catches[0].HandlerRva == 0x7000);
	CHECK(scope.Catches[1].Type.empty());
	CHECK(scope.Catches[1].HandlerRva == 0x7100);
}

TEST_CASE("A catch funclet's own FuncInfo4 has no try blocks of the function", "[eh]") {
	Image image;
	std::vector<uint8_t> data;
	Add32(data, 0x6000);
	image.Put(0x5000, data);
	std::vector<uint8_t> info{ 0x01 | 0x10 };	// isCatch
	Add32(info, 0x6100);
	Add32(info, 0x6200);
	AddCompressed(info, 0x40);
	image.Put(0x6000, info);
	auto eh = ReadExceptionHandling(image.Reader(), ExceptionHandlerKind::Cxx4, 0x2000, 0x5000, 0x1000, 0x1400);
	CHECK(eh.Error.empty());
	CHECK(eh.Scopes.empty());
}

// A file of the caller's (TOTALPE_EH_FILE, the RVA of a function in TOTALPE_EH_RVA, the handler's name in TOTALPE_EH_HANDLER):
// what is read of it
TEST_CASE("The exception handling of a function of a file", "[.][eh-file]") {
	auto variable = [](char const* name) {
		char value[1024]{};
		return ::GetEnvironmentVariableA(name, value, sizeof(value)) ? std::string(value) : std::string();
	};
	auto file = variable("TOTALPE_EH_FILE"), rva = variable("TOTALPE_EH_RVA"), handlerName = variable("TOTALPE_EH_HANDLER");
	REQUIRE(!file.empty());
	PEFile pe;
	REQUIRE(pe.Open(std::wstring(file.begin(), file.end())));
	auto eh = ReadExceptionHandling(pe, (uint32_t)strtoul(rva.c_str(), nullptr, 16), [&](uint32_t handler) {
		WARN("handler at " << std::hex << handler);
		return handlerName;
		});
	WARN("kind " << (int)eh.Kind << " error '" << (eh.Error.empty() ? "" : "yes") << "' scopes " << eh.Scopes.size());
	for (auto const& s : eh.Scopes) {
		std::string text = std::to_string((int)s.Kind) + ":";
		for (auto [b, e] : s.Ranges)
			text += std::format(" [{:x},{:x})", b, e);
		text += std::format(" filter {:x} handler {:x}", s.FilterRva, s.HandlerRva);
		for (auto const& c : s.Catches)
			text += std::format(" catch '{}' {:x} {:x}", c.Type, c.Adjectives, c.HandlerRva);
		WARN(text);
	}
}
