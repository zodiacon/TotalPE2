#include "TestCommon.h"
#include "ByteWriter.h"
#include <CoffObject.h>
#include <LibArchive.h>
#include <GuardTables.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>

namespace {
	using Bytes = std::vector<uint8_t>;

	void Put32(Bytes& b, size_t at, uint32_t v) {
		memcpy(b.data() + at, &v, 4);
	}

	void Name8(ByteWriter& w, std::string_view name) {
		for (size_t i = 0; i < 8; i++)
			w.U8(i < name.size() ? (uint8_t)name[i] : 0);
	}

	void Section(ByteWriter& w, std::string_view name, uint32_t rawSize, uint32_t rawPtr, uint32_t relPtr, uint16_t relocs, uint32_t characteristics) {
		Name8(w, name);
		w.U32(0).U32(0).U32(rawSize).U32(rawPtr).U32(relPtr).U32(0).U16(relocs).U16(0).U32(characteristics);
	}

	// An x64 object: .text (with a call to an external function), .drectve and a section with a long name;
	// a file symbol, the definition of .text (a COMDAT), an external function and main
	Bytes BuildObject() {
		ByteWriter w;
		w.U16(0x8664).U16(3).U32(0x12345678).U32(0).U32(6).U16(0).U16(0);
		Section(w, ".text", 8, 140, 148, 1, IMAGE_SCN_CNT_CODE | IMAGE_SCN_LNK_COMDAT | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
		Section(w, ".drectve", 20, 158, 0, 0, IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE);
		Section(w, "/4", 4, 178, 0, 0, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);
		REQUIRE(w.Bytes.size() == 140);
		for (uint8_t b : { 0xE8, 0, 0, 0, 0, 0xC3, 0x90, 0x90 })	// call ExternalFunction; ret
			w.U8(b);
		w.U32(1).U32(4).U16(4);		// the relocation: at 1, symbol 4, REL32
		for (char c : std::string_view(" /DEFAULTLIB:LIBCMT "))
			w.U8((uint8_t)c);
		w.U32(0xDEADBEEF);			// the section with the long name
		REQUIRE(w.Bytes.size() == 182);

		// the symbol table
		Name8(w, ".file");
		w.U32(0).U16((uint16_t)IMAGE_SYM_DEBUG).U16(0).U8(IMAGE_SYM_CLASS_FILE).U8(1);
		Name8(w, "test.cpp");
		w.U32(0).U32(0).U16(0);		// the rest of the auxiliary record (18 bytes)
		Name8(w, ".text");
		w.U32(0).U16(1).U16(0).U8(IMAGE_SYM_CLASS_STATIC).U8(1);
		w.U32(8).U16(1).U16(0).U32(0xAABBCCDD).U16(0).U8(2).U8(0).U16(0);	// length, relocations, line numbers, checksum, number, selection: Any
		w.U32(0).U32(21);			// a long name: zeros, then the offset in the string table
		w.U32(0).U16(0).U16(0x20).U8(IMAGE_SYM_CLASS_EXTERNAL).U8(0);
		Name8(w, "main");
		w.U32(0).U16(1).U16(0x20).U8(IMAGE_SYM_CLASS_EXTERNAL).U8(0);
		REQUIRE(w.Bytes.size() == 182 + 6 * 18);

		// the string table: its size, then the names
		w.U32(4 + 17 + 17);
		for (char c : std::string_view(".rdata$long_name"))
			w.U8((uint8_t)c);
		w.U8(0);
		for (char c : std::string_view("ExternalFunction"))
			w.U8((uint8_t)c);
		w.U8(0);

		auto bytes = w.Bytes;
		Put32(bytes, 8, 182);		// the symbol table
		return bytes;
	}

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }

	Bytes ReadFile(std::filesystem::path const& path) {
		std::ifstream in(path, std::ios::binary);
		return Bytes(std::istreambuf_iterator<char>(in), {});
	}
}

TEST_CASE("A COFF object", "[coff]") {
	auto data = BuildObject();
	REQUIRE(CoffObject::IsObject(AsBytes(data)));
	CoffObject obj;
	REQUIRE(obj.Parse(AsBytes(data)));
	CHECK(obj.Problems().empty());
	CHECK(obj.Machine() == 0x8664);
	CHECK(obj.TimeDateStamp() == 0x12345678);
	CHECK_FALSE(obj.BigObj());

	SECTION("sections") {
		auto const& s = obj.Sections();
		REQUIRE(s.size() == 3);
		CHECK(s[0].Name == ".text");
		CHECK(s[1].Name == ".drectve");
		CHECK(s[2].Name == ".rdata$long_name");	// from the string table
		CHECK(obj.SectionData(0).size() == 8);
		CHECK(std::to_integer<int>(obj.SectionData(0)[0]) == 0xE8);
		CHECK(obj.Directives() == "/DEFAULTLIB:LIBCMT");
	}
	SECTION("symbols") {
		auto const& syms = obj.Symbols();
		REQUIRE(syms.size() == 4);	// the auxiliary records are not symbols
		CHECK(syms[0].Name == ".file");
		CHECK(syms[0].Details == "test.cpp");
		CHECK(syms[1].Index == 2);
		CHECK(syms[1].Details.find("COMDAT: Any") != std::string::npos);
		CHECK(syms[1].Details.find("Length 0x8") != std::string::npos);
		CHECK(syms[2].Name == "ExternalFunction");
		CHECK(syms[2].SectionNumber == 0);
		CHECK(CoffObject::SectionNumberName(syms[2].SectionNumber) == L"Undefined");
		CHECK(syms[3].Name == "main");
		CHECK(syms[3].Index == 5);
		CHECK(obj.SymbolByIndex(1) == nullptr);		// an auxiliary record
		REQUIRE(obj.SymbolByIndex(4));
		CHECK(obj.SymbolByIndex(4)->Name == "ExternalFunction");
		CHECK(obj.SymbolByIndex(6) == nullptr);
		CHECK(CoffObject::StorageClassName(IMAGE_SYM_CLASS_EXTERNAL) == L"External");
	}
	SECTION("relocations") {
		auto const& r = obj.Relocations();
		REQUIRE(r.size() == 1);
		CHECK(r[0].Section == 0);
		CHECK(r[0].Offset == 1);
		CHECK(r[0].SymbolIndex == 4);
		CHECK(CoffObject::RelocationTypeName(obj.Machine(), r[0].Type) == L"REL32");
		CHECK(CoffObject::RelocationTypeName(IMAGE_FILE_MACHINE_I386, 0x14) == L"REL32");
		CHECK(CoffObject::RelocationTypeName(IMAGE_FILE_MACHINE_ARM64, 3) == L"BRANCH26");
		CHECK(CoffObject::RelocationTypeName(0x8664, 0x99) == L"0x99");
	}
}

TEST_CASE("A bigobj object", "[coff]") {
	static const uint8_t classId[16] = { 0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B, 0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8 };
	ByteWriter w;
	w.U16(0).U16(0xFFFF).U16(2).U16(0x8664).U32(0x1000);
	for (auto b : classId)
		w.U8(b);
	w.U32(0).U32(0).U32(0).U32(0).U32(1).U32(56 + 40 + 4).U32(1);		// one section, the symbols after it and its data
	Name8(w, "//AAAAAE");		// offset 4 in the string table, in base 64
	w.U32(0).U32(0).U32(4).U32(56 + 40).U32(0).U32(0).U16(0).U16(0).U32(IMAGE_SCN_CNT_CODE);
	w.U32(0xC3C3C3C3);
	Name8(w, "main");
	w.U32(0).U32(1).U16(0x20).U8(IMAGE_SYM_CLASS_EXTERNAL).U8(0);	// a 32-bit section number
	w.U32(4 + 11);
	for (char c : std::string_view(".text$long"))
		w.U8((uint8_t)c);
	w.U8(0);

	CoffObject obj;
	REQUIRE(obj.Parse(w.Span()));
	CHECK(obj.BigObj());
	CHECK(obj.Machine() == 0x8664);
	REQUIRE(obj.Sections().size() == 1);
	CHECK(obj.Sections()[0].Name == ".text$long");
	REQUIRE(obj.Symbols().size() == 1);
	CHECK(obj.Symbols()[0].Name == "main");
	CHECK(obj.Symbols()[0].SectionNumber == 1);
	CHECK(obj.Problems().empty());
}

TEST_CASE("What is not an object", "[coff]") {
	CHECK_FALSE(CoffObject::IsObject({}));
	Bytes text(100, 'a');
	CHECK_FALSE(CoffObject::IsObject(AsBytes(text)));

	auto dll = ReadFile(L"C:\\Windows\\System32\\version.dll");
	REQUIRE(!dll.empty());
	CHECK_FALSE(CoffObject::IsObject(AsBytes(dll)));
	CHECK_FALSE(CoffObject::IsObjectFile(L"C:\\Windows\\System32\\version.dll"));

	SECTION("a symbol table past the end of the file") {
		auto data = BuildObject();
		Put32(data, 12, 100000);
		CHECK_FALSE(CoffObject::IsObject(AsBytes(data)));
	}
	SECTION("an optional header") {
		auto data = BuildObject();
		data[16] = 0xE0;
		CHECK_FALSE(CoffObject::IsObject(AsBytes(data)));
	}
}

TEST_CASE("A damaged object is read as far as it goes", "[coff]") {
	auto data = BuildObject();
	Put32(data, 20 + 24, 0x7FFFFFFF);	// the relocations of .text
	CoffObject obj;
	REQUIRE(obj.Parse(AsBytes(data)));
	CHECK(obj.Relocations().empty());
	CHECK(obj.Problems().size() == 1);
	CHECK(obj.Symbols().size() == 4);
}

TEST_CASE("The objects of an SDK library", "[coff][system]") {
	std::filesystem::path lib;
	std::error_code ec;
	for (auto const& dir : std::filesystem::directory_iterator(L"C:\\Program Files (x86)\\Windows Kits\\10\\Lib", ec)) {
		auto candidate = dir.path() / L"um" / L"x64" / L"uuid.lib";
		if (std::filesystem::exists(candidate, ec))
			lib = candidate;
	}
	if (lib.empty())
		SKIP("The Windows SDK is not installed");

	LibArchive archive;
	REQUIRE(archive.Open(lib.wstring()));
	int objects = 0;
	for (size_t i = 0; i < archive.Members().size(); i++) {
		auto const& m = archive.Members()[i];
		if (m.Kind != ArchiveMemberKind::Object)
			continue;
		CoffObject obj;
		INFO(m.Name);
		REQUIRE(obj.Parse(archive.MemberData(i)));
		CHECK(obj.Sections().size() == m.Sections);
		CHECK(obj.SymbolTableCount() == m.Symbols);
		CHECK(obj.Problems().empty());
		CHECK(obj.Machine() == m.Machine);
		objects++;
	}
	CHECK(objects > 10);
}

TEST_CASE("The Control Flow Guard tables of system DLLs", "[guard][system]") {
	for (auto name : { L"kernel32.dll", L"ntdll.dll", L"kernelbase.dll" }) {
		PEFile pe;
		REQUIRE(pe.Open(std::wstring(L"C:\\Windows\\System32\\") + name));
		auto tables = ReadGuardTables(pe);
		auto imageSize = pe.GetNTHeader()->NTHdr64.OptionalHeader.SizeOfImage;
		INFO(Narrow(name));
		auto functions = std::ranges::find_if(tables, [](auto const& t) { return t.Kind == GuardTableKind::Functions; });
		REQUIRE(functions != tables.end());
		for (auto const& t : tables) {
			INFO(Narrow(GuardTableName(t.Kind)));
			CHECK(t.Error.empty());
			CHECK(t.Entries.size() == t.Count);
			// the entries are sorted, and in the image: a wrong size of the entries would make a mess of both
			CHECK(std::ranges::is_sorted(t.Entries, {}, &GuardEntry::Rva));
			CHECK(std::ranges::all_of(t.Entries, [&](auto const& e) { return e.Rva < imageSize; }));
		}
	}
}
