#include "TestCommon.h"
#include <filesystem>

// These tests open real Windows binaries. They check structural invariants rather than exact values, since the
// contents of system files change between Windows builds. A missing file skips the test.

namespace {
	std::wstring SystemFile(PCWSTR name) {
		WCHAR dir[MAX_PATH];
		::GetSystemDirectoryW(dir, MAX_PATH);
		return std::wstring(dir) + L"\\" + name;
	}

	// opens a system file, skipping the test if it does not exist
	void OpenSystemFile(PEFile& pe, PCWSTR name) {
		auto path = SystemFile(name);
		if (!std::filesystem::exists(path))
			SKIP("the system file does not exist on this machine");
		REQUIRE(pe.Open(path));
	}

	bool EndsWithNoCase(std::string s, std::string suffix) {
		if (s.size() < suffix.size())
			return false;
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
	}
}

TEST_CASE("kernel32.dll", "[system]") {
	PEFile pe;
	OpenSystemFile(pe, L"kernel32.dll");
	auto info = pe.GetFileInfo();

	SECTION("basic properties") {
		CHECK(info->IsPE64 == (sizeof(void*) == 8));	// the system directory of a 64-bit process
		CHECK(info->HasSections);
		CHECK(info->HasExport);
		CHECK(info->HasImport);
		CHECK((pe.GetNTHeader()->NTHdr32.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0);
		CHECK(pe.GetFileSize() > 100 * 1024);
	}

	SECTION("exports") {
		auto exp = pe.GetExport();
		REQUIRE(exp != nullptr);
		CHECK_THAT(exp->ModuleName, Catch::Matchers::Equals("KERNEL32.dll", Catch::CaseSensitive::No));
		CHECK(exp->Funcs.size() > 1000);

		bool createFile = false, forwarded = false;
		for (auto& f : exp->Funcs) {
			if (f.FuncName == "CreateFileW") {
				createFile = true;
				CHECK(f.FuncRVA != 0);
				CHECK(f.ForwarderName.empty());
				CHECK(pe.GetOffsetFromRVA(f.FuncRVA) != 0);
			}
			if (!f.ForwarderName.empty()) {
				forwarded = true;
				CHECK(f.ForwarderName.find('.') != std::string::npos);	// "NTDLL.RtlAllocateHeap"
			}
		}
		CHECK(createFile);
		CHECK(forwarded);
	}

	SECTION("imports") {
		REQUIRE(info->HasImport);
		auto imports = pe.GetImport();
		REQUIRE_FALSE(imports->empty());
		for (auto& mod : *imports) {
			CAPTURE(mod.ModuleName);
			CHECK_FALSE(mod.ModuleName.empty());
			for (auto& fn : mod.ImportFunc) {
				// an import is either by name or by ordinal
				bool byName = !fn.FuncName.empty();
				bool byOrdinal = fn.ImpByName.Name[0] == 0;
				CHECK((byName || byOrdinal));
			}
		}
	}

	SECTION("sections are consistent") {
		auto sections = pe.GetSecHeaders();
		REQUIRE_FALSE(sections->empty());
		DWORD previousEnd = 0;
		for (auto& s : *sections) {
			CAPTURE(s.SectionName);
			CHECK(s.SecHdr.VirtualAddress >= previousEnd);
			CHECK((uint64_t)s.SecHdr.PointerToRawData + s.SecHdr.SizeOfRawData <= pe.GetFileSize());
			previousEnd = s.SecHdr.VirtualAddress;
		}
		CHECK((*sections)[0].SectionName == ".text");
	}

	SECTION("relocations") {
		REQUIRE(info->HasReloc);
		auto relocs = pe.GetRelocations();
		REQUIRE_FALSE(relocs->empty());
		for (auto& block : *relocs) {
			CHECK(block.BaseReloc.VirtualAddress % 0x1000 == 0);	// blocks cover one page each
			CHECK_FALSE(block.RelocData.empty());
			for (auto& r : block.RelocData)
				CHECK(r.RelocOffset < 0x1000);
		}
	}

	SECTION("exception directory") {
		REQUIRE(info->HasException);
		auto ex = pe.GetExceptions();
		REQUIRE_FALSE(ex->empty());
		for (auto& e : *ex)
			CHECK(e.RuntimeFuncEntry.BeginAddress < e.RuntimeFuncEntry.EndAddress);
	}

	SECTION("debug directory") {
		REQUIRE(info->HasDebug);
		bool pdb = false;
		for (auto& d : *pe.GetDebug())
			if (!d.PdbPath.empty()) {
				pdb = true;
				CHECK(EndsWithNoCase(d.PdbPath, ".pdb"));
			}
		CHECK(pdb);
	}

	SECTION("every mapped RVA converts to a file offset inside the file") {
		for (auto& s : *pe.GetSecHeaders()) {
			if (s.SecHdr.SizeOfRawData == 0)
				continue;
			auto offset = pe.GetOffsetFromRVA(s.SecHdr.VirtualAddress);
			CHECK(offset == s.SecHdr.PointerToRawData);
		}
	}
}

TEST_CASE("cmd.exe has resources", "[system]") {
	PEFile pe;
	OpenSystemFile(pe, L"cmd.exe");
	REQUIRE(pe.GetFileInfo()->HasResource);

	auto& resources = pe.GetFlatResources();
	REQUIRE_FALSE(resources.empty());

	bool version = false, manifest = false;
	for (auto& r : resources) {
		if (r.TypeID == (WORD)(ULONG_PTR)RT_VERSION)
			version = true;
		if (r.TypeID == (WORD)(ULONG_PTR)RT_MANIFEST)
			manifest = true;
		CHECK_FALSE(r.Data.empty());
	}
	CHECK(version);
	CHECK(manifest);
}

TEST_CASE("The same file opened twice gives identical results", "[system]") {
	PEFile a, b;
	OpenSystemFile(a, L"user32.dll");
	REQUIRE(b.Open(a.GetPath()));

	CHECK(a.GetFileSize() == b.GetFileSize());
	REQUIRE(a.GetImport()->size() == b.GetImport()->size());
	CHECK(a.GetExport()->Funcs.size() == b.GetExport()->Funcs.size());
	CHECK(a.GetRelocations()->size() == b.GetRelocations()->size());
	CHECK(a.GetFlatResources().size() == b.GetFlatResources().size());
}
