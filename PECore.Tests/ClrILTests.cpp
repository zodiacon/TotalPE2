#include "TestCommon.h"
#include <ClrMetadata.h>
#include <ClrIL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {
	std::vector<std::byte> Bytes(std::initializer_list<int> values) {
		std::vector<std::byte> bytes;
		for (auto v : values)
			bytes.push_back((std::byte)v);
		return bytes;
	}

	// mscorlib of the .NET Framework: thousands of types and methods, IL of every kind
	bool OpenMscorlib(PEFile& pe) {
		WCHAR windows[MAX_PATH];
		::GetWindowsDirectoryW(windows, MAX_PATH);
		std::wstring path = std::wstring(windows) + L"\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
		if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			return false;
		return pe.Open(path);
	}

	uint32_t FindType(ClrMetadata const& md, std::wstring_view ns, std::wstring_view name) {
		auto& types = md.Types();
		for (uint32_t i = 0; i < types.size(); i++)
			if (types[i].Namespace == ns && types[i].Name == name && types[i].Enclosing == 0)
				return i + 1;
		return 0;
	}

	uint32_t FindMethod(ClrMetadata const& md, uint32_t type, std::wstring_view name) {
		for (auto m : md.MethodsOf(type))
			if (md.Methods()[m - 1].Name == name)
				return m;
		return 0;
	}
}

TEST_CASE("IL instructions are decoded", "[clr][il]") {
	std::vector<IlInstruction> code;
	SECTION("operands of every size") {
		auto bytes = Bytes({
			0x00,							// nop
			0x1F, 0xFE,						// ldc.i4.s -2
			0x20, 0x78, 0x56, 0x34, 0x12,	// ldc.i4 0x12345678
			0x21, 1, 0, 0, 0, 0, 0, 0, 0x80,	// ldc.i8 0x8000000000000001
			0x22, 0x00, 0x00, 0xC0, 0x3F,	// ldc.r4 1.5
			0x11, 0x05,						// ldloc.s 5
			0xFE, 0x0C, 0x34, 0x12,			// ldloc 0x1234
			0x72, 0x01, 0x00, 0x00, 0x70,	// ldstr
			0x28, 0x02, 0x00, 0x00, 0x0A,	// call (MemberRef 2)
			0xFE, 0x01,						// ceq
			0x2A,							// ret
		});
		REQUIRE(DecodeIl(bytes, code));
		REQUIRE(code.size() == 11);
		CHECK(code[0].Opcode == 0x00);
		CHECK(code[1].Value == -2);
		CHECK(code[2].Value == 0x12345678);
		CHECK((uint64_t)code[3].Value == 0x8000000000000001ULL);
		CHECK(code[4].Real == 1.5);
		CHECK(code[5].Operand == IlOperand::Local8);
		CHECK(code[5].Value == 5);
		CHECK(code[6].Opcode == 0xFE0C);
		CHECK(code[6].Value == 0x1234);
		CHECK(code[6].Offset == 24);
		CHECK(code[6].Size == 4);
		CHECK(code[7].Operand == IlOperand::String);
		CHECK(code[7].Value == 0x70000001);
		CHECK(code[8].Value == 0x0A000002);
		CHECK(std::wstring(GetIlOpcode(code[9].Opcode)->Name) == L"ceq");
		CHECK(std::wstring(GetIlOpcode(code[10].Opcode)->Name) == L"ret");
	}
	SECTION("branches are relative to the next instruction") {
		auto bytes = Bytes({
			0x2B, 0x05,						// br.s +5 -> 7
			0x38, 0xFB, 0xFF, 0xFF, 0xFF,	// br -5 -> 2
			0x45, 2, 0, 0, 0, 1, 0, 0, 0, 0xF0, 0xFF, 0xFF, 0xFF,	// switch (+1, -16), next = 20
			0x2A,
		});
		REQUIRE(DecodeIl(bytes, code));
		REQUIRE(code.size() == 4);
		CHECK(code[0].Value == 7);
		CHECK(code[1].Value == 2);
		REQUIRE(code[2].Targets.size() == 2);
		CHECK(code[2].Targets[0] == 21);
		CHECK(code[2].Targets[1] == 4);
		CHECK(code[3].Offset == 20);
	}
	SECTION("an opcode that does not exist stops the decoding") {
		auto bytes = Bytes({ 0x00, 0x24, 0x00 });
		CHECK_FALSE(DecodeIl(bytes, code));
		CHECK(code.size() == 1);
	}
	SECTION("an operand cut short stops the decoding") {
		auto bytes = Bytes({ 0x00, 0x20, 0x01, 0x02 });
		CHECK_FALSE(DecodeIl(bytes, code));
		CHECK(code.size() == 1);
	}
	SECTION("a switch with more targets than bytes") {
		auto bytes = Bytes({ 0x45, 0xFF, 0xFF, 0xFF, 0x7F, 0, 0, 0, 0 });
		CHECK_FALSE(DecodeIl(bytes, code));
	}
}

TEST_CASE("The opcode table", "[clr][il]") {
	auto name = [](uint16_t op) { auto info = GetIlOpcode(op); return info ? std::wstring(info->Name) : std::wstring(); };
	CHECK(name(0x00) == L"nop");
	CHECK(name(0x23) == L"ldc.r8");
	CHECK(name(0x24).empty());
	CHECK(name(0x45) == L"switch");
	CHECK(name(0x73) == L"newobj");
	CHECK(name(0x8C) == L"box");
	CHECK(name(0xA5) == L"unbox.any");
	CHECK(name(0xA6).empty());
	CHECK(name(0xB3) == L"conv.ovf.i1");
	CHECK(name(0xC2) == L"refanyval");
	CHECK(name(0xD0) == L"ldtoken");
	CHECK(name(0xDD) == L"leave");
	CHECK(name(0xE0) == L"conv.u");
	CHECK(name(0xE1).empty());
	CHECK(name(0xFE06) == L"ldftn");
	CHECK(name(0xFE08).empty());
	CHECK(name(0xFE16) == L"constrained.");
	CHECK(name(0xFE1C) == L"sizeof");
	CHECK(name(0xFE1E) == L"readonly.");
	CHECK(name(0xFE1F).empty());
}

TEST_CASE("IL strings are quoted", "[clr][il]") {
	CHECK(IlQuote(L"Hello") == L"\"Hello\"");
	CHECK(IlQuote(L"a\"b\\c\r\n\t") == L"\"a\\\"b\\\\c\\r\\n\\t\"");
	CHECK(IlQuote(std::wstring(L"\x01z", 2)) == L"\"\\u0001z\"");
}

TEST_CASE("The types and methods of mscorlib are disassembled", "[clr][il][system]") {
	PEFile pe;
	if (!OpenMscorlib(pe))
		SKIP("the .NET Framework is not installed");
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	REQUIRE(md.Types().size() > 1000);
	REQUIRE(md.Methods().size() > 10000);

	SECTION("types") {
		auto object = FindType(md, L"System", L"Object");
		REQUIRE(object);
		CHECK(md.Types()[object - 1].Extends == 0);
		CHECK(md.TypeDeclaration(object).starts_with(L".class public auto ansi serializable"));
		CHECK(md.TypeDeclaration(object).ends_with(L"System.Object"));

		auto string = FindType(md, L"System", L"String");
		REQUIRE(string);
		auto decl = md.TypeDeclaration(string);
		INFO(Narrow(decl));
		CHECK(decl.find(L"sealed") != std::wstring::npos);
		CHECK(decl.find(L" extends System.Object") != std::wstring::npos);
		CHECK(decl.find(L"System.IComparable") != std::wstring::npos);
		CHECK(md.TypeKind(string) == L"class");
		CHECK(md.TypeKind(FindType(md, L"System", L"Int32")) == L"struct");
		CHECK(md.TypeKind(FindType(md, L"System", L"DayOfWeek")) == L"enum");
		CHECK(md.TypeKind(FindType(md, L"System", L"IDisposable")) == L"interface");
		CHECK(md.TypeKind(FindType(md, L"System", L"Action")) == L"delegate");

		auto list = FindType(md, L"System.Collections.Generic", L"List`1");
		REQUIRE(list);
		CHECK(md.TypeDeclaration(list).find(L"System.Collections.Generic.List`1<T>") != std::wstring::npos);
		// a nested type: List<T>.Enumerator
		auto nested = md.NestedTypes(list);
		auto it = std::find_if(nested.begin(), nested.end(), [&](uint32_t t) { return md.Types()[t - 1].Name == L"Enumerator"; });
		REQUIRE(it != nested.end());
		CHECK(md.FullName(*it) == L"System.Collections.Generic.List`1/Enumerator");
	}

	SECTION("a method") {
		auto string = FindType(md, L"System", L"String");
		auto method = FindMethod(md, string, L"IsNullOrEmpty");
		REQUIRE(method);
		CHECK(md.MethodSignature(method) == L"bool IsNullOrEmpty(string value)");
		CHECK(md.MethodDeclaration(method) == L".method public hidebysig static bool IsNullOrEmpty(string value) cil managed");
		auto text = DisassembleIlMethod(pe, md, method);
		INFO(Narrow(text));
		CHECK(text.find(L".maxstack") != std::wstring::npos);
		CHECK(text.find(L"IL_0000:  ldarg.0") != std::wstring::npos);
		CHECK(text.find(L"ret") != std::wstring::npos);
		CHECK(text.find(L"(token") == std::wstring::npos);
	}

	SECTION("generic methods, instance methods and fields") {
		auto list = FindType(md, L"System.Collections.Generic", L"List`1");
		auto add = FindMethod(md, list, L"Add");
		REQUIRE(add);
		CHECK(md.MethodSignature(add) == L"instance void Add(T item)");
		auto convert = FindMethod(md, list, L"ConvertAll");
		REQUIRE(convert);
		CHECK(md.MethodSignature(convert) ==
			L"instance class System.Collections.Generic.List`1<TOutput> ConvertAll<TOutput>(class System.Converter`2<T, TOutput> converter)");
		auto fields = md.FieldsOf(list);
		REQUIRE(!fields.empty());
		auto items = std::find_if(fields.begin(), fields.end(), [&](uint32_t f) { return md.Fields()[f - 1].Name == L"_items"; });
		REQUIRE(items != fields.end());
		CHECK(md.FieldDeclaration(*items) == L".field private T[] _items");
		auto text = DisassembleIlMethod(pe, md, add);
		INFO(Narrow(text));
		CHECK(text.find(L"ldfld      !0[] class System.Collections.Generic.List`1<!0>::_items") != std::wstring::npos);
	}

	SECTION("try blocks and handlers") {
		// a method with a finally: Monitor.Enter/Exit in a lock
		size_t withClauses = 0, checkedMethods = 0;
		for (uint32_t m = 1; m <= md.Methods().size() && checkedMethods < 2000; m++) {
			IlMethodBody body;
			if (!ReadIlMethodBody(pe, md.Methods()[m - 1].Rva, body) || body.Clauses.empty())
				continue;
			checkedMethods++;
			withClauses++;
			auto text = DisassembleIlMethod(pe, md, m);
			CHECK(text.find(L".try") != std::wstring::npos);
			// the braces balance
			CHECK(std::count(text.begin(), text.end(), L'{') == std::count(text.begin(), text.end(), L'}'));
		}
		CHECK(withClauses > 100);
	}

	SECTION("every method body decodes, and every token has a name") {
		size_t bodies = 0, failed = 0, unnamed = 0;
		std::string examples;
		std::vector<IlInstruction> code;
		for (uint32_t m = 1; m <= md.Methods().size(); m++) {
			auto& method = md.Methods()[m - 1];
			IlMethodBody body;
			if (method.Rva == 0 || (method.ImplFlags & 3) != 0 || !ReadIlMethodBody(pe, method.Rva, body))
				continue;
			bodies++;
			code.clear();
			if (!DecodeIl(body.Code, code)) {
				failed++;
				continue;
			}
			for (auto& c : body.Clauses) {
				CHECK(c.TryOffset + c.TryLength <= body.Code.size());
				CHECK(c.HandlerOffset + c.HandlerLength <= body.Code.size());
			}
			for (auto& inst : code) {
				if (inst.Operand >= IlOperand::Method && inst.Operand != IlOperand::String) {
					auto text = md.TokenText((uint32_t)inst.Value);
					if (text.find(L"(token") != std::wstring::npos || text.find(L"(0x") != std::wstring::npos) {
						if (unnamed++ < 5)
							examples += std::format("0x{:08X} in method {}: {}\n", inst.Value, m, Narrow(text));
					}
				}
			}
			if (body.LocalVarSigToken)
				CHECK(!md.LocalTypes(body.LocalVarSigToken, m).empty());
		}
		CHECK(bodies > 10000);
		CHECK(failed == 0);
		INFO(examples);
		CHECK(unnamed == 0);
	}

	SECTION("strings that the code uses") {
		auto uses = FindIlStringUses(pe, md);
		REQUIRE(uses.size() > 1000);
		CHECK(std::is_sorted(uses.begin(), uses.end(), [](auto& a, auto& b) { return a.Rva < b.Rva; }));
		// the RVA is that of the characters
		auto offset = pe.GetOffsetFromRVA(uses[0].Rva);
		REQUIRE(offset);
	}
}

TEST_CASE("IL is colored", "[clr][il]") {
	auto styles = [](std::string_view line) {
		// one letter per character: the style
		auto s = IlLineStyles(line);
		std::string text;
		for (auto style : s)
			text += "-CSLOdKTN"[(int)style];
		return text;
	};
	CHECK(styles("  IL_0001:  ldstr      \"a \\\" b\" // x") == "--LLLLLLL---OOOOO------SSSSSSSS-CCCC");
	CHECK(styles(".method public static int32 Main") == "ddddddd-KKKKKK-KKKKKK-TTTTT-----");
	CHECK(styles("IL_0002:  ldc.i4     -12") == "LLLLLLL---OOOOOO-----NNN");
	CHECK(styles("brtrue.s IL_0010") == "---------LLLLLLL");
	CHECK(styles("call void System.Object::.ctor()") == "-----TTTT-----------------------");
}

TEST_CASE("The definitions of the tokens of the IL", "[clr][il][system]") {
	PEFile pe;
	if (!OpenMscorlib(pe))
		SKIP("the .NET Framework is not installed");
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	auto list = FindType(md, L"System.Collections.Generic", L"List`1");
	auto add = FindMethod(md, list, L"Add");
	auto ensure = FindMethod(md, list, L"EnsureCapacity");
	REQUIRE(add);
	REQUIRE(ensure);

	std::vector<uint32_t> tokens;
	auto text = DisassembleIlMethod(pe, md, add, &tokens);
	// a token per line
	CHECK(tokens.size() == (size_t)std::count(text.begin(), text.end(), L'\n'));
	CHECK(tokens[0] == ((ClrTableId::MethodDef << 24) | add));	// the declaration
	bool items = false, called = false;
	for (auto token : tokens) {
		if (!token)
			continue;
		auto def = md.Definition(token);
		if ((def >> 24) == ClrTableId::Field && md.Fields()[(def & 0xFFFFFF) - 1].Name == L"_items")
			items = true;
		if (def == ((ClrTableId::MethodDef << 24) | ensure))
			called = true;
	}
	CHECK(items);	// ldfld !0[] class List`1<!0>::_items: a MemberRef of an instance of the type
	CHECK(called);
	// a type of another file has no definition here, but those of this file do
	CHECK(md.Definition((ClrTableId::TypeDef << 24) | list) == ((ClrTableId::TypeDef << 24) | list));
	CHECK(md.Definition(0x01000001) == 0);	// a TypeRef

	// the lines of a type: its declaration, then its members'
	tokens.clear();
	text = DisassembleIlType(pe, md, list, &tokens);
	CHECK(tokens.size() == (size_t)std::count(text.begin(), text.end(), L'\n'));
	CHECK(std::find(tokens.begin(), tokens.end(), (ClrTableId::MethodDef << 24) | ensure) != tokens.end());
}

// The core library of .NET (compiled ahead of time, ReadyToRun): its IL is still there
TEST_CASE("Every method of System.Private.CoreLib decodes", "[clr][il][system]") {
	std::wstring path;
	std::error_code ec;
	for (auto& dir : std::filesystem::directory_iterator(L"C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App", ec))
		if (std::filesystem::exists(dir.path() / L"System.Private.CoreLib.dll"))
			path = (dir.path() / L"System.Private.CoreLib.dll").wstring();
	if (path.empty())
		SKIP(".NET is not installed");
	PEFile pe;
	REQUIRE(pe.Open(path));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	REQUIRE(md.Methods().size() > 10000);
	size_t bodies = 0, failed = 0, unnamed = 0;
	std::vector<IlInstruction> code;
	for (uint32_t m = 1; m <= md.Methods().size(); m++) {
		auto& method = md.Methods()[m - 1];
		IlMethodBody body;
		if (method.Rva == 0 || (method.ImplFlags & 3) != 0 || !ReadIlMethodBody(pe, method.Rva, body))
			continue;
		bodies++;
		code.clear();
		if (!DecodeIl(body.Code, code)) {
			failed++;
			continue;
		}
		for (auto& inst : code)
			if (inst.Operand >= IlOperand::Method && inst.Operand != IlOperand::String && md.TokenText((uint32_t)inst.Value).starts_with(L"(token"))
				unnamed++;
	}
	CHECK(bodies > 10000);
	CHECK(failed == 0);
	CHECK(unnamed == 0);
}

// Prints the IL of the types whose names contain TOTALPE_IL_TYPE in TOTALPE_IL_FILE
TEST_CASE("Print the IL of types", "[.il]") {
	WCHAR file[MAX_PATH]{}, name[256]{};
	if (!::GetEnvironmentVariableW(L"TOTALPE_IL_FILE", file, MAX_PATH) || !::GetEnvironmentVariableW(L"TOTALPE_IL_TYPE", name, 256))
		SKIP("set TOTALPE_IL_FILE and TOTALPE_IL_TYPE");
	PEFile pe;
	REQUIRE(pe.Open(file));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	for (uint32_t t = 1; t <= md.Types().size(); t++)
		if (md.FullName(t).find(name) != std::wstring::npos)
			std::cout << Narrow(DisassembleIlType(pe, md, t)) << std::endl;
}
