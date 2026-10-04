#include "TestCommon.h"
#include <ElfFile.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
	using Bytes = std::vector<uint8_t>;

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }

	// Builds a file piece by piece: each piece is aligned and its offset is returned
	struct Builder {
		Bytes Data;
		bool Big{ false };

		size_t Add(Bytes const& bytes, size_t align = 1) {
			while (Data.size() % align)
				Data.push_back(0);
			auto at = Data.size();
			Data.insert(Data.end(), bytes.begin(), bytes.end());
			return at;
		}
		template<typename T>
		void Put(size_t at, T value) {
			if (Big) {
				for (size_t i = 0; i < sizeof(T); i++)
					Data[at + i] = (uint8_t)((uint64_t)value >> ((sizeof(T) - 1 - i) * 8));
			}
			else
				memcpy(Data.data() + at, &value, sizeof(T));
		}
	};

	Bytes Str(std::string_view s) {
		Bytes b(s.begin(), s.end());
		b.push_back(0);
		return b;
	}

	// a string table and the offset of each string in it
	struct StringTable {
		Bytes Data{ 0 };
		uint32_t Add(std::string_view s) {
			auto at = (uint32_t)Data.size();
			Data.insert(Data.end(), s.begin(), s.end());
			Data.push_back(0);
			return at;
		}
	};

	constexpr uint64_t Base = 0x400000;

	struct Section {
		uint32_t Name, Type;
		uint64_t Flags, Address, Offset, Size;
		uint32_t Link, Info;
		uint64_t Align, EntrySize;
	};

	// An x86-64 executable: an interpreter, code, the dynamic symbols, a dynamic section with two needed libraries,
	// a RELA and a RELR relocation table, a build ID note and a .bss
	Bytes BuildElf64() {
		Builder b;
		b.Data.resize(64 + 4 * 56);		// the header and the program headers

		auto interp = b.Add(Str("/lib64/ld-linux-x86-64.so.2"));
		auto text = b.Add({ 0x55, 0x48, 0x89, 0xE5, 0xC3 }, 16);	// push rbp; mov rbp, rsp; ret

		StringTable dynstr;
		auto libc = dynstr.Add("libc.so.6"), libfoo = dynstr.Add("libfoo.so"), mainName = dynstr.Add("main"), puts = dynstr.Add("puts");
		auto dynstrAt = b.Add(dynstr.Data, 8);

		Bytes dynsym(3 * 24, 0);
		auto symbol = [&](int i, uint32_t name, uint8_t info, uint16_t shndx, uint64_t value, uint64_t size) {
			memcpy(&dynsym[i * 24], &name, 4);
			dynsym[i * 24 + 4] = info;
			memcpy(&dynsym[i * 24 + 6], &shndx, 2);
			memcpy(&dynsym[i * 24 + 8], &value, 8);
			memcpy(&dynsym[i * 24 + 16], &size, 8);
		};
		symbol(1, mainName, 0x12, 2, Base + text, 5);	// GLOBAL FUNC in .text (section 2)
		symbol(2, puts, 0x12, 0, 0, 0);					// GLOBAL FUNC, undefined
		auto dynsymAt = b.Add(dynsym, 8);

		Bytes dynamic;
		auto entry = [&](int64_t tag, uint64_t value) {
			Bytes e(16);
			memcpy(e.data(), &tag, 8);
			memcpy(e.data() + 8, &value, 8);
			dynamic.insert(dynamic.end(), e.begin(), e.end());
		};
		entry(1, libc);
		entry(1, libfoo);
		entry(5, Base + dynstrAt);
		entry(0, 0);
		auto dynamicAt = b.Add(dynamic, 8);

		Bytes rela(24);
		uint64_t relaOffset = Base + 0x1000, relaInfo = (2ull << 32) | 7;	// JUMP_SLOT for puts
		memcpy(rela.data(), &relaOffset, 8);
		memcpy(rela.data() + 8, &relaInfo, 8);
		auto relaAt = b.Add(rela, 8);

		Bytes relr(16);
		uint64_t address = Base + 0x2000, bitmap = (0b101 << 1) | 1;	// the address, then the next word and the third after it
		memcpy(relr.data(), &address, 8);
		memcpy(relr.data() + 8, &bitmap, 8);
		auto relrAt = b.Add(relr, 8);

		Bytes note{ 4, 0, 0, 0, 8, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 0, 1, 2, 3, 4, 5, 6, 7, 8 };
		auto noteAt = b.Add(note, 4);

		StringTable shstr;
		std::vector<Section> sections{
			{},
			{ shstr.Add(".interp"), 1, 2, Base + interp, interp, 28, 0, 0, 1, 0 },
			{ shstr.Add(".text"), 1, 6, Base + text, text, 5, 0, 0, 16, 0 },
			{ shstr.Add(".dynstr"), 3, 2, Base + dynstrAt, dynstrAt, dynstr.Data.size(), 0, 0, 1, 0 },
			{ shstr.Add(".dynsym"), 11, 2, Base + dynsymAt, dynsymAt, dynsym.size(), 3, 1, 8, 24 },
			{ shstr.Add(".dynamic"), 6, 3, Base + dynamicAt, dynamicAt, dynamic.size(), 3, 0, 8, 16 },
			{ shstr.Add(".rela.dyn"), 4, 2, Base + relaAt, relaAt, rela.size(), 4, 0, 8, 24 },
			{ shstr.Add(".relr.dyn"), 19, 2, Base + relrAt, relrAt, relr.size(), 0, 0, 8, 8 },
			{ shstr.Add(".note.gnu.build-id"), 7, 2, Base + noteAt, noteAt, note.size(), 0, 0, 4, 0 },
			{ shstr.Add(".bss"), 8, 3, Base + 0x3000, 0, 0x100, 0, 0, 16, 0 },
			{ shstr.Add(".shstrtab"), 3, 0, 0, 0, 0, 0, 0, 1, 0 },
		};
		auto shstrAt = b.Add(shstr.Data);
		sections.back().Offset = shstrAt;
		sections.back().Size = shstr.Data.size();
		auto fileSize = b.Data.size();

		Bytes table(sections.size() * 64);
		for (size_t i = 0; i < sections.size(); i++) {
			auto const& s = sections[i];
			auto p = table.data() + i * 64;
			memcpy(p, &s.Name, 4); memcpy(p + 4, &s.Type, 4); memcpy(p + 8, &s.Flags, 8); memcpy(p + 16, &s.Address, 8);
			memcpy(p + 24, &s.Offset, 8); memcpy(p + 32, &s.Size, 8); memcpy(p + 40, &s.Link, 4); memcpy(p + 44, &s.Info, 4);
			memcpy(p + 48, &s.Align, 8); memcpy(p + 56, &s.EntrySize, 8);
		}
		auto shoff = b.Add(table, 8);

		// the header
		Bytes ident{ 0x7F, 'E', 'L', 'F', 2, 1, 1, 3, 0 };
		std::copy(ident.begin(), ident.end(), b.Data.begin());
		b.Put<uint16_t>(16, 2);					// ET_EXEC
		b.Put<uint16_t>(18, 62);				// x86-64
		b.Put<uint32_t>(20, 1);
		b.Put<uint64_t>(24, Base + text);		// the entry point
		b.Put<uint64_t>(32, 64);				// the program headers
		b.Put<uint64_t>(40, shoff);
		b.Put<uint16_t>(52, 64);
		b.Put<uint16_t>(54, 56);
		b.Put<uint16_t>(56, 4);
		b.Put<uint16_t>(58, 64);
		b.Put<uint16_t>(60, (uint16_t)sections.size());
		b.Put<uint16_t>(62, (uint16_t)sections.size() - 1);	// .shstrtab

		// the program headers: INTERP, LOAD (the whole file), DYNAMIC, NOTE
		auto ph = [&](int i, uint32_t type, uint32_t flags, uint64_t offset, uint64_t size, uint64_t align) {
			auto at = 64 + i * 56;
			b.Put<uint32_t>(at, type);
			b.Put<uint32_t>(at + 4, flags);
			b.Put<uint64_t>(at + 8, offset);
			b.Put<uint64_t>(at + 16, Base + offset);
			b.Put<uint64_t>(at + 24, Base + offset);
			b.Put<uint64_t>(at + 32, size);
			b.Put<uint64_t>(at + 40, size);
			b.Put<uint64_t>(at + 48, align);
		};
		ph(0, 3, 4, interp, 28, 1);
		ph(1, 1, 5, 0, fileSize, 0x1000);
		ph(2, 2, 6, dynamicAt, dynamic.size(), 8);
		ph(3, 4, 4, noteAt, note.size(), 4);
		return b.Data;
	}

	Bytes ReadFile(std::filesystem::path const& path) {
		std::ifstream in(path, std::ios::binary);
		return Bytes(std::istreambuf_iterator<char>(in), {});
	}
}

TEST_CASE("A 64-bit ELF executable", "[elf]") {
	auto data = BuildElf64();
	REQUIRE(ElfFile::IsElf(AsBytes(data)));
	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(data)));
	CHECK(elf.Problems().empty());
	CHECK(elf.Is64());
	CHECK_FALSE(elf.BigEndian());
	CHECK(elf.Type() == 2);
	CHECK(ElfFile::TypeName(elf.Type()) == L"Executable");
	CHECK(ElfFile::MachineName(elf.Machine()) == L"x86-64");
	CHECK(ElfFile::OsAbiName(elf.OsAbi()) == L"Linux (GNU)");
	CHECK(elf.Interpreter() == "/lib64/ld-linux-x86-64.so.2");

	SECTION("program headers") {
		auto const& ph = elf.ProgramHeaders();
		REQUIRE(ph.size() == 4);
		CHECK(ElfFile::SegmentTypeName(ph[1].Type) == L"LOAD");
		CHECK(ElfFile::SegmentFlagsToString(ph[1].Flags) == L"R-X");
		CHECK(elf.OffsetOfAddress(elf.Entry()) == (int64_t)(elf.Entry() - Base));
		CHECK(elf.OffsetOfAddress(0x10) == -1);
	}
	SECTION("sections") {
		auto const& s = elf.Sections();
		REQUIRE(s.size() == 11);
		CHECK(s[2].Name == ".text");
		CHECK(ElfFile::SectionFlagsToString(s[2].Flags) == L"ALLOC EXEC");
		CHECK(elf.SectionData(2).size() == 5);
		CHECK(elf.SectionData(9).empty());		// .bss
		CHECK(ElfFile::SectionTypeName(s[7].Type) == L"RELR");
		CHECK(elf.SectionOfAddress(elf.Entry()) == 2);
	}
	SECTION("symbols") {
		auto const& syms = elf.Symbols();
		REQUIRE(syms.size() == 2);
		CHECK(syms[0].Name == "main");
		CHECK(syms[0].Dynamic);
		CHECK(syms[0].SectionIndex == 2);
		CHECK(ElfFile::SymbolTypeName(syms[0].Type) == L"FUNC");
		CHECK(ElfFile::SymbolBindName(syms[0].Bind) == L"GLOBAL");
		CHECK(syms[1].Name == "puts");
		CHECK(syms[1].SectionIndex == 0);
	}
	SECTION("dynamic") {
		REQUIRE(elf.Dynamic().size() == 3);
		CHECK(elf.NeededLibraries() == std::vector<std::string>{ "libc.so.6", "libfoo.so" });
		CHECK(ElfFile::DynamicTagName(elf.Dynamic()[2].Tag) == L"STRTAB");
	}
	SECTION("relocations") {
		auto const& r = elf.Relocations();
		REQUIRE(r.size() == 4);
		CHECK(r[0].Offset == Base + 0x1000);
		CHECK(r[0].SymbolName == "puts");
		CHECK(ElfFile::RelocationTypeName(elf.Machine(), r[0].Type) == L"R_X86_64_JUMP_SLOT");
		CHECK(r[0].HasAddend);
		// the packed ones: the address, then the words that the bitmap has
		CHECK(r[1].Relr);
		CHECK(r[1].Offset == Base + 0x2000);
		CHECK(r[2].Offset == Base + 0x2008);
		CHECK(r[3].Offset == Base + 0x2018);
	}
	SECTION("notes") {
		REQUIRE(elf.Notes().size() == 1);
		CHECK(elf.Notes()[0].Owner == "GNU");
		CHECK(ElfFile::NoteTypeName("GNU", 3) == L"NT_GNU_BUILD_ID");
		CHECK(elf.BuildId() == "0102030405060708");
		CHECK(elf.NoteDescription(elf.Notes()[0]) == L"0102030405060708");
	}
}

TEST_CASE("A 32-bit big-endian relocatable object", "[elf]") {
	Builder b;
	b.Big = true;
	b.Data.resize(52);
	StringTable strtab;
	auto func = strtab.Add("func");
	auto strAt = b.Add(strtab.Data);
	Bytes symtab(2 * 16);
	auto symAt = b.Add(symtab, 4);
	b.Put<uint32_t>(symAt + 16, func);
	b.Put<uint32_t>(symAt + 20, 0x1234);	// value
	b.Put<uint32_t>(symAt + 24, 0x10);		// size
	b.Data[symAt + 28] = 0x12;				// GLOBAL FUNC
	b.Put<uint16_t>(symAt + 30, 0xFFF1);	// SHN_ABS
	StringTable shstr;
	auto symtabName = shstr.Add(".symtab"), strtabName = shstr.Add(".strtab"), shstrName = shstr.Add(".shstrtab");
	auto shstrAt = b.Add(shstr.Data);
	auto shoff = b.Add(Bytes(4 * 40), 4);
	auto section = [&](int i, uint32_t name, uint32_t type, uint32_t offset, uint32_t size, uint32_t link, uint32_t entsize) {
		auto at = shoff + i * 40;
		b.Put<uint32_t>(at, name);
		b.Put<uint32_t>(at + 4, type);
		b.Put<uint32_t>(at + 16, offset);
		b.Put<uint32_t>(at + 20, size);
		b.Put<uint32_t>(at + 24, link);
		b.Put<uint32_t>(at + 36, entsize);
	};
	section(1, symtabName, 2, (uint32_t)symAt, (uint32_t)symtab.size(), 2, 16);
	section(2, strtabName, 3, (uint32_t)strAt, (uint32_t)strtab.Data.size(), 0, 0);
	section(3, shstrName, 3, (uint32_t)shstrAt, (uint32_t)shstr.Data.size(), 0, 0);

	Bytes ident{ 0x7F, 'E', 'L', 'F', 1, 2, 1 };
	std::copy(ident.begin(), ident.end(), b.Data.begin());
	b.Put<uint16_t>(16, 1);				// ET_REL
	b.Put<uint16_t>(18, 20);			// PowerPC
	b.Put<uint32_t>(20, 1);
	b.Put<uint32_t>(32, (uint32_t)shoff);
	b.Put<uint16_t>(40, 52);
	b.Put<uint16_t>(46, 40);
	b.Put<uint16_t>(48, 4);
	b.Put<uint16_t>(50, 3);

	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(b.Data)));
	CHECK(elf.Problems().empty());
	CHECK_FALSE(elf.Is64());
	CHECK(elf.BigEndian());
	CHECK(ElfFile::TypeName(elf.Type()) == L"Relocatable");
	CHECK(ElfFile::MachineName(elf.Machine()) == L"PowerPC");
	REQUIRE(elf.Sections().size() == 4);
	CHECK(elf.Sections()[1].Name == ".symtab");
	REQUIRE(elf.Symbols().size() == 1);
	auto const& s = elf.Symbols()[0];
	CHECK(s.Name == "func");
	CHECK(s.Value == 0x1234);
	CHECK(s.Size == 0x10);
	CHECK(s.SectionIndex == 0xFFF1);
	CHECK_FALSE(s.Dynamic);
	CHECK(elf.ProgramHeaders().empty());
}

TEST_CASE("What is not an ELF file", "[elf]") {
	CHECK_FALSE(ElfFile::IsElf({}));
	Bytes text(100, 'a');
	CHECK_FALSE(ElfFile::IsElf(AsBytes(text)));
	auto dll = ReadFile(L"C:\\Windows\\System32\\version.dll");
	CHECK_FALSE(ElfFile::IsElf(AsBytes(dll)));
	CHECK_FALSE(ElfFile::IsElfFile(L"C:\\Windows\\System32\\version.dll"));

	SECTION("a header that is cut short") {
		auto data = BuildElf64();
		data.resize(40);
		CHECK_FALSE(ElfFile::IsElf(AsBytes(data)));
	}
}

TEST_CASE("A damaged ELF file is read as far as it goes", "[elf]") {
	auto data = BuildElf64();
	uint64_t outside = 0x7FFFFFFF;
	memcpy(data.data() + 40, &outside, 8);	// the section headers
	ElfFile elf;
	REQUIRE(elf.Parse(AsBytes(data)));
	CHECK(elf.Sections().empty());
	CHECK(elf.Problems().size() == 1);
	// the segments are still there, and the dynamic section through them
	CHECK(elf.ProgramHeaders().size() == 4);
	CHECK(elf.Interpreter() == "/lib64/ld-linux-x86-64.so.2");
	CHECK(elf.NeededLibraries() == std::vector<std::string>{ "libc.so.6", "libfoo.so" });
	CHECK(elf.BuildId() == "0102030405060708");		// from PT_NOTE
}

TEST_CASE("Real ELF files", "[elf][system]") {
	int files = 0;
	for (auto name : { L"C:\\Program Files\\WSL\\tools\\init", L"C:\\Program Files\\WSL\\tools\\bsdtar" }) {
		if (!std::filesystem::exists(name))
			continue;
		INFO(Narrow(name));
		REQUIRE(ElfFile::IsElfFile(name));
		ElfFile elf;
		REQUIRE(elf.Open(name));
		std::wstring problems;
		for (auto const& p : elf.Problems())
			problems += p + L"; ";
		INFO(Narrow(problems));
		CHECK(elf.Problems().empty());
		CHECK_FALSE(elf.ProgramHeaders().empty());
		CHECK_FALSE(elf.Sections().empty());
		CHECK(elf.OffsetOfAddress(elf.Entry()) >= 0);
		CHECK(elf.Sections()[elf.SectionOfAddress(elf.Entry())].Name == ".text");
		files++;
	}
	if (files == 0)
		SKIP("WSL is not installed");
}
