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
