#include "TestCommon.h"

namespace {
	PEExportFunction const* FindExport(PEExport const& exp, std::string const& name) {
		for (auto& f : exp.Funcs)
			if (f.FuncName == name)
				return &f;
		return nullptr;
	}
}

TEST_CASE("A synthetic PE is parsed correctly", "[parse]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	spec.Dll = GENERATE(true, false);
	CAPTURE(spec.Is64, spec.Dll);

	auto bytes = spec.Build();
	TempFile file(bytes, spec.Dll ? L".dll" : L".exe");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));

	SECTION("file info") {
		auto info = pe.GetFileInfo();
		CHECK(info->IsPE64 == spec.Is64);
		CHECK(info->IsPE32 == !spec.Is64);
		CHECK(info->HasSections);
		CHECK(info->HasDataDirs);
		CHECK(info->HasExport);
		CHECK(info->HasImport);
		CHECK(pe.GetImageBase() == spec.ImageBase());
	}

	SECTION("DOS header") {
		auto dos = pe.GetMSDOSHeader();
		CHECK(dos->e_magic == IMAGE_DOS_SIGNATURE);
		CHECK(dos->e_lfanew == SyntheticPE::ELfanew);
		CHECK(pe.GetNTHeader()->dwOffset == SyntheticPE::ELfanew);
	}

	SECTION("NT headers") {
		auto nt = pe.GetNTHeader();
		// the file header sits at the same place for PE32 and PE32+
		auto& fh = nt->NTHdr32.FileHeader;
		CHECK(nt->NTHdr32.Signature == IMAGE_NT_SIGNATURE);
		CHECK(fh.Machine == (spec.Is64 ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386));
		CHECK(fh.NumberOfSections == 3);
		CHECK(fh.TimeDateStamp == SyntheticPE::TimeDateStamp);
		CHECK(((fh.Characteristics & IMAGE_FILE_DLL) != 0) == spec.Dll);
		CHECK((fh.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) != 0);

		if (spec.Is64) {
			auto& opt = nt->NTHdr64.OptionalHeader;
			CHECK(opt.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
			CHECK(opt.ImageBase == spec.ImageBase());
			CHECK(opt.AddressOfEntryPoint == SyntheticPE::EntryPointRva);
			CHECK(opt.SectionAlignment == SyntheticPE::SectionAlignment);
			CHECK(opt.FileAlignment == SyntheticPE::FileAlignment);
			CHECK(opt.SizeOfImage == SyntheticPE::ImageSize);
			CHECK(opt.SizeOfHeaders == SyntheticPE::HeadersSize);
			CHECK(opt.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI);
			CHECK(opt.NumberOfRvaAndSizes == IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
		}
		else {
			auto& opt = nt->NTHdr32.OptionalHeader;
			CHECK(opt.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);
			CHECK(opt.ImageBase == spec.ImageBase());
			CHECK(opt.AddressOfEntryPoint == SyntheticPE::EntryPointRva);
			CHECK(opt.BaseOfData == SyntheticPE::DataRva);
			CHECK(opt.SectionAlignment == SyntheticPE::SectionAlignment);
			CHECK(opt.FileAlignment == SyntheticPE::FileAlignment);
			CHECK(opt.SizeOfImage == SyntheticPE::ImageSize);
			CHECK(opt.SizeOfHeaders == SyntheticPE::HeadersSize);
			CHECK(opt.NumberOfRvaAndSizes == IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
		}
	}

	SECTION("section headers") {
		auto sections = pe.GetSecHeaders();
		REQUIRE(sections->size() == 3);

		struct Expected { char const* Name; uint32_t Rva, Offset; DWORD Flags; } const expected[] = {
			{ ".text", SyntheticPE::TextRva, SyntheticPE::TextOffset, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ },
			{ ".data", SyntheticPE::DataRva, SyntheticPE::DataOffset, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE },
			{ ".rdata", SyntheticPE::RdataRva, SyntheticPE::RdataOffset, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ },
		};
		for (size_t i = 0; i < 3; i++) {
			CAPTURE(i);
			auto& s = (*sections)[i];
			CHECK(s.SectionName == expected[i].Name);
			CHECK(std::string((char const*)s.SecHdr.Name) == expected[i].Name);
			CHECK(s.SecHdr.VirtualAddress == expected[i].Rva);
			CHECK(s.SecHdr.PointerToRawData == expected[i].Offset);
			CHECK(s.SecHdr.SizeOfRawData == SyntheticPE::SectionSize);
			CHECK(s.SecHdr.Misc.VirtualSize == SyntheticPE::SectionSize);
			CHECK(s.SecHdr.Characteristics == expected[i].Flags);
		}
	}

	SECTION("data directories") {
		auto dirs = pe.GetDataDirs();
		REQUIRE(dirs->size() == IMAGE_NUMBEROF_DIRECTORY_ENTRIES);

		auto& exp = (*dirs)[IMAGE_DIRECTORY_ENTRY_EXPORT];
		CHECK(exp.DataDir.VirtualAddress == SyntheticPE::ExportDirRva);
		CHECK(exp.DataDir.Size == SyntheticPE::ExportDirSize);
		CHECK(exp.Section == ".rdata");

		auto& imp = (*dirs)[IMAGE_DIRECTORY_ENTRY_IMPORT];
		CHECK(imp.DataDir.VirtualAddress == SyntheticPE::ImportDirRva);
		CHECK(imp.DataDir.Size == SyntheticPE::ImportDirSize);

		auto& iat = (*dirs)[IMAGE_DIRECTORY_ENTRY_IAT];
		CHECK(iat.DataDir.VirtualAddress == SyntheticPE::IatRva);
		CHECK(iat.DataDir.Size == spec.IatSize());

		// the rest is empty
		for (int i : { IMAGE_DIRECTORY_ENTRY_RESOURCE, IMAGE_DIRECTORY_ENTRY_EXCEPTION, IMAGE_DIRECTORY_ENTRY_SECURITY,
			IMAGE_DIRECTORY_ENTRY_BASERELOC, IMAGE_DIRECTORY_ENTRY_DEBUG, IMAGE_DIRECTORY_ENTRY_TLS,
			IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR }) {
			CAPTURE(i);
			CHECK((*dirs)[i].DataDir.VirtualAddress == 0);
			CHECK((*dirs)[i].DataDir.Size == 0);
		}
	}

	SECTION("RVA to file offset") {
		CHECK(pe.GetOffsetFromRVA(SyntheticPE::TextRva) == SyntheticPE::TextOffset);
		CHECK(pe.GetOffsetFromRVA(SyntheticPE::TextRva + 0x10) == SyntheticPE::TextOffset + 0x10);
		CHECK(pe.GetOffsetFromRVA(SyntheticPE::DataRva + 5) == SyntheticPE::DataOffset + 5);
		CHECK(pe.GetOffsetFromRVA(SyntheticPE::RdataRva + 0x123) == SyntheticPE::RdataOffset + 0x123);
		CHECK(pe.GetOffsetFromRVA(SyntheticPE::RdataRva + SyntheticPE::SectionSize - 1) == SyntheticPE::RdataOffset + SyntheticPE::SectionSize - 1);
		// the headers are mapped one to one
		CHECK(pe.GetOffsetFromRVA(0x40) == 0x40);
	}

	SECTION("exports") {
		auto exp = pe.GetExport();
		REQUIRE(exp != nullptr);
		CHECK(exp->ModuleName == "test.dll");
		CHECK(exp->ExportDesc.Base == 1);
		REQUIRE(exp->Funcs.size() == 2);

		auto alpha = FindExport(*exp, "Alpha");
		auto beta = FindExport(*exp, "Beta");
		REQUIRE(alpha != nullptr);
		REQUIRE(beta != nullptr);
		CHECK(alpha->FuncRVA == SyntheticPE::AlphaRva);
		CHECK(beta->FuncRVA == SyntheticPE::BetaRva);
		CHECK(alpha->Ordinal == 1);
		CHECK(beta->Ordinal == 2);
		CHECK(alpha->ForwarderName.empty());
		CHECK(beta->ForwarderName.empty());
	}

	SECTION("imports") {
		auto imports = pe.GetImport();
		REQUIRE(imports->size() == 1);

		auto& mod = (*imports)[0];
		CHECK(mod.ModuleName == "kernel32.dll");
		REQUIRE(mod.ImportFunc.size() == 1);
		CHECK(mod.ImportFunc[0].FuncName == "ExitProcess");
		CHECK(std::string(mod.ImportFunc[0].ImpByName.Name) == "ExitProcess");
		CHECK(mod.ImportFunc[0].ImpByName.Hint == 0);

		// the descriptor's RVAs: anything that works with import slots (such as symbol lookup) relies on these
		CHECK(mod.ImportDesc.OriginalFirstThunk == 0x30D0);
		CHECK(mod.ImportDesc.Name == 0x3120);
		CHECK(mod.ImportDesc.FirstThunk == SyntheticPE::IatRva);
	}

	SECTION("directories that are not present") {
		auto info = pe.GetFileInfo();
		CHECK_FALSE(info->HasResource);
		CHECK_FALSE(info->HasSecurity);
		CHECK_FALSE(info->HasTLS);
		CHECK_FALSE(info->HasLoadConfig);
		CHECK_FALSE(info->HasReloc);
		CHECK_FALSE(info->HasException);
		CHECK_FALSE(info->HasDebug);
		CHECK_FALSE(info->HasDelayImport);
		CHECK_FALSE(info->HasCOMDescr);
		CHECK_FALSE(info->HasRichHdr);

		CHECK(pe.GetTLS() == nullptr);
		CHECK(pe.GetLoadConfig() == nullptr);
		CHECK(pe.GetRichHeader() == nullptr);
		CHECK(pe.GetRelocations()->empty());
		CHECK(pe.GetExceptions()->empty());
		CHECK(pe.GetDelayImport()->empty());
		CHECK(pe.GetDebug()->empty());
		CHECK(pe.GetSecurity()->empty());
		CHECK(pe.GetFlatResources().empty());
	}
}

TEST_CASE("Lookup tables cover the common values", "[parse]") {
	CHECK(MapFileHdrMachine.at(IMAGE_FILE_MACHINE_AMD64) == L"IMAGE_FILE_MACHINE_AMD64");
	CHECK(MapFileHdrMachine.at(IMAGE_FILE_MACHINE_I386) == L"IMAGE_FILE_MACHINE_I386");
	CHECK(MapFileHdrMachine.at(IMAGE_FILE_MACHINE_ARM64) == L"IMAGE_FILE_MACHINE_ARM64");
}
