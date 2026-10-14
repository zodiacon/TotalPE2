// The decompiler (GhidraDecompiler) on small pieces of machine code: what TotalPE relies on it for (names, imports, strings,
// types, the addresses of lines, x86, x64 and ARM64), and the cases that were fixed.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "TestImage.h"

using Catch::Matchers::ContainsSubstring;

namespace {
	constexpr char const* X64 = "x86:LE:64:default:windows";
	constexpr char const* X86 = "x86:LE:32:default:windows";
	constexpr char const* Arm64 = "AARCH64:LE:64:v8A:windows";

	constexpr uint64_t Code = 0x140001000;
	constexpr uint64_t Slots = 0x140003000;		// the import address table
	constexpr uint64_t ReadOnly = 0x140004000;
	constexpr uint64_t Data = 0x140005000;
	constexpr uint64_t External = 0x7FF000000000;	// where the imports go

	// once: the languages are copied next to the executable when it is built
	void Initialize() {
		static bool done = [] {
			wchar_t path[MAX_PATH];
			::GetModuleFileNameW(nullptr, path, MAX_PATH);
			auto folder = std::filesystem::path(path).parent_path() / L"Decompiler" / L"languages";
			std::string error;
			INFO(error);
			REQUIRE(GhidraDecompiler::Initialize(folder.wstring(), error));
			return true;
		}();
	}

	// An image and a decompiler of it, opened
	struct Fixture {
		explicit Fixture(char const* target = X64) : Decompiler(target, Image.Reader()) {}

		void Open() {
			Initialize();
			std::string error;
			INFO(error);
			REQUIRE(Decompiler.Open(error));
			Decompiler.SetExternalArea(External);
		}

		std::string Decompile(uint64_t address, std::string const& name = {}) {
			std::string code, error;
			INFO(error);
			REQUIRE(Decompiler.Decompile(address, name, code, error, &Functions, &Lines));
			UNSCOPED_INFO(code);
			return code;
		}

		TestImage Image;
		GhidraDecompiler Decompiler;
		GhidraDecompiler::FunctionRefs Functions;
		GhidraDecompiler::LineAddresses Lines;
	};

	size_t Count(std::string const& text, std::string const& part) {
		size_t count = 0;
		for (auto pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + part.size()))
			count++;
		return count;
	}
}

TEST_CASE("A function of x64 code", "[decompiler]") {
	Fixture f;
	f.Image.At(Code).Bytes({ 0x8D, 0x04, 0x11, 0xC3 });		// lea eax, [rcx+rdx]; ret
	f.Open();
	auto code = f.Decompile(Code, "Add");
	CHECK_THAT(code, ContainsSubstring("Add("));
	CHECK_THAT(code, ContainsSubstring("param_2 + param_1") || ContainsSubstring("param_1 + param_2"));
	CHECK_THAT(code, !ContainsSubstring("WARNING"));
	REQUIRE(!f.Functions.empty());
	CHECK(f.Functions[0] == std::pair<std::string, uint64_t>("Add", Code));
}

TEST_CASE("The addresses of the lines", "[decompiler]") {
	Fixture f;
	f.Image.At(Code).Bytes({ 0x8D, 0x04, 0x11, 0xC3 });
	f.Open();
	auto code = f.Decompile(Code, "Add");
	// a list of addresses for each line (the empty one after the last line break as well), the prototype's line at the
	// entry, the return at the instructions
	CHECK(f.Lines.size() == Count(code, "\n") + 1);
	bool entry = false, body = false;
	size_t line = 0, start = 0;
	for (auto end = code.find('\n'); end != std::string::npos && line < f.Lines.size(); start = end + 1, end = code.find('\n', start), line++) {
		auto text = code.substr(start, end - start);
		for (auto address : f.Lines[line]) {
			CHECK((address >= Code && address < Code + 4));
			if (text.find("Add(") != std::string::npos && address == Code)
				entry = true;
			if (text.find("return") != std::string::npos)
				body = true;
		}
	}
	CHECK(entry);
	CHECK(body);
}

TEST_CASE("A call of a function that has a name", "[decompiler]") {
	Fixture f;
	auto helper = Code + 0x100;
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28 })		// sub rsp, 28h
		.Call(helper)
		.Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });	// add rsp, 28h; ret
	f.Image.At(helper).Bytes({ 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 });	// mov eax, 42; ret

	SECTION("given ahead") {
		f.Open();
		f.Decompiler.AddFunction(helper, "Helper");
	}
	SECTION("asked for when it is called") {
		f.Decompiler.SetNameSource([=](uint64_t address) { return address == helper ? std::string("Helper") : std::string(); });
		f.Open();
	}
	auto code = f.Decompile(Code, "Caller");
	CHECK_THAT(code, ContainsSubstring("Helper()"));
	CHECK(std::find(f.Functions.begin(), f.Functions.end(), std::pair<std::string, uint64_t>("Helper", helper)) != f.Functions.end());
}

TEST_CASE("A call through an import", "[decompiler]") {
	Fixture f;
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28 })		// sub rsp, 28h
		.Bytes({ 0xB9, 0x01, 0x00, 0x00, 0x00 })	// mov ecx, 1
		.CallThrough(Slots)						// call [ExitProcess]
		.Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	f.Image.At(Slots).U64(0);
	f.Open();
	f.Decompiler.AddImport(Slots, "ExitProcess", "kernel32.dll");
	auto code = f.Decompile(Code, "Quit");
	CHECK_THAT(code, ContainsSubstring("ExitProcess("));
	CHECK_THAT(code, !ContainsSubstring("A thunk"));
}

TEST_CASE("A thunk of an import", "[decompiler]") {
	// kernel32's CreateFile3: jmp [slot], which Ghidra took for a switch whose table it could not find
	Fixture f;
	f.Image.At(Code).JumpThrough(Slots);
	f.Image.At(Slots).U64(0);
	f.Open();
	f.Decompiler.AddImport(Slots, "CreateFile3", "api-ms-win-core-file-l1-2-5.dll (kernelbase.dll)");
	auto check = [](std::string const& code) {
		CHECK_THAT(code, !ContainsSubstring("WARNING"));
		CHECK_THAT(code, ContainsSubstring("A thunk: it jumps to CreateFile3 in api-ms-win-core-file-l1-2-5.dll (kernelbase.dll)"));
		CHECK(Count(code, "A thunk") == 1);
		CHECK_THAT(code, ContainsSubstring("CreateFile3();"));
	};
	check(f.Decompile(Code, "CreateFile3"));
	// again (the view is opened again): the same
	check(f.Decompile(Code, "CreateFile3"));
}

TEST_CASE("A tail call through an import", "[decompiler]") {
	// the end of a function that jumps to an import (as kernel32's Beep does): a call and a return, not a thunk
	Fixture f;
	f.Image.At(Code)
		.Bytes({ 0xB9, 0x05, 0x00, 0x00, 0x00 })	// mov ecx, 5
		.JumpThrough(Slots);						// jmp [Sleep]
	f.Image.At(Slots).U64(0);
	f.Open();
	f.Decompiler.AddImport(Slots, "Sleep", "kernel32.dll");
	auto code = f.Decompile(Code, "Nap");
	CHECK_THAT(code, !ContainsSubstring("WARNING"));
	CHECK_THAT(code, !ContainsSubstring("A thunk"));
	CHECK_THAT(code, ContainsSubstring("Sleep("));
}

TEST_CASE("An indirect jump to an unknown place stays as it was", "[decompiler]") {
	Fixture f;
	f.Image.At(Code).Bytes({ 0xFF, 0xE1 });		// jmp rcx
	f.Open();
	auto code = f.Decompile(Code, "Jump");
	CHECK_THAT(code, !ContainsSubstring("A thunk"));
}

TEST_CASE("A string in read-only memory", "[decompiler]") {
	Fixture f;
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28 })
		.Bytes({ 0x48, 0x8D, 0x0D }).Rel32(ReadOnly)	// lea rcx, [string]
		.CallThrough(Slots)						// call [OutputDebugStringA]
		.Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	f.Image.At(Slots).U64(0);
	f.Image.At(ReadOnly).Text("Hello, decompiler");
	f.Open();
	f.Decompiler.AddReadOnly(ReadOnly, 0x1000);
	f.Decompiler.AddImport(Slots, "OutputDebugStringA");
	auto code = f.Decompile(Code, "Say");
	CHECK_THAT(code, ContainsSubstring("\"Hello, decompiler\""));
}

namespace {
	// the caller's types: 1 int, 2 a struct of two ints, 3 a pointer to it, 4 int (int, int), 5 int (POINT*)
	bool Describe(uint32_t id, GhidraDecompiler::Type& type) {
		using Kind = GhidraDecompiler::TypeKind;
		switch (id) {
			case 1: type.Kind = Kind::Integer; type.Name = "int"; type.Size = 4; return true;
			case 2:
				type.Kind = Kind::Struct; type.Name = "POINT"; type.Size = 8;
				type.Fields = { { "x", 0, 1 }, { "y", 4, 1 } };
				return true;
			case 3: type.Kind = Kind::Pointer; type.Size = 8; type.Target = 2; return true;
			case 4: type.Kind = Kind::Function; type.Target = 1; type.Parameters = { 1, 1 }; return true;
			case 5: type.Kind = Kind::Function; type.Target = 1; type.Parameters = { 3 }; return true;
		}
		return false;
	}
}

TEST_CASE("A prototype and the types it uses", "[decompiler]") {
	Fixture f;
	f.Image.At(Code).Bytes({ 0x8D, 0x04, 0x11, 0xC3 });		// lea eax, [rcx+rdx]; ret
	f.Image.At(Code + 0x10).Bytes({ 0x8B, 0x41, 0x04, 0xC3 });	// mov eax, [rcx+4]; ret
	f.Decompiler.SetTypeSource(Describe);
	f.Decompiler.SetPrototypeSource([](uint64_t address, uint32_t& type, std::vector<std::string>& names) {
		if (address == Code) {
			type = 4;
			names = { "first", "second" };
			return true;
		}
		if (address == Code + 0x10) {
			type = 5;
			names = { "point" };
			return true;
		}
		return false;
	});
	f.Open();

	auto sum = f.Decompile(Code, "Sum");
	CHECK_THAT(sum, ContainsSubstring("int Sum(int first,int second)"));
	CHECK_THAT(sum, ContainsSubstring("second + first") || ContainsSubstring("first + second"));

	auto y = f.Decompile(Code + 0x10, "GetY");
	CHECK_THAT(y, ContainsSubstring("GetY(POINT *point)"));
	CHECK_THAT(y, ContainsSubstring("point->y"));
}

TEST_CASE("A local variable of optimized code, in a register", "[decompiler]") {
	Fixture f;
	auto a = Code + 0x100;
	f.Image.At(a).Bytes({ 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 });	// mov eax, 42; ret
	f.Decompiler.SetTypeSource(Describe);
	std::vector<GhidraDecompiler::Local> given;
	std::vector<uint64_t> asked;
	f.Decompiler.SetLocalSource([&](uint64_t address, std::vector<uint64_t> const& instructions, std::vector<GhidraDecompiler::Local>& locals) {
		if (address != Code)
			return false;
		asked = instructions;
		locals = given;
		return true;
		});

	SECTION("the variable its value is in is named") {
		f.Image.At(Code)
			.Bytes({ 0x53, 0x48, 0x83, 0xEC, 0x20 })	// push rbx; sub rsp, 20h
			.Bytes({ 0x31, 0xDB })						// xor ebx, ebx
			.Bytes({ 0x8B, 0xCB })						// loop: mov ecx, ebx
			.Call(a)
			.Bytes({ 0xFF, 0xC3 })						// inc ebx
			.Bytes({ 0x83, 0xFB, 0x0A, 0x7C, 0xF2 })	// cmp ebx, 10; jl loop
			.Bytes({ 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 });	// add rsp, 20h; pop rbx; ret
		// in EBX from after "xor ebx, ebx" to after "jl loop"
		GhidraDecompiler::Local i{ "i", 1, 0, "EBX" };
		i.Ranges = { { Code + 7, Code + 0x15 } };
		given = { i };
		f.Open();
		f.Decompiler.AddFunction(a, "A");
		auto code = f.Decompile(Code, "Loop");
		CHECK(std::find(asked.begin(), asked.end(), Code + 5) != asked.end());
		CHECK(std::find(asked.begin(), asked.end(), Code + 0x1A) != asked.end());
		CHECK_THAT(code, ContainsSubstring("A(i)"));
		CHECK(Count(code, " i;") == 1);
		CHECK_THAT(code, !ContainsSubstring("WARNING"));
	}
	SECTION("of its type") {
		f.Image.At(Code)
			.Bytes({ 0x53, 0x48, 0x83, 0xEC, 0x20 })	// push rbx; sub rsp, 20h
			.Bytes({ 0x48, 0x8B, 0x59, 0x08 })			// mov rbx, [rcx+8]
			.Call(a)
			.Bytes({ 0x8B, 0x43, 0x04 })				// mov eax, [rbx+4]
			.Bytes({ 0x03, 0x03 })						// add eax, [rbx]
			.Bytes({ 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 });	// add rsp, 20h; pop rbx; ret
		GhidraDecompiler::Local point{ "point", 3, 0, "RBX" };
		point.Ranges = { { Code + 9, Code + 0x13 } };
		given = { point };
		f.Open();
		f.Decompiler.AddFunction(a, "A");
		auto code = f.Decompile(Code, "Sum");
		CHECK_THAT(code, ContainsSubstring("POINT *point;"));
		CHECK_THAT(code, ContainsSubstring("point->y"));
		CHECK_THAT(code, !ContainsSubstring("WARNING"));

		// renamed by the user: still of its type
		std::string text, error;
		GhidraDecompiler::Variables variables;
		REQUIRE(f.Decompiler.Decompile(Code, "Sum", text, error, nullptr, nullptr, &variables));
		auto variable = std::find_if(variables.begin(), variables.end(), [](auto const& v) { return v.Name == "point"; });
		REQUIRE(variable != variables.end());
		std::vector<GhidraDecompiler::VariableChange> changes{ { variable->Key, "pt", variable->Type } };
		f.Decompiler.SetChangeSource([&](uint64_t address, std::vector<GhidraDecompiler::VariableChange>& out) {
			out = address == Code ? changes : std::vector<GhidraDecompiler::VariableChange>();
			return !out.empty();
			});
		code = f.Decompile(Code, "Sum");
		CHECK_THAT(code, ContainsSubstring("POINT *pt;"));
		CHECK_THAT(code, ContainsSubstring("pt->y"));
	}
	SECTION("a call's result kept in it") {
		f.Image.At(Code)
			.Bytes({ 0x53, 0x48, 0x83, 0xEC, 0x20 })	// push rbx; sub rsp, 20h
			.Call(a)
			.Bytes({ 0x8B, 0xD8 })						// mov ebx, eax
			.Bytes({ 0x8B, 0xCB })						// mov ecx, ebx
			.Call(a)
			.Bytes({ 0x03, 0xC3 })						// add eax, ebx
			.Bytes({ 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 });	// add rsp, 20h; pop rbx; ret
		GhidraDecompiler::Local result{ "result", 1, 0, "EBX" };
		result.Ranges = { { Code + 0xC, Code + 0x15 } };
		given = { result };
		f.Open();
		f.Decompiler.AddFunction(a, "A");
		auto code = f.Decompile(Code, "Twice");
		CHECK_THAT(code, ContainsSubstring("result = A();"));
		CHECK_THAT(code, ContainsSubstring("A(result)"));
	}
	SECTION("one that no value is in is not shown") {
		f.Image.At(Code).Bytes({ 0x8D, 0x04, 0x11, 0xC3 });		// lea eax, [rcx+rdx]; ret
		GhidraDecompiler::Local unused{ "unused", 1, 0, "EBX" };
		unused.Ranges = { { Code, Code + 3 } };
		given = { unused };
		f.Open();
		auto code = f.Decompile(Code, "Add");
		CHECK_THAT(code, !ContainsSubstring("unused"));
	}
	SECTION("one on the stack that the code does not use is not shown") {
		f.Image.At(Code).Bytes({ 0x8D, 0x04, 0x11, 0xC3 });		// lea eax, [rcx+rdx]; ret
		given = { { "spare", 1, -0x10 } };
		f.Open();
		auto code = f.Decompile(Code, "Add");
		CHECK_THAT(code, !ContainsSubstring("spare"));
		CHECK_THAT(code, !ContainsSubstring("WARNING"));
		// (nor when it is decompiled again)
		code = f.Decompile(Code, "Add");
		CHECK_THAT(code, !ContainsSubstring("spare"));
	}
}

TEST_CASE("A member function that returns a structure: \"this\" before where it goes", "[decompiler]") {
	Fixture f;
	f.Image.At(Code).Bytes({
		0x48, 0x8B, 0x01,			// mov rax, [rcx]
		0x48, 0x89, 0x02,			// mov [rdx], rax
		0x48, 0x8B, 0x41, 0x08,		// mov rax, [rcx+8]
		0x48, 0x89, 0x42, 0x08,		// mov [rdx+8], rax
		0x48, 0x8B, 0xC2,			// mov rax, rdx
		0xC3 });
	f.Decompiler.SetTypeSource([](uint32_t id, GhidraDecompiler::Type& type) {
		using Kind = GhidraDecompiler::TypeKind;
		switch (id) {
			case 1: type.Kind = Kind::Integer; type.Name = "longlong"; type.Size = 8; return true;
			case 2: type.Kind = Kind::Struct; type.Name = "PAIR"; type.Size = 16; type.Fields = { { "first", 0, 1 }, { "second", 8, 1 } }; return true;
			case 3: type.Kind = Kind::Struct; type.Name = "OBJECT"; type.Size = 16; type.Fields = { { "x", 0, 1 }, { "y", 8, 1 } }; return true;
			case 4: type.Kind = Kind::Pointer; type.Size = 8; type.Target = 3; return true;
			case 5: type.Kind = Kind::Function; type.Target = 2; type.Parameters = { 4 }; type.Member = true; return true;
		}
		return false;
		});
	f.Decompiler.SetPrototypeSource([](uint64_t address, uint32_t& type, std::vector<std::string>& names) {
		type = 5;
		names = { "this" };
		return address == Code;
		});
	f.Open();
	auto code = f.Decompile(Code, "OBJECT::Get");
	CHECK_THAT(code, ContainsSubstring("this->x"));
	CHECK_THAT(code, ContainsSubstring("this->y"));
}

TEST_CASE("A global variable", "[decompiler]") {
	Fixture f;
	f.Image.At(Code).Bytes({ 0x8B, 0x05 }).Rel32(Data).Bytes({ 0xC3 });	// mov eax, [Counter]; ret
	f.Decompiler.SetTypeSource(Describe);
	f.Open();
	f.Decompiler.AddGlobal(Data, "Counter", 1);
	auto code = f.Decompile(Code, "GetCounter");
	CHECK_THAT(code, ContainsSubstring("return Counter;"));
}

TEST_CASE("A function of x86 code", "[decompiler]") {
	Fixture f(X86);
	f.Image.At(0x401000).Bytes({ 0x8B, 0x44, 0x24, 0x04, 0x03, 0x44, 0x24, 0x08, 0xC3 });	// mov eax, [esp+4]; add eax, [esp+8]; ret
	f.Open();
	auto code = f.Decompile(0x401000, "Add");
	CHECK_THAT(code, ContainsSubstring("param_2 + param_1") || ContainsSubstring("param_1 + param_2"));
	CHECK_THAT(code, !ContainsSubstring("WARNING"));
}

TEST_CASE("A function of ARM64 code", "[decompiler]") {
	Fixture f(Arm64);
	f.Image.At(Code).U32(0x0B010000).U32(0xD65F03C0);	// add w0, w0, w1; ret
	f.Open();
	auto code = f.Decompile(Code, "Add");
	CHECK_THAT(code, ContainsSubstring("param_2 + param_1") || ContainsSubstring("param_1 + param_2"));
	CHECK_THAT(code, !ContainsSubstring("WARNING"));
}

TEST_CASE("What the decompiler cannot do", "[decompiler]") {
	SECTION("a language that is not there") {
		Initialize();
		TestImage image;
		GhidraDecompiler decompiler("MIPS:BE:32:default:default", image.Reader());
		std::string error;
		CHECK_FALSE(decompiler.Open(error));
		CHECK_FALSE(error.empty());
	}
	SECTION("an address without code") {
		Fixture f;
		f.Open();
		std::string code, error;
		auto done = f.Decompiler.Decompile(Code, "Nothing", code, error);
		// it fails, or it says what it could not read
		INFO(code);
		CHECK((!done ? !error.empty() : !code.empty()));
	}
}

namespace {
	// int f(int n): a local on the stack whose address is passed to a function, and the value it returns kept in a register
	// across another call
	//   push rbx; sub rsp, 30h; mov [rsp+20h], ecx; lea rcx, [rsp+20h]; call Fill; mov ebx, eax; call Fill2;
	//   add eax, ebx; add eax, [rsp+20h]; add rsp, 30h; pop rbx; ret
	void Variables(Fixture& f, uint64_t fill, uint64_t fill2) {
		f.Image.At(Code)
			.Bytes({ 0x53, 0x48, 0x83, 0xEC, 0x30, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x8D, 0x4C, 0x24, 0x20 })
			.Call(fill)
			.Bytes({ 0x89, 0xC3 })
			.Call(fill2)
			.Bytes({ 0x01, 0xD8, 0x03, 0x44, 0x24, 0x20, 0x48, 0x83, 0xC4, 0x30, 0x5B, 0xC3 });
		f.Image.At(fill).Bytes({ 0xC7, 0x01, 0x05, 0x00, 0x00, 0x00, 0xB8, 0x07, 0x00, 0x00, 0x00, 0xC3 });	// mov dword [rcx], 5; mov eax, 7; ret
		f.Image.At(fill2).Bytes({ 0xB8, 0x09, 0x00, 0x00, 0x00, 0xC3 });	// mov eax, 9; ret
	}

	GhidraDecompiler::Variable const* Find(GhidraDecompiler::Variables const& variables, std::string const& prefix) {
		for (auto const& v : variables)
			if (v.Key.rfind(prefix, 0) == 0)
				return &v;
		return nullptr;
	}

	std::string Describe(GhidraDecompiler::Variables const& variables) {
		std::string text;
		for (auto const& v : variables)
			text += v.Type + " " + v.Name + " (" + v.Key + ", " + std::to_string(v.Size) + ")\n";
		return text;
	}
}

TEST_CASE("Variables are renamed and retyped", "[decompiler]") {
	Fixture f;
	auto fill = Code + 0x100, fill2 = Code + 0x140;
	Variables(f, fill, fill2);
	std::vector<GhidraDecompiler::VariableChange> changes;
	f.Decompiler.SetChangeSource([&](uint64_t address, std::vector<GhidraDecompiler::VariableChange>& out) {
		out = address == Code ? changes : std::vector<GhidraDecompiler::VariableChange>();
		return !out.empty();
		});
	f.Open();
	f.Decompiler.AddFunction(fill, "Fill");
	f.Decompiler.AddFunction(fill2, "Fill2");
	auto decompile = [&](GhidraDecompiler::Variables& variables) {
		std::string code, error;
		INFO(error);
		REQUIRE(f.Decompiler.Decompile(Code, "Sum", code, error, nullptr, nullptr, &variables));
		UNSCOPED_INFO(code);
		UNSCOPED_INFO(Describe(variables));
		return code;
	};

	GhidraDecompiler::Variables before;
	auto code = decompile(before);
	auto parameter = Find(before, "param:0");
	auto local = Find(before, "stack:");
	auto temporary = Find(before, "reg:") ? Find(before, "reg:") : Find(before, "hash:");
	REQUIRE(parameter);
	REQUIRE(local);
	REQUIRE(temporary);
	CHECK(local->Key == "stack:-24");	// [rsp+20h] after 8 (rbx) and 30h: 18h below the stack pointer at the entry
	// a variable on the stack is local_ and its offset, without its type ("aiStack_18"); bytes of no type are "undef"
	CHECK(local->Name == "local_18");
	CHECK(code.find("Stack_") == std::string::npos);
	CHECK(code.find("undefined") == std::string::npos);
	// the values in registers: Var and a number, without their types ("iVar1")
	REQUIRE(temporary);
	CHECK(temporary->Name.rfind("Var", 0) == 0);
	CHECK(code.find("iVar") == std::string::npos);
	CHECK_THAT(code, ContainsSubstring(parameter->Name));
	CHECK_THAT(code, ContainsSubstring(local->Name));

	SECTION("named, and typed") {
		changes = {
			{ parameter->Key, "count", "uint", true },
			{ local->Key, "slot", local->Type, false },
			{ temporary->Key, "first", temporary->Type, false },
		};
		GhidraDecompiler::Variables after;
		code = decompile(after);
		CHECK_THAT(code, ContainsSubstring("Sum(uint count)"));
		CHECK_THAT(code, ContainsSubstring("slot"));
		CHECK_THAT(code, ContainsSubstring("first"));
		CHECK(code.find(parameter->Name) == std::string::npos);
		// the same variables, by the same keys, with the new names
		REQUIRE(Find(after, parameter->Key));
		CHECK(Find(after, parameter->Key)->Name == "count");
		CHECK(Find(after, parameter->Key)->Type == "uint");
		REQUIRE(Find(after, local->Key));
		CHECK(Find(after, local->Key)->Name == "slot");

		// and back, when the changes are gone
		changes.clear();
		GhidraDecompiler::Variables again;
		code = decompile(again);
		CHECK_THAT(code, ContainsSubstring(parameter->Name));
		CHECK(code.find("count") == std::string::npos);
	}
	SECTION("typed as a pointer to a type of the symbols") {
		f.Decompiler.SetTypeSource([](uint32_t id, GhidraDecompiler::Type& type) {
			if (id == 1) {
				type = { GhidraDecompiler::TypeKind::Struct, "POINT", 8 };
				type.Fields = { { "x", 0, 2 }, { "y", 4, 2 } };
				return true;
			}
			if (id == 2) {
				type = { GhidraDecompiler::TypeKind::Integer, "", 4 };
				return true;
			}
			return false;
			});
		f.Decompiler.SetTypeNameSource([](std::string const& name) { return name == "POINT" ? 1u : 0u; });
		changes = { { parameter->Key, "where", "POINT *", true } };
		GhidraDecompiler::Variables after;
		code = decompile(after);
		CHECK_THAT(code, ContainsSubstring("POINT *where"));
	}
	SECTION("a type of the names Ghidra has (saved before they were changed)") {
		changes = { { parameter->Key, "raw", "undefined4", true } };
		GhidraDecompiler::Variables after;
		code = decompile(after);
		// (known: bytes of a size leave the decompiler free to find their type)
		CHECK_THAT(code, ContainsSubstring(" raw)"));
		CHECK(code.find("not known") == std::string::npos);
	}
	SECTION("what cannot be done is said") {
		changes = {
			{ local->Key, "bad", "NoSuchType", true },
			{ temporary->Key, "wide", "char[3]", true },
		};
		GhidraDecompiler::Variables after;
		code = decompile(after);
		CHECK_THAT(code, ContainsSubstring("bad: the type 'NoSuchType' is not known"));
		if (temporary->Key.rfind("reg:", 0) == 0)
			CHECK_THAT(code, ContainsSubstring("wide: the variable is in"));
	}
}

TEST_CASE("Functions and global variables are renamed", "[decompiler]") {
	Fixture f;
	auto helper = Code + 0x100;
	// mov eax, [rip+data]; call Helper; ret (the global, then the call)
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05 }).Rel32(Data)
		.Bytes({ 0x89, 0xC1 })
		.Call(helper)
		.Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	f.Image.At(helper).Bytes({ 0x8D, 0x41, 0x01, 0xC3 });		// lea eax, [rcx+1]; ret
	f.Image.At(Data).Bytes({ 1, 0, 0, 0 });
	f.Open();
	f.Decompiler.AddFunction(helper, "Helper");
	auto code = f.Decompile(Code, "Caller");
	CHECK_THAT(code, ContainsSubstring("Helper("));

	// a global variable that has no name: Ram and its address, without its type ("uRam0000000140005000")
	CHECK_THAT(code, ContainsSubstring("Helper(Ram0000000140005000)"));

	f.Decompiler.RenameFunction(helper, "Increment");
	f.Decompiler.RenameFunction(Code, "Start");
	f.Decompiler.ChangeGlobal(Data, { "global:0x140005000", "g_count", "int", true });
	code = f.Decompile(Code, "Caller");
	CHECK_THAT(code, ContainsSubstring("Start("));
	CHECK_THAT(code, ContainsSubstring("Increment(g_count)"));
	CHECK(std::find(f.Functions.begin(), f.Functions.end(), std::pair<std::string, uint64_t>("Increment", helper)) != f.Functions.end());

	// an empty name: the name the function had (one made up, without a source of names)
	f.Decompiler.RenameFunction(helper, "");
	f.Decompiler.ChangeGlobal(Data, { "global:0x140005000", "", "", false });
	code = f.Decompile(Code, "Caller");
	CHECK(code.find("Increment") == std::string::npos);
	CHECK(code.find("g_count") == std::string::npos);
}

TEST_CASE("Comments are shown at their instructions", "[decompiler]") {
	Fixture f;
	// lea eax, [rcx+rdx]; ret
	f.Image.At(Code).Bytes({ 0x8D, 0x04, 0x11, 0xC3 });
	f.Decompiler.SetCommentSource([](std::vector<std::pair<uint64_t, std::string>>& comments) {
		comments = { { Code, "the sum" }, { Code + 3, "back" }, { Code + 0x1000, "elsewhere" } };
		});
	f.Open();
	auto code = f.Decompile(Code, "Add");
	CHECK_THAT(code, ContainsSubstring("the sum"));
	CHECK(code.find("elsewhere") == std::string::npos);
	// the comment comes before the statement
	CHECK(code.find("the sum") < code.find("return"));
	// at the indentation of the statement after it
	CHECK_THAT(code, ContainsSubstring("\n  /* the sum */\n"));
}

TEST_CASE("Imports have prototypes, and the values of enums are named", "[decompiler]") {
	Fixture f;
	// DoThing(0x80070005, 3) == 0x80004005 || result == 0x80004007:
	//   sub rsp, 28h; mov edx, 3; mov ecx, 80070005h; call [slot]; cmp eax, 80004005h; je yes; cmp eax, 80004007h; je yes;
	//   xor eax, eax; add rsp, 28h; ret; yes: mov eax, 1; add rsp, 28h; ret
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28, 0xBA, 0x03, 0x00, 0x00, 0x00, 0xB9, 0x05, 0x00, 0x07, 0x80 })
		.CallThrough(Slots)
		.Bytes({ 0x3D, 0x05, 0x40, 0x00, 0x80, 0x74, 0x0F, 0x3D, 0x07, 0x40, 0x00, 0x80, 0x74, 0x08,
			0x31, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	using Kind = GhidraDecompiler::TypeKind;
	f.Decompiler.SetTypeSource([](uint32_t id, GhidraDecompiler::Type& type) {
		switch (id) {
			case 1:	// HRESULT: its values are not flags
				type = { Kind::Enum, "HRESULT", 4, true };
				type.Values = { { "S_OK", 0 }, { "E_FAIL", 0x80004005 }, { "E_ACCESSDENIED", 0x80070005 }, { "S_FALSE", 1 }, { "X_TWO", 2 } };
				type.Exact = true;
				return true;
			case 2:	// flags
				type = { Kind::Enum, "OPTIONS", 4, false };
				type.Values = { { "ONE", 1 }, { "TWO", 2 } };
				return true;
			case 3:
				type = { Kind::Function };
				type.Target = 1;
				type.Parameters = { 1, 2 };
				return true;
		}
		return false;
		});
	uint64_t doThing = 0;
	f.Decompiler.SetPrototypeSource([&](uint64_t address, uint32_t& type, std::vector<std::string>& names) {
		if (address != doThing)
			return false;
		type = 3;
		names = { "code", "options" };
		return true;
		});
	f.Open();
	doThing = f.Decompiler.AddImport(Slots, "DoThing", "THING.dll");
	REQUIRE(doThing != 0);
	auto code = f.Decompile(Code, "Caller");
	CHECK_THAT(code, ContainsSubstring("DoThing(E_ACCESSDENIED,ONE|TWO)") || ContainsSubstring("DoThing(E_ACCESSDENIED,TWO|ONE)"));
	CHECK_THAT(code, ContainsSubstring("E_FAIL"));
	// a value that is no one name is a number, not the | of names that make it up (E_FAIL | X_TWO)
	CHECK(code.find("X_TWO") == std::string::npos);
	CHECK_THAT(code, ContainsSubstring("0x80004007") || ContainsSubstring("-0x7fffbff9"));
}

TEST_CASE("Blocks of the caller's are placed around the lines of their code", "[decompiler]") {
	Fixture f;
	auto a = Code + 0x100, b = Code + 0x110, c = Code + 0x120, handler = Code + 0x130;
	// sub rsp, 28h; call A; call B; call C; add rsp, 28h; ret
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28 })
		.Call(a)
		.Call(b)
		.Call(c)
		.Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	for (auto at : { a, b, c, handler })
		f.Image.At(at).Bytes({ 0xC3 });
	auto callA = Code + 4, callB = Code + 9, callC = Code + 14;
	std::vector<GhidraDecompiler::Block> blocks;
	f.Decompiler.SetBlockSource([&](uint64_t address, std::vector<GhidraDecompiler::Block>& out) {
		if (address == Code)
			out = blocks;
		});
	f.Open();
	f.Decompiler.AddFunction(a, "A");
	f.Decompiler.AddFunction(b, "B");
	f.Decompiler.AddFunction(c, "C");
	GhidraDecompiler::Block guarded{ { { callB, callB + 5 } }, "__try {",
		{ "} __except (EXCEPTION_EXECUTE_HANDLER) {", "\t/* the handler: Main$except_0 */", "}" }, { { "Main$except_0", handler } } };
	auto lineCount = [](std::string const& code) { return (size_t)std::count(code.begin(), code.end(), '\n'); };

	SECTION("one statement") {
		blocks = { guarded };
		auto code = f.Decompile(Code, "Main");
		CHECK_THAT(code, ContainsSubstring("  A();\n  __try {\n    B();\n  } __except (EXCEPTION_EXECUTE_HANDLER) {\n"
			"    /* the handler: Main$except_0 */\n  }\n  C();\n"));
		// the lines keep their addresses (those the block adds have none)
		CHECK(f.Lines.size() >= lineCount(code));
		auto lines = std::count_if(f.Lines.begin(), f.Lines.end(), [&](auto const& l) {
			return std::find(l.begin(), l.end(), callB) != l.end();
			});
		CHECK(lines == 1);
		// its links are functions of the code, with their names
		CHECK(std::find(f.Functions.begin(), f.Functions.end(), std::pair<std::string, uint64_t>("Main$except_0", handler)) != f.Functions.end());
	}
	SECTION("in another, inner ones first") {
		GhidraDecompiler::Block outer{ { { callB, callC + 5 } }, "try {", { "} catch (...) {", "}" }, {} };
		blocks = { guarded, outer };
		auto code = f.Decompile(Code, "Main");
		CHECK_THAT(code, ContainsSubstring("  A();\n  try {\n    __try {\n      B();\n    } __except (EXCEPTION_EXECUTE_HANDLER) {\n"
			"      /* the handler: Main$except_0 */\n    }\n    C();\n  } catch (...) {\n  }\n"));
	}
	SECTION("with other code among its lines: comments where it begins and ends") {
		GhidraDecompiler::Block apart{ { { callA, callA + 5 }, { callC, callC + 5 } }, "__try {", guarded.Close, {} };
		blocks = { apart };
		auto code = f.Decompile(Code, "Main");
		CHECK_THAT(code, ContainsSubstring("  /* __try { */\n  A();\n  B();\n  C();\n"
			"  /* } __except (EXCEPTION_EXECUTE_HANDLER) { the handler: Main$except_0 } */\n"));
	}
	SECTION("whose ranges may go on after its code (a C++ try): the most from its first line that are whole statements") {
		GhidraDecompiler::Block tried{ { { callA, callA + 5 }, { callC, callC + 5 } }, "try {", { "} catch (...) {", "}" }, {} };
		tried.Approximate = true;
		blocks = { tried };
		auto code = f.Decompile(Code, "Main");
		CHECK_THAT(code, ContainsSubstring("  try {\n    A();\n  } catch (...) {\n  }\n  B();\n  C();\n"));
	}
	SECTION("none of its code shown") {
		GhidraDecompiler::Block elsewhere{ { { Code + 0x200, Code + 0x210 } }, "__try {", guarded.Close, {} };
		blocks = { elsewhere };
		auto code = f.Decompile(Code, "Main");
		CHECK_THAT(code, ContainsSubstring("/* __try { } __except (EXCEPTION_EXECUTE_HANDLER) { the handler: Main$except_0 } (the code it guards is not shown) */"));
	}
}

TEST_CASE("A C++ try whose ranges are not whole statements: those that have the most of its code", "[decompiler]") {
	Fixture f;
	auto a = Code + 0x100, x = Code + 0x110, b = Code + 0x120, c = Code + 0x130;
	// sub rsp, 28h; call A; call X; call B; call C; add rsp, 28h; ret
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28 })
		.Call(a)
		.Call(x)
		.Call(b)
		.Call(c)
		.Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	for (auto at : { a, x, b, c })
		f.Image.At(at).Bytes({ 0xC3 });
	auto callA = Code + 4, callB = Code + 14, callC = Code + 19;
	GhidraDecompiler::Block tried{ { { callA, callA + 5 }, { callB, callC + 5 } }, "try {", { "} catch (...) {", "}" }, {} };
	tried.Approximate = true;
	f.Decompiler.SetBlockSource([&](uint64_t address, std::vector<GhidraDecompiler::Block>& out) {
		if (address == Code)
			out = { tried };
		});
	f.Open();
	for (auto [at, name] : { std::pair{ a, "A" }, { x, "X" }, { b, "B" }, { c, "C" } })
		f.Decompiler.AddFunction(at, name);
	auto code = f.Decompile(Code, "Main");
	CHECK_THAT(code, ContainsSubstring("  A();\n  X();\n  try {\n    B();\n    C();\n  } catch (...) {\n  }\n"));
}

TEST_CASE("The code of a block's handler is shown in it", "[decompiler]") {
	Fixture f;
	auto a = Code + 0x100, b = Code + 0x110, funclet = Code + 0x140;
	// sub rsp, 28h; call A; mov eax, 1; add rsp, 28h; ret
	f.Image.At(Code)
		.Bytes({ 0x48, 0x83, 0xEC, 0x28 })
		.Call(a)
		.Bytes({ 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	// a funclet of its own: sub rsp, 28h; call B; add rsp, 28h; ret
	f.Image.At(funclet).Bytes({ 0x48, 0x83, 0xEC, 0x28 }).Call(b).Bytes({ 0x48, 0x83, 0xC4, 0x28, 0xC3 });
	for (auto at : { a, b })
		f.Image.At(at).Bytes({ 0xC3 });
	std::vector<GhidraDecompiler::Block> blocks;
	f.Decompiler.SetBlockSource([&](uint64_t address, std::vector<GhidraDecompiler::Block>& out) {
		if (address == Code)
			out = blocks;
		});
	f.Open();
	f.Decompiler.AddFunction(a, "A");
	f.Decompiler.AddFunction(b, "B");
	GhidraDecompiler::Block guarded{ { { Code + 4, Code + 9 } }, "__try {",
		{ "} __except (EXCEPTION_EXECUTE_HANDLER) {", "\t/* the handler */", "}" }, {} };
	// what it returns is not shown
	guarded.Bodies = { { 1, funclet } };
	blocks = { guarded };
	auto code = f.Decompile(Code, "Main");
	CHECK_THAT(code, ContainsSubstring("    /* the handler */\n    B();\n  }\n"));
}
