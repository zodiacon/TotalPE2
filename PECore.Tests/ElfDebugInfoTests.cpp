#include "TestCommon.h"
#include "ByteWriter.h"
#include <ElfFile.h>
#include <ElfDebugInfo.h>
#include <zlib.h>
#include <algorithm>

namespace {
	using Bytes = std::vector<uint8_t>;

	struct TestSection {
		std::string Name;
		Bytes Data;
		uint64_t Flags{};
		uint32_t Type{ 1 };		// SHT_PROGBITS
	};

	// A little-endian ELF64 executable that has only the sections given (and the section names)
	Bytes BuildElf(std::vector<TestSection> sections) {
		ByteWriter names;
		names.U8(0);
		std::vector<uint32_t> nameOffsets;
		sections.push_back({ ".shstrtab", {}, 0, 3 });
		for (auto const& s : sections) {
			nameOffsets.push_back((uint32_t)names.Bytes.size());
			for (char c : s.Name)
				names.U8((uint8_t)c);
			names.U8(0);
		}
		sections.back().Data = names.Bytes;

		ByteWriter w;
		w.Bytes.resize(64);
		std::vector<uint64_t> offsets;
		for (auto const& s : sections) {
			w.Align(8);
			offsets.push_back(w.Bytes.size());
			w.Bytes.insert(w.Bytes.end(), s.Data.begin(), s.Data.end());
		}
		w.Align(8);
		uint64_t shoff = w.Bytes.size();
		// the null section, then the others
		w.Bytes.resize(w.Bytes.size() + 64);
		for (size_t i = 0; i < sections.size(); i++) {
			auto const& s = sections[i];
			w.U32(nameOffsets[i]).U32(s.Type);
			w.U32((uint32_t)s.Flags).U32((uint32_t)(s.Flags >> 32));
			w.U32(0).U32(0);											// address
			w.U32((uint32_t)offsets[i]).U32(0);
			w.U32((uint32_t)s.Data.size()).U32(0);
			w.U32(0).U32(0);											// link, info
			w.U32(1).U32(0);											// alignment
			w.U32(0).U32(0);											// entry size
		}
		auto& b = w.Bytes;
		uint8_t ident[16] = { 0x7F, 'E', 'L', 'F', 2, 1, 1 };
		memcpy(b.data(), ident, 16);
		auto put16 = [&](size_t at, uint16_t v) { memcpy(b.data() + at, &v, 2); };
		auto put32 = [&](size_t at, uint32_t v) { memcpy(b.data() + at, &v, 4); };
		auto put64 = [&](size_t at, uint64_t v) { memcpy(b.data() + at, &v, 8); };
		put16(16, 2);			// ET_EXEC
		put16(18, 62);			// x86-64
		put32(20, 1);
		put64(40, shoff);
		put16(52, 64);
		put16(54, 56);
		put16(58, 64);
		put16(60, (uint16_t)(sections.size() + 1));
		put16(62, (uint16_t)sections.size());	// .shstrtab, the last one
		return b;
	}

	void Uleb(ByteWriter& w, uint64_t v) {
		do {
			uint8_t b = v & 0x7F;
			v >>= 7;
			w.U8(b | (v ? 0x80 : 0));
		} while (v);
	}

	void CString(ByteWriter& w, std::string_view s) {
		for (char c : s)
			w.U8((uint8_t)c);
		w.U8(0);
	}

	void U64(ByteWriter& w, uint64_t v) {
		w.U32((uint32_t)v).U32((uint32_t)(v >> 32));
	}

	// DWARF 4: a compile unit with a function, and a definition that has its name in its declaration (a member function)
	struct Dwarf4 {
		Bytes Abbrev, Info, Str;
	};

	Dwarf4 BuildDwarf4() {
		ByteWriter str;
		str.U8(0);
		auto file = (uint32_t)str.Bytes.size();
		CString(str, "widget.cpp");
		auto linkage = (uint32_t)str.Bytes.size();
		CString(str, "_Z3addii");

		ByteWriter a;
		// 1: the unit (children): name strp, producer string, language data1, low_pc addr, high_pc data4, comp_dir string
		Uleb(a, 1); Uleb(a, 0x11); a.U8(1);
		Uleb(a, 0x03); Uleb(a, 0x0E); Uleb(a, 0x25); Uleb(a, 0x08); Uleb(a, 0x13); Uleb(a, 0x0B);
		Uleb(a, 0x11); Uleb(a, 0x01); Uleb(a, 0x12); Uleb(a, 0x06); Uleb(a, 0x1B); Uleb(a, 0x08); a.U8(0); a.U8(0);
		// 2: a function: name string, linkage name strp, low_pc addr, high_pc data4, external flag_present, decl_line data1
		Uleb(a, 2); Uleb(a, 0x2E); a.U8(0);
		Uleb(a, 0x03); Uleb(a, 0x08); Uleb(a, 0x6E); Uleb(a, 0x0E); Uleb(a, 0x11); Uleb(a, 0x01); Uleb(a, 0x12); Uleb(a, 0x06);
		Uleb(a, 0x3F); Uleb(a, 0x19); Uleb(a, 0x3B); Uleb(a, 0x0B); a.U8(0); a.U8(0);
		// 3: a definition: specification ref4, low_pc addr, high_pc data4
		Uleb(a, 3); Uleb(a, 0x2E); a.U8(0);
		Uleb(a, 0x47); Uleb(a, 0x13); Uleb(a, 0x11); Uleb(a, 0x01); Uleb(a, 0x12); Uleb(a, 0x06); a.U8(0); a.U8(0);
		// 4: a declaration: name string, linkage name string, declaration flag_present
		Uleb(a, 4); Uleb(a, 0x2E); a.U8(0);
		Uleb(a, 0x03); Uleb(a, 0x08); Uleb(a, 0x6E); Uleb(a, 0x08); Uleb(a, 0x3C); Uleb(a, 0x19); a.U8(0); a.U8(0);
		a.U8(0);

		ByteWriter body;
		body.U16(4).U32(0).U8(8);	// version, abbreviations at 0, addresses of 8 bytes
		Uleb(body, 1);
		body.U32(file);
		CString(body, "GNU C++14 15.2.0 -g");
		body.U8(0x21);
		U64(body, 0x401000);
		body.U32(0x100);
		CString(body, "/home/dev/src");
		Uleb(body, 2);
		CString(body, "add");
		body.U32(linkage);
		U64(body, 0x401000);
		body.U32(0x20);
		body.U8(7);
		// the definition refers to the declaration after it: its offset in the unit
		Uleb(body, 3);
		auto refAt = body.Bytes.size();
		body.U32(0);
		U64(body, 0x401020);
		body.U32(0x30);
		auto declaration = (uint32_t)(4 + body.Bytes.size());	// from the start of the unit (the length comes first)
		memcpy(body.Bytes.data() + refAt, &declaration, 4);
		Uleb(body, 4);
		CString(body, "run");
		CString(body, "_ZN6Widget3runEv");
		body.U8(0);		// the end of the children of the unit

		ByteWriter info;
		info.U32((uint32_t)body.Bytes.size());
		info.Bytes.insert(info.Bytes.end(), body.Bytes.begin(), body.Bytes.end());
		return { a.Bytes, info.Bytes, str.Bytes };
	}

	std::span<const std::byte> AsBytes(Bytes const& b) {
		return std::as_bytes(std::span(b));
	}
}

TEST_CASE("The DWARF 4 of an ELF file", "[elf][dwarf]") {
	auto dwarf = BuildDwarf4();
	auto data = BuildElf({ { ".debug_abbrev", dwarf.Abbrev }, { ".debug_info", dwarf.Info }, { ".debug_str", dwarf.Str } });
	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(data)));
	auto info = ReadDwarf(elf);
	CHECK(info.Problems.empty());
	REQUIRE(info.Units.size() == 1);
	auto const& unit = info.Units[0];
	CHECK(unit.Version == 4);
	CHECK(unit.Name == "widget.cpp");
	CHECK(unit.Producer == "GNU C++14 15.2.0 -g");
	CHECK(unit.CompDir == "/home/dev/src");
	CHECK(DwarfInfo::LanguageName(unit.Language) == L"C++14");
	CHECK(unit.LowPc == 0x401000);
	CHECK(unit.HighPc == 0x401100);
	CHECK(unit.Functions == 2);
	REQUIRE(info.Functions.size() == 2);
	auto const& add = info.Functions[0];
	CHECK(add.Name == "add");
	CHECK(add.LinkageName == "_Z3addii");
	CHECK(add.LowPc == 0x401000);
	CHECK(add.HighPc == 0x401020);
	CHECK(add.External);
	CHECK(add.Line == 7);
	// the definition has the names of its declaration
	auto const& run = info.Functions[1];
	CHECK(run.Name == "run");
	CHECK(run.LinkageName == "_ZN6Widget3runEv");
	CHECK(run.LowPc == 0x401020);
	CHECK(run.HighPc == 0x401050);
	REQUIRE(info.Sections.size() == 3);
	CHECK(info.Sections[0].Compression.empty());
}

TEST_CASE("DWARF 5: string and address indices", "[elf][dwarf]") {
	ByteWriter str;
	str.U8(0);
	auto unitName = (uint32_t)str.Bytes.size();
	CString(str, "main.c");
	auto fnName = (uint32_t)str.Bytes.size();
	CString(str, "main");

	// the offsets of the strings, after the header (8 bytes): the base of the unit
	ByteWriter offsets;
	offsets.U32(4 + 8).U16(5).U16(0).U32(unitName).U32(fnName);
	ByteWriter addr;
	addr.U32(4 + 16).U16(5).U8(8).U8(0);
	U64(addr, 0x1130);
	U64(addr, 0x1140);

	ByteWriter a;
	// 1: unit: name strx1, str_offsets_base sec_offset, addr_base sec_offset, low_pc addrx, high_pc data4, language data2
	Uleb(a, 1); Uleb(a, 0x11); a.U8(1);
	Uleb(a, 0x03); Uleb(a, 0x25); Uleb(a, 0x72); Uleb(a, 0x17); Uleb(a, 0x73); Uleb(a, 0x17);
	Uleb(a, 0x11); Uleb(a, 0x1B); Uleb(a, 0x12); Uleb(a, 0x06); Uleb(a, 0x13); Uleb(a, 0x05); a.U8(0); a.U8(0);
	// 2: function: name strx1, low_pc addrx1, high_pc data4 (implicit_const: no data)
	Uleb(a, 2); Uleb(a, 0x2E); a.U8(0);
	Uleb(a, 0x03); Uleb(a, 0x25); Uleb(a, 0x11); Uleb(a, 0x29); Uleb(a, 0x12); Uleb(a, 0x21); a.U8(0x10); a.U8(0); a.U8(0);
	a.U8(0);

	ByteWriter body;
	body.U16(5).U8(1).U8(8).U32(0);		// version, DW_UT_compile, address size, abbreviations
	Uleb(body, 1);
	body.U8(0);			// the first string
	body.U32(8);		// the base of the string offsets
	body.U32(8);		// the base of the addresses
	Uleb(body, 0);		// the first address
	body.U32(0x40);
	body.U16(0x1D);		// C11
	Uleb(body, 2);
	body.U8(1);			// the second string
	body.U8(1);			// the second address
	body.U8(0);
	ByteWriter info;
	info.U32((uint32_t)body.Bytes.size());
	info.Bytes.insert(info.Bytes.end(), body.Bytes.begin(), body.Bytes.end());

	auto data = BuildElf({ { ".debug_abbrev", a.Bytes }, { ".debug_info", info.Bytes }, { ".debug_str", str.Bytes },
		{ ".debug_str_offsets", offsets.Bytes }, { ".debug_addr", addr.Bytes } });
	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(data)));
	auto dwarf = ReadDwarf(elf);
	CHECK(dwarf.Problems.empty());
	REQUIRE(dwarf.Units.size() == 1);
	CHECK(dwarf.Units[0].Version == 5);
	CHECK(dwarf.Units[0].Name == "main.c");
	CHECK(dwarf.Units[0].LowPc == 0x1130);
	CHECK(dwarf.Units[0].HighPc == 0x1170);
	CHECK(DwarfInfo::LanguageName(dwarf.Units[0].Language) == L"C11");
	REQUIRE(dwarf.Functions.size() == 1);
	CHECK(dwarf.Functions[0].Name == "main");
	CHECK(dwarf.Functions[0].LowPc == 0x1140);
	CHECK(dwarf.Functions[0].HighPc == 0x1150);	// the implicit constant
}

TEST_CASE("Compressed DWARF sections", "[elf][dwarf]") {
	auto dwarf = BuildDwarf4();
	// Elf64_Chdr: ELFCOMPRESS_ZLIB, reserved, the size, the alignment; then the zlib stream
	uLongf size = compressBound((uLong)dwarf.Info.size());
	Bytes compressed(size);
	REQUIRE(compress(compressed.data(), &size, dwarf.Info.data(), (uLong)dwarf.Info.size()) == Z_OK);
	compressed.resize(size);
	ByteWriter z;
	z.U32(1).U32(0);
	U64(z, dwarf.Info.size());
	U64(z, 1);
	z.Bytes.insert(z.Bytes.end(), compressed.begin(), compressed.end());

	auto data = BuildElf({ { ".debug_abbrev", dwarf.Abbrev }, { ".debug_info", z.Bytes, 0x800 }, { ".debug_str", dwarf.Str } });
	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(data)));
	auto info = ReadDwarf(elf);
	CHECK(info.Problems.empty());
	REQUIRE(info.Units.size() == 1);
	CHECK(info.Functions.size() == 2);
	auto section = std::ranges::find(info.Sections, std::string(".debug_info"), &DwarfSection::Name);
	REQUIRE(section != info.Sections.end());
	CHECK(section->Compression == L"zlib");
	CHECK(section->UncompressedSize == dwarf.Info.size());

	SECTION("damaged") {
		auto bad = data;
		auto at = std::search(bad.begin(), bad.end(), compressed.begin(), compressed.begin() + 4) - bad.begin();
		bad[at + 4] ^= 0xFF;
		bad[at + 5] ^= 0xFF;
		ElfFile damaged;
		REQUIRE(damaged.Parse(AsBytes(bad)));
		auto d = ReadDwarf(damaged);
		CHECK(d.Units.empty());
		CHECK_FALSE(d.Problems.empty());
	}
}

TEST_CASE("Damaged DWARF is read as far as it goes", "[elf][dwarf]") {
	auto dwarf = BuildDwarf4();
	// every truncation of .debug_info and .debug_abbrev: no crash
	for (size_t n = 0; n < dwarf.Info.size(); n++) {
		Bytes info(dwarf.Info.begin(), dwarf.Info.begin() + n);
		auto data = BuildElf({ { ".debug_abbrev", dwarf.Abbrev }, { ".debug_info", info }, { ".debug_str", dwarf.Str } });
		ElfFile elf;
		REQUIRE(elf.Parse(AsBytes(data)));
		(void)ReadDwarf(elf);
	}
	for (size_t n = 0; n < dwarf.Abbrev.size(); n++) {
		Bytes abbrev(dwarf.Abbrev.begin(), dwarf.Abbrev.begin() + n);
		auto data = BuildElf({ { ".debug_abbrev", abbrev }, { ".debug_info", dwarf.Info }, { ".debug_str", dwarf.Str } });
		ElfFile elf;
		REQUIRE(elf.Parse(AsBytes(data)));
		(void)ReadDwarf(elf);
	}
}

TEST_CASE("The link to the debug file", "[elf][dwarf]") {
	ByteWriter link;
	CString(link, "app.debug");
	link.Align(4);
	link.U32(0x12345678);
	auto data = BuildElf({ { ".gnu_debuglink", link.Bytes } });
	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(data)));
	auto l = ReadDebugLink(elf);
	CHECK(l.Present);
	CHECK(l.File == "app.debug");
	CHECK(l.Crc == 0x12345678);
	CHECK(ReadDwarf(elf).Empty());

	// next to the file, in .debug; in a WSL distribution, in /usr/lib/debug too
	auto local = DebugFileCandidates(L"C:\\bin\\app", l, "");
	REQUIRE(local.size() == 2);
	CHECK(local[0] == L"C:\\bin\\app.debug");
	CHECK(local[1] == L"C:\\bin\\.debug\\app.debug");
	auto wsl = DebugFileCandidates(L"\\\\wsl.localhost\\Ubuntu\\usr\\bin\\app", l, "abcdef0123");
	CHECK(std::ranges::find(wsl, L"\\\\wsl.localhost\\Ubuntu\\usr\\lib\\debug\\.build-id\\ab\\cdef0123.debug") != wsl.end());
	CHECK(std::ranges::find(wsl, L"\\\\wsl.localhost\\Ubuntu\\usr\\lib\\debug\\usr\\bin\\app.debug") != wsl.end());
	CHECK(DebugFileCandidates(L"C:\\bin\\app", {}, "abcd").empty());

	// the CRC of GNU debuglink is the usual CRC-32
	Bytes text{ '1', '2', '3', '4', '5', '6', '7', '8', '9' };
	CHECK(Crc32(AsBytes(text)) == 0xCBF43926);
}

// A file to look at: TOTALPE_ELF_DWARF=<path> PECore.Tests "[.dwarfdump]"
TEST_CASE("Dump the debug information of an ELF file", "[.dwarfdump]") {
	wchar_t path[MAX_PATH];
	if (::GetEnvironmentVariableW(L"TOTALPE_ELF_DWARF", path, MAX_PATH) == 0)
		SKIP("TOTALPE_ELF_DWARF is not set");
	ElfFile elf;
	REQUIRE(elf.Open(path));
	auto info = LoadElfDebugInfo(elf);
	printf("link: %s crc %08X present %d\ndebug file: %s\n", info.Link.File.c_str(), info.Link.Crc, info.Link.Present, Narrow(info.DebugFile).c_str());
	for (auto const& s : info.Searched)
		printf("searched: %s\n", Narrow(s).c_str());
	for (auto const& s : info.Dwarf.Sections)
		printf("section %s %llu -> %llu %s\n", s.Name.c_str(), s.Size, s.UncompressedSize, Narrow(s.Compression).c_str());
	for (auto const& p : info.Dwarf.Problems)
		printf("problem: %s\n", Narrow(p).c_str());
	printf("%zu units, %zu functions\n", info.Dwarf.Units.size(), info.Dwarf.Functions.size());
	for (size_t i = 0; i < info.Dwarf.Units.size() && i < 4; i++) {
		auto const& u = info.Dwarf.Units[i];
		printf("unit v%u %s | %s | %s | %s | %llX-%llX | %u functions\n", u.Version, u.Name.c_str(), u.Producer.c_str(), u.CompDir.c_str(),
			Narrow(DwarfInfo::LanguageName(u.Language)).c_str(), u.LowPc, u.HighPc, u.Functions);
	}
	for (size_t i = 0; i < info.Dwarf.Functions.size() && i < 25; i++) {
		auto const& f = info.Dwarf.Functions[i];
		printf("fn %llX-%llX %s | %s | line %u\n", f.LowPc, f.HighPc, f.Name.c_str(), f.LinkageName.c_str(), f.Line);
	}
}
