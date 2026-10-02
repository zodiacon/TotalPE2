#include "TestCommon.h"
#include <ImportAnalysis.h>

namespace {
	struct Opened {
		TempFile File;
		PEFile Pe;
		explicit Opened(std::vector<uint8_t> const& bytes) : File(bytes, L".dll") {
			REQUIRE(Pe.Open(File.Path()));
		}
	};

	OrdinalNameLookup NoLookup = [](std::string const&, uint16_t) { return std::string(); };
}

TEST_CASE("Imports by ordinal are recognized", "[imports]") {
	SECTION("by name") {
		PEImportFunction fn{};
		fn.FuncName = "CreateFileW";
		strcpy_s(fn.ImpByName.Name, "CreateFileW");
		CHECK_FALSE(IsOrdinalImport(fn, true));
		CHECK_FALSE(IsOrdinalImport(fn, false));
		CHECK(ImportOrdinal(fn) == 0);
	}
	SECTION("by ordinal in a 64-bit file") {
		PEImportFunction fn{};
		fn.unThunk.Thunk64.u1.Ordinal = IMAGE_ORDINAL_FLAG64 | 12;
		CHECK(IsOrdinalImport(fn, true));
		CHECK(ImportOrdinal(fn) == 12);
	}
	SECTION("by ordinal in a 32-bit file") {
		PEImportFunction fn{};
		fn.unThunk.Thunk32.u1.Ordinal = IMAGE_ORDINAL_FLAG32 | 0xFFFF;
		CHECK(IsOrdinalImport(fn, false));
		CHECK(ImportOrdinal(fn) == 0xFFFF);	// the flag is not part of the ordinal
	}
	SECTION("the width of the thunk decides which flag counts") {
		PEImportFunction fn{};
		fn.unThunk.Thunk64.u1.Ordinal = IMAGE_ORDINAL_FLAG64 | 12;
		CHECK_FALSE(IsOrdinalImport(fn, false));	// as a 32-bit thunk the top bit is clear
		PEImportFunction fn32{};
		fn32.unThunk.Thunk32.u1.Ordinal = IMAGE_ORDINAL_FLAG32 | 12;
		CHECK_FALSE(IsOrdinalImport(fn32, true));
	}
	SECTION("an empty import") {
		PEImportFunction fn{};
		CHECK_FALSE(IsOrdinalImport(fn, true));
		CHECK_FALSE(IsOrdinalImport(fn, false));
	}
}

TEST_CASE("The import hash of a synthetic file", "[imports]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	CAPTURE(spec.Is64);
	Opened file(spec.Build());

	SECTION("a module and a function in lowercase, without the extension") {
		CHECK(ImphashInput(file.Pe, NoLookup) == "kernel32.exitprocess");
		CHECK(ComputeImphash(file.Pe) == "f9ade0aa18f660a34a4fa23392e21838");	// md5("kernel32.exitprocess")
	}
}

TEST_CASE("Ordinal imports in the import hash", "[imports]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	spec.ImportByOrdinal = true;
	CAPTURE(spec.Is64);

	SECTION("a library without a name table uses ordN") {
		spec.ImportModule = "kernel32.dll";
		spec.ImportOrdinal = 5;
		Opened file(spec.Build());
		auto const& imports = *file.Pe.GetImport();
		REQUIRE(imports.size() == 1);
		REQUIRE(imports[0].ImportFunc.size() == 1);
		CHECK(IsOrdinalImport(imports[0].ImportFunc[0], spec.Is64));
		CHECK(ImportOrdinal(imports[0].ImportFunc[0]) == 5);

		bool asked = false;
		auto lookup = [&](std::string const&, uint16_t) { asked = true; return std::string("ignored"); };
		CHECK(ImphashInput(file.Pe, lookup) == "kernel32.ord5");
		CHECK_FALSE(asked);	// kernel32 is not one of the libraries with a table
		CHECK(ComputeImphash(file.Pe) == "9955267f0bde481657b972168f4c3161");	// md5("kernel32.ord5")
	}
	SECTION("ws2_32 ordinals are looked up") {
		spec.ImportModule = "WS2_32.dll";
		spec.ImportOrdinal = 10;
		Opened file(spec.Build());

		std::string asked;
		auto lookup = [&](std::string const& dll, uint16_t ordinal) {
			asked = dll + "#" + std::to_string(ordinal);
			return std::string("ioctlsocket");
		};
		CHECK(ImphashInput(file.Pe, lookup) == "ws2_32.ioctlsocket");
		CHECK(asked == "ws2_32.dll#10");	// the name is lowercased before the lookup
		// when the lookup knows nothing the ordinal is used
		CHECK(ImphashInput(file.Pe, NoLookup) == "ws2_32.ord10");
	}
	SECTION("without a lookup the fixed tables of pefile are used") {
		spec.ImportModule = "ws2_32.dll";
		spec.ImportOrdinal = 10;
		Opened file(spec.Build());
		CHECK(ImphashInput(file.Pe) == "ws2_32.ioctlsocket");
		CHECK(ComputeImphash(file.Pe) == "045958ecb070bfaaeeb48e3b8178beff");	// md5("ws2_32.ioctlsocket")
	}
	SECTION("ordinals that pefile does not know become ordN") {
		spec.ImportModule = "ws2_32.dll";
		spec.ImportOrdinal = 60000;
		Opened file(spec.Build());
		CHECK(ImphashInput(file.Pe) == "ws2_32.ord60000");
	}
	SECTION("the tables follow pefile, not the current Windows") {
		// ordinal 24 of ws2_32 was WSApSetPostRoutine in the tables of pefile; the ws2_32.dll of today exports another function
		spec.ImportModule = "ws2_32.dll";
		spec.ImportOrdinal = 24;
		Opened file(spec.Build());
		CHECK(ImphashInput(file.Pe) == "ws2_32.wsapsetpostroutine");
	}
	SECTION("wsock32 and oleaut32 have their own tables") {
		spec.ImportModule = "oleaut32.dll";
		spec.ImportOrdinal = 2;
		Opened ole(spec.Build());
		CHECK(ImphashInput(ole.Pe) == "oleaut32.sysallocstring");

		spec.ImportModule = "wsock32.dll";
		spec.ImportOrdinal = 3;
		Opened sock(spec.Build());
		CHECK(ImphashInput(sock.Pe) == "wsock32.closesocket");
	}
}

TEST_CASE("File names without a recognized extension keep it", "[imports]") {
	SyntheticPE spec;
	spec.ImportModule = "Some.Library.EXE";
	Opened file(spec.Build());
	// only .dll, .ocx and .sys are removed
	CHECK(ImphashInput(file.Pe, NoLookup) == "some.library.exe.exitprocess");

	spec.ImportModule = "driver.SYS";
	Opened sys(spec.Build());
	CHECK(ImphashInput(sys.Pe, NoLookup) == "driver.exitprocess");
}

TEST_CASE("A file without imports has no import hash", "[imports]") {
	auto bytes = SyntheticPE{}.Build();
	// the import directory is gone
	auto dirs = bytes.data() + SyntheticPE{}.OptionalHeaderOffset() + 112;
	memset(dirs + IMAGE_DIRECTORY_ENTRY_IMPORT * 8, 0, 8);
	Opened file(bytes);
	CHECK(ImphashInput(file.Pe, NoLookup).empty());
	CHECK(ComputeImphash(file.Pe).empty());
}

TEST_CASE("Ordinals are resolved to names through the export tables of system DLLs", "[imports][system]") {
	SECTION("a well known ordinal") {
		auto name = ResolveOrdinalName("ws2_32.dll", 10, false);
		if (name.empty())
			SKIP("the system's ws2_32.dll could not be read");
		CHECK(name == "ioctlsocket");
		CHECK(ResolveOrdinalName("WS2_32.DLL", 10, false) == "ioctlsocket");	// case does not matter; the second call is cached
	}
	SECTION("oleaut32") {
		auto name = ResolveOrdinalName("oleaut32.dll", 2, false);
		if (name.empty())
			SKIP("the system's oleaut32.dll could not be read");
		CHECK(name == "SysAllocString");
	}
	SECTION("things that are not there") {
		CHECK(ResolveOrdinalName("ws2_32.dll", 60000, false).empty());
		CHECK(ResolveOrdinalName("no-such-library.dll", 1, false).empty());
		CHECK(ResolveOrdinalName("", 1, false).empty());
	}
	SECTION("an API set name is resolved to its host first") {
		// kernelbase exports HeapAlloc? no: it is the host of the heap API set; any ordinal lookup must not crash
		ResolveOrdinalName("api-ms-win-core-heap-l1-1-0.dll", 1, false);
	}
	SECTION("the 32-bit system directory") {
		WCHAR dir[MAX_PATH];
		if (!::GetSystemWow64DirectoryW(dir, MAX_PATH))
			SKIP("there is no 32-bit system directory");
		auto name = ResolveOrdinalName("ws2_32.dll", 10, true);
		CHECK(name == "ioctlsocket");
	}
}

// Writes synthetic files with imports by ordinal into the directory named by PECORE_TEST_DIR, so that their import
// hashes can be compared with the pefile library.
TEST_CASE("Write synthetic files with ordinal imports to PECORE_TEST_DIR", "[.dumpimports]") {
	wchar_t* dir = nullptr;
	size_t length = 0;
	REQUIRE((_wdupenv_s(&dir, &length, L"PECORE_TEST_DIR") == 0 && dir));
	struct { char const* Module; uint16_t Ordinal; } const cases[] = {
		{ "ws2_32.dll", 1 }, { "ws2_32.dll", 10 }, { "ws2_32.dll", 24 }, { "ws2_32.dll", 115 }, { "ws2_32.dll", 60000 },
		{ "WS2_32.DLL", 12 }, { "wsock32.dll", 3 }, { "wsock32.dll", 111 }, { "oleaut32.dll", 2 }, { "oleaut32.dll", 144 },
		{ "oleaut32.dll", 9999 }, { "kernel32.dll", 5 }, { "comctl32.dll", 17 }, { "mfc42.dll", 800 }, { "msvbvm60.dll", 100 },
		{ "driver.sys", 1 }, { "control.ocx", 7 },
	};
	int n = 0;
	for (bool is64 : { true, false })
		for (auto& c : cases) {
			SyntheticPE spec;
			spec.Is64 = is64;
			spec.ImportByOrdinal = true;
			spec.ImportModule = c.Module;
			spec.ImportOrdinal = c.Ordinal;
			auto bytes = spec.Build();
			auto path = std::wstring(dir) + L"\\ordinal" + std::to_wstring(n++) + L".dll";
			wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
			REQUIRE(file);
			DWORD written;
			REQUIRE(::WriteFile(file.get(), bytes.data(), (DWORD)bytes.size(), &written, nullptr));
		}
	free(dir);
}

// Prints the import hash of the file named by PECORE_TEST_FILE, to compare it with the pefile library.
TEST_CASE("Print the import hash of PECORE_TEST_FILE", "[.imphash]") {
	wchar_t* path = nullptr;
	size_t length = 0;
	REQUIRE((_wdupenv_s(&path, &length, L"PECORE_TEST_FILE") == 0 && path));
	PEFile pe;
	REQUIRE(pe.Open(path));
	printf("IMPHASH %s\n", ComputeImphash(pe).c_str());
	free(path);
}
