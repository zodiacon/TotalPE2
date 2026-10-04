#include "TestCommon.h"
#include "ByteWriter.h"
#include <CoffObject.h>
#include <CodeViewInfo.h>
#include <LibArchive.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

namespace {
	using Bytes = std::vector<uint8_t>;

	struct TestSection {
		std::string Name;
		Bytes Data;
		uint32_t Characteristics{};
		std::vector<std::tuple<uint32_t, uint32_t, uint16_t>> Relocations;	// offset, symbol index, type
		std::vector<std::pair<uint32_t, uint16_t>> Lines;					// symbol index or offset, line
	};

	struct TestSymbol {
		std::string Name;		// up to 8 characters
		uint32_t Value{};
		int16_t Section{};
		uint8_t StorageClass{ IMAGE_SYM_CLASS_EXTERNAL };
		uint16_t Type{};
	};

	void Name8(ByteWriter& w, std::string_view name) {
		for (size_t i = 0; i < 8; i++)
			w.U8(i < name.size() ? (uint8_t)name[i] : 0);
	}

	// An x64 object: the sections, their relocations and line numbers, the symbols (no auxiliary records), an empty string table
	Bytes BuildObject(std::vector<TestSection> const& sections, std::vector<TestSymbol> const& symbols) {
		size_t at = 20 + sections.size() * 40;
		std::vector<size_t> data, relocs, lines;
		for (auto const& s : sections) {
			data.push_back(at);
			at += s.Data.size();
			relocs.push_back(at);
			at += s.Relocations.size() * 10;
			lines.push_back(at);
			at += s.Lines.size() * 6;
		}
		ByteWriter w;
		w.U16(0x8664).U16((uint16_t)sections.size()).U32(0).U32((uint32_t)at).U32((uint32_t)symbols.size()).U16(0).U16(0);
		for (size_t i = 0; i < sections.size(); i++) {
			auto const& s = sections[i];
			Name8(w, s.Name);
			w.U32(0).U32(0).U32((uint32_t)s.Data.size()).U32((uint32_t)data[i]);
			w.U32(s.Relocations.empty() ? 0 : (uint32_t)relocs[i]).U32(s.Lines.empty() ? 0 : (uint32_t)lines[i]);
			w.U16((uint16_t)s.Relocations.size()).U16((uint16_t)s.Lines.size()).U32(s.Characteristics);
		}
		for (auto const& s : sections) {
			w.Bytes.insert(w.Bytes.end(), s.Data.begin(), s.Data.end());
			for (auto [offset, symbol, type] : s.Relocations)
				w.U32(offset).U32(symbol).U16(type);
			for (auto [value, line] : s.Lines)
				w.U32(value).U16(line);
		}
		for (auto const& sym : symbols) {
			Name8(w, sym.Name);
			w.U32(sym.Value).U16((uint16_t)sym.Section).U16(sym.Type).U8(sym.StorageClass).U8(0);
		}
		w.U32(4);
		return w.Bytes;
	}

	void Chars(ByteWriter& w, std::string_view s, bool zero = true) {
		for (char c : s)
			w.U8((uint8_t)c);
		if (zero)
			w.U8(0);
	}

	// a type record: its length, its leaf, its data, padded to 4 bytes (with LF_PAD bytes)
	void TypeRecord(ByteWriter& w, uint16_t leaf, ByteWriter const& data) {
		auto size = 2 + data.Bytes.size();
		auto padded = (size + 2 + 3) / 4 * 4 - 2;
		w.U16((uint16_t)padded).U16(leaf);
		w.Bytes.insert(w.Bytes.end(), data.Bytes.begin(), data.Bytes.end());
		for (auto i = size; i < padded; i++)
			w.U8((uint8_t)(0xF0 + padded - i));
	}

	// a symbol record; returns the offset of its body (after the length and the kind) in the writer
	size_t SymbolRecord(ByteWriter& w, uint16_t kind, ByteWriter const& data) {
		w.U16((uint16_t)(2 + data.Bytes.size())).U16(kind);
		auto body = w.Bytes.size();
		w.Bytes.insert(w.Bytes.end(), data.Bytes.begin(), data.Bytes.end());
		return body;
	}

	// a subsection of .debug$S: its type, its size, its data, padded to 4 bytes; returns the offset of the data
	size_t Subsection(ByteWriter& w, uint32_t type, ByteWriter const& data) {
		w.U32(type).U32((uint32_t)data.Bytes.size());
		auto at = w.Bytes.size();
		w.Bytes.insert(w.Bytes.end(), data.Bytes.begin(), data.Bytes.end());
		w.Align(4);
		return at;
	}

	std::span<const std::byte> AsBytes(Bytes const& b) {
		return std::as_bytes(std::span(b));
	}

	constexpr uint16_t SecRel = 0x000B;		// IMAGE_REL_AMD64_SECREL
	constexpr uint32_t DebugCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_READ;

	// int f(Point* p) at the start of .text: its types, its symbols (the procedure, a local variable), two lines in t.cpp
	Bytes BuildDebugObject() {
		ByteWriter types;
		types.U32(4);
		TypeRecord(types, 0x1201, ByteWriter().U32(1).U32(0x1004));										// 0x1000 LF_ARGLIST (Point*)
		TypeRecord(types, 0x1008, ByteWriter().U32(0x74).U8(0).U8(0).U16(1).U32(0x1000));				// 0x1001 LF_PROCEDURE int(Point*)
		ByteWriter fields;
		fields.U16(0x150D).U16(3).U32(0x74).U16(0);
		Chars(fields, "x");
		fields.U8(0xF2).U8(0xF1);
		fields.U16(0x150D).U16(3).U32(0x74).U16(4);
		Chars(fields, "y");
		TypeRecord(types, 0x1203, fields);																// 0x1002 LF_FIELDLIST
		ByteWriter point;
		point.U16(2).U16(0).U32(0x1002).U32(0).U32(0).U16(8);
		Chars(point, "Point");
		TypeRecord(types, 0x1505, point);																// 0x1003 LF_STRUCTURE
		TypeRecord(types, 0x1002, ByteWriter().U32(0x1003).U32((8 << 13) | 0x0C));						// 0x1004 LF_POINTER Point*
		ByteWriter funcId;
		funcId.U32(0).U32(0x1001);
		Chars(funcId, "f");
		TypeRecord(types, 0x1601, funcId);																// 0x1005 LF_FUNC_ID

		ByteWriter debug;
		debug.U32(4);
		ByteWriter syms;
		ByteWriter objName;
		objName.U32(0);
		Chars(objName, "t.obj");
		SymbolRecord(syms, 0x1101, objName);
		ByteWriter proc;
		proc.U32(0).U32(0).U32(0).U32(4).U32(0).U32(3).U32(0x1005).U32(0).U16(0).U8(0);
		Chars(proc, "f");
		auto procBody = SymbolRecord(syms, 0x1147, proc);		// S_GPROC32_ID
		ByteWriter local;
		local.U32(8).U32(0x1004).U16(335);
		Chars(local, "p");
		SymbolRecord(syms, 0x1111, local);						// S_REGREL32 [rsp+8]
		SymbolRecord(syms, 0x114F, ByteWriter());				// S_PROC_ID_END
		auto symsAt = Subsection(debug, 0xF1, syms);

		ByteWriter strings;
		strings.U8(0);
		Chars(strings, "t.cpp");
		Subsection(debug, 0xF3, strings);
		ByteWriter files;
		files.U32(1).U8(0).U8(0);
		Subsection(debug, 0xF4, files);

		ByteWriter lines;
		lines.U32(0).U16(0).U16(0).U32(4);
		lines.U32(0).U32(2).U32(12 + 16);
		lines.U32(0).U32(10 | 0x80000000);
		lines.U32(2).U32(11 | 0x80000000);
		auto linesAt = Subsection(debug, 0xF2, lines);

		TestSection text{ ".text", { 0x8B, 0x01, 0xC3, 0x90 }, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ };
		TestSection debugS{ ".debug$S", debug.Bytes, DebugCharacteristics };
		// the address of the procedure (its offset field) and of the lines are relative to f
		debugS.Relocations.push_back({ (uint32_t)(symsAt + procBody + 28), 0, SecRel });
		debugS.Relocations.push_back({ (uint32_t)linesAt, 0, SecRel });
		TestSection debugT{ ".debug$T", types.Bytes, DebugCharacteristics };
		return BuildObject({ text, debugS, debugT }, { { "f", 0, 1, IMAGE_SYM_CLASS_EXTERNAL, 0x20 } });
	}
}

TEST_CASE("The CodeView information of an object", "[coff][codeview]") {
	CoffObject obj;
	auto data = BuildDebugObject();
	REQUIRE(obj.Parse(AsBytes(data)));
	REQUIRE(obj.Problems().empty());
	auto cv = ReadCodeView(obj);
	CHECK(cv.Problems.empty());
	CHECK_FALSE(cv.Empty());
	CHECK(cv.TypeServer.empty());

	SECTION("subsections") {
		REQUIRE(cv.Subsections.size() == 4);
		CHECK(cv.Subsections[0].Type == 0xF1);
		CHECK(cv.Subsections[0].Section == 1);
		CHECK(cv.Subsections[0].Offset == 4);
		CHECK(cv.Subsections[3].Type == 0xF2);
		CHECK(CodeViewInfo::SubsectionName(0xF4) == L"File Checksums");
		CHECK(CodeViewInfo::SubsectionName(0x800000F1) == L"Symbols (ignored)");
	}
	SECTION("types") {
		REQUIRE(cv.Types.size() == 6);
		CHECK(cv.Types[0].Index == 0x1000);
		CHECK(cv.Types[0].Name == L"(Point*)");
		CHECK(cv.Types[1].Name == L"int(Point*)");
		CHECK(cv.Types[1].Details == L"__cdecl, 1 parameters");
		CHECK(cv.Types[2].Kind == 0x1203);
		CHECK(cv.Types[2].Name.empty());
		CHECK(cv.Types[2].Details == L"2 fields: x: int (+0); y: int (+4)");
		CHECK(cv.Types[3].Name == L"Point");
		CHECK(cv.Types[3].Details == L"8 bytes, 2 members, fields 0x1002");
		CHECK(cv.Types[4].Name == L"Point*");
		CHECK(cv.Types[4].Details == L"Pointer to Point, 8 bytes");
		CHECK(cv.Types[5].Name == L"f");
		CHECK(cv.Types[5].Details == L"Type int(Point*)");
		CHECK(CodeViewInfo::TypeKindName(0x1505) == L"LF_STRUCTURE");
	}
	SECTION("symbols") {
		REQUIRE(cv.Symbols.size() == 4);
		CHECK(cv.Symbols[0].Name == L"t.obj");
		auto const& proc = cv.Symbols[1];
		CHECK(proc.Kind == 0x1147);
		CHECK(proc.Name == L"f");
		CHECK(proc.Details == L"4 bytes, int(Point*)");
		CHECK(proc.Depth == 0);
		CHECK(proc.Target.Section == 0);	// .text, through the relocation
		CHECK(proc.Target.Offset == 0);
		auto const& local = cv.Symbols[2];
		CHECK(local.Name == L"p");
		CHECK(local.Depth == 1);
		CHECK(local.Details == L"Point*, [rsp+0x8]");
		CHECK(local.Target.Section == -1);
		CHECK(cv.Symbols[3].Depth == 0);	// the end of the procedure
		CHECK(CodeViewInfo::SymbolKindName(0x1147) == L"S_GPROC32_ID");
	}
	SECTION("files and lines") {
		REQUIRE(cv.Files.size() == 1);
		CHECK(cv.Files[0].Name == L"t.cpp");
		CHECK(cv.Files[0].Id == 0);
		REQUIRE(cv.Lines.size() == 2);
		CHECK(cv.Lines[0].Function == L"f");
		CHECK(cv.Lines[0].File == L"t.cpp");
		CHECK(cv.Lines[0].Line == 10);
		CHECK(cv.Lines[0].Statement);
		CHECK(cv.Lines[1].Line == 11);
		CHECK(cv.Lines[1].CodeOffset == 2);
		CHECK(cv.Lines[1].Target.Section == 0);
		CHECK(cv.Lines[1].Target.Offset == 2);
	}
}

TEST_CASE("An object without debug information has no CodeView information", "[coff][codeview]") {
	CoffObject obj;
	auto data = BuildObject({ { ".text", { 0xC3 }, IMAGE_SCN_CNT_CODE } }, { { "f", 0, 1 } });
	REQUIRE(obj.Parse(AsBytes(data)));
	auto cv = ReadCodeView(obj);
	CHECK(cv.Empty());
	CHECK(cv.Problems.empty());
}

TEST_CASE("Damaged CodeView information is read as far as it goes", "[coff][codeview]") {
	auto data = BuildDebugObject();
	CoffObject obj;
	REQUIRE(obj.Parse(AsBytes(data)));
	auto debugS = obj.Sections()[1];
	auto debugT = obj.Sections()[2];

	SECTION("a subsection larger than its section") {
		auto bad = data;
		uint32_t huge = 0x100000;
		memcpy(bad.data() + debugS.PointerToRawData + 8, &huge, 4);
		CoffObject damaged;
		REQUIRE(damaged.Parse(AsBytes(bad)));
		auto cv = ReadCodeView(damaged);
		CHECK(cv.Subsections.empty());
		CHECK(cv.Problems.size() == 1);
		CHECK(cv.Types.size() == 6);
	}
	SECTION("a type record larger than its section") {
		auto bad = data;
		uint16_t huge = 0xFFF0;
		memcpy(bad.data() + debugT.PointerToRawData + 4 + 12, &huge, 2);	// the second record
		CoffObject damaged;
		REQUIRE(damaged.Parse(AsBytes(bad)));
		auto cv = ReadCodeView(damaged);
		CHECK(cv.Types.size() == 1);
		CHECK(cv.Problems.size() == 1);
		// the names refer to types that are not there
		CHECK(cv.Symbols[1].Details == L"4 bytes, 0x1005");
	}
	SECTION("an unknown signature") {
		auto bad = data;
		bad[debugS.PointerToRawData] = 2;
		CoffObject damaged;
		REQUIRE(damaged.Parse(AsBytes(bad)));
		auto cv = ReadCodeView(damaged);
		CHECK(cv.Subsections.empty());
		CHECK(cv.Problems.size() == 1);
	}
	SECTION("every truncation") {
		// no crash, whatever the size of the sections
		for (uint32_t size = 0; size < debugS.SizeOfRawData; size++) {
			auto bad = data;
			memcpy(bad.data() + 20 + 40 + 16, &size, 4);
			CoffObject damaged;
			if (damaged.Parse(AsBytes(bad)))
				(void)ReadCodeView(damaged);
		}
	}
}

TEST_CASE("COFF line numbers", "[coff]") {
	TestSection text{ ".text", { 0x90, 0x90, 0x90, 0xC3 }, IMAGE_SCN_CNT_CODE };
	text.Lines = { { 0, 0 }, { 1, 2 }, { 3, 3 } };	// f (symbol 0), then lines relative to its first line
	auto data = BuildObject({ text }, { { "f", 0, 1 } });
	CoffObject obj;
	REQUIRE(obj.Parse(AsBytes(data)));
	CHECK(obj.Problems().empty());
	auto const& lines = obj.LineNumbers();
	REQUIRE(lines.size() == 3);
	CHECK(lines[0].Section == 0);
	CHECK(lines[0].Line == 0);
	CHECK(lines[0].SymbolIndexOrOffset == 0);
	CHECK(lines[2].Line == 3);
	CHECK(lines[2].SymbolIndexOrOffset == 3);
}

// The static CRT is built with debug information: hundreds of objects, written by the compiler and the assembler
TEST_CASE("The CodeView information of the objects of the CRT", "[codeview][system]") {
	std::filesystem::path lib;
	std::error_code ec;
	for (auto const& vs : std::filesystem::directory_iterator(L"C:\\Program Files\\Microsoft Visual Studio", ec))
		for (auto const& edition : std::filesystem::directory_iterator(vs.path(), ec))
			for (auto const& tools : std::filesystem::directory_iterator(edition.path() / L"VC" / L"Tools" / L"MSVC", ec))
				if (auto candidate = tools.path() / L"lib" / L"x64" / L"libcmt.lib"; std::filesystem::exists(candidate, ec))
					lib = candidate;
	if (lib.empty())
		SKIP("Visual C++ is not installed");

	LibArchive archive;
	REQUIRE(archive.Open(lib.wstring()));
	size_t objects = 0, withSymbols = 0, lines = 0, types = 0;
	for (size_t i = 0; i < archive.Members().size(); i++) {
		if (archive.Members()[i].Kind != ArchiveMemberKind::Object)
			continue;
		CoffObject obj;
		INFO(archive.Members()[i].Name);
		REQUIRE(obj.Parse(archive.MemberData(i)));
		auto cv = ReadCodeView(obj);
		CHECK(cv.Problems.empty());
		objects++;
		withSymbols += !cv.Symbols.empty();
		lines += cv.Lines.size();
		types += cv.Types.size();
		// every line is in a function that has a place in the code
		for (auto const& line : cv.Lines)
			CHECK(line.Target.Section >= 0);
	}
	CHECK(objects > 100);
	CHECK(withSymbols > objects / 2);
	CHECK(lines > 1000);
	CHECK(types > 0);
}

// An object to look at: TOTALPE_CV_OBJ=<path> PECore.Tests "[.cvdump]"
TEST_CASE("Dump the CodeView information of an object", "[.cvdump]") {
	char path[MAX_PATH];
	if (::GetEnvironmentVariableA("TOTALPE_CV_OBJ", path, MAX_PATH) == 0)
		SKIP("TOTALPE_CV_OBJ is not set");
	std::ifstream in(path, std::ios::binary);
	Bytes data((std::istreambuf_iterator<char>(in)), {});
	CoffObject obj;
	REQUIRE(obj.Parse(AsBytes(data)));
	auto cv = ReadCodeView(obj);
	printf("compiler: %s\ntype server: %s\n", Narrow(cv.Compiler).c_str(), Narrow(cv.TypeServer).c_str());
	for (auto const& p : cv.Problems)
		printf("problem: %s\n", Narrow(p).c_str());
	for (auto const& s : cv.Subsections)
		printf("sub %u 0x%X %s %u\n", s.Section, s.Offset, Narrow(CodeViewInfo::SubsectionName(s.Type)).c_str(), s.Size);
	for (auto const& s : cv.Symbols)
		printf("sym %*s%s %s | %s | %d:0x%X\n", s.Depth * 2, "", Narrow(CodeViewInfo::SymbolKindName(s.Kind)).c_str(), Narrow(s.Name).c_str(),
			Narrow(s.Details).c_str(), s.Target.Section, s.Target.Offset);
	for (auto const& f : cv.Files)
		printf("file 0x%X %s %s %s\n", f.Id, Narrow(f.Name).c_str(), Narrow(CodeViewInfo::ChecksumKindName(f.ChecksumKind)).c_str(), Narrow(f.Checksum).c_str());
	for (auto const& l : cv.Lines)
		printf("line %s+0x%X %s(%u-%u) %d:0x%X\n", Narrow(l.Function).c_str(), l.CodeOffset, Narrow(l.File).c_str(), l.Line, l.EndLine, l.Target.Section, l.Target.Offset);
	for (auto const& t : cv.Types)
		printf("type 0x%X %s %s | %s\n", t.Index, Narrow(CodeViewInfo::TypeKindName(t.Kind)).c_str(), Narrow(t.Name).c_str(), Narrow(t.Details).c_str());
}
