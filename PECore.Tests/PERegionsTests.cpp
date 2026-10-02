#include "TestCommon.h"
#include <PERegions.h>

namespace {
	HexRegion const* Find(std::vector<HexRegion> const& regions, std::wstring const& name) {
		for (auto& r : regions)
			if (r.Name == name)
				return &r;
		return nullptr;
	}
}

TEST_CASE("The regions of a synthetic PE", "[regions]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	CAPTURE(spec.Is64);
	auto bytes = spec.Build();
	TempFile file(bytes, L".dll");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));

	auto regions = BuildPERegions(pe);
	REQUIRE_FALSE(regions.empty());

	SECTION("every region lies inside the file and has a name and a valid color") {
		for (auto& r : regions) {
			CAPTURE(r.Name);
			CHECK(r.Offset >= 0);
			CHECK(r.Length > 0);
			CHECK(r.Offset + r.Length <= (int64_t)bytes.size());
			CHECK_FALSE(r.Name.empty());
			CHECK(r.Color >= 0);
			CHECK(r.Color < HexRegionColorCount);
		}
	}

	SECTION("headers") {
		auto dos = Find(regions, L"DOS Header");
		REQUIRE(dos != nullptr);
		CHECK(dos->Offset == 0);
		CHECK(dos->Length == sizeof(IMAGE_DOS_HEADER));

		auto stub = Find(regions, L"DOS Stub");
		REQUIRE(stub != nullptr);
		CHECK(stub->Offset == sizeof(IMAGE_DOS_HEADER));
		CHECK(stub->Offset + stub->Length == SyntheticPE::ELfanew);

		auto sig = Find(regions, L"PE Signature");
		REQUIRE(sig != nullptr);
		CHECK(sig->Offset == SyntheticPE::ELfanew);
		CHECK(sig->Length == 4);

		auto fh = Find(regions, L"File Header");
		REQUIRE(fh != nullptr);
		CHECK(fh->Offset == SyntheticPE::ELfanew + 4);
		CHECK(fh->Length == sizeof(IMAGE_FILE_HEADER));

		auto opt = Find(regions, L"Optional Header");
		auto dirs = Find(regions, L"Data Directories");
		auto sections = Find(regions, L"Section Headers");
		REQUIRE(opt != nullptr);
		REQUIRE(dirs != nullptr);
		REQUIRE(sections != nullptr);
		CHECK(opt->Offset == (int64_t)spec.OptionalHeaderOffset());
		CHECK(dirs->Offset == opt->Offset + opt->Length);
		CHECK(dirs->Length == 16 * sizeof(IMAGE_DATA_DIRECTORY));
		CHECK(sections->Offset == dirs->Offset + dirs->Length);
		CHECK(sections->Length == 3 * sizeof(IMAGE_SECTION_HEADER));
		CHECK(sections->Offset == (int64_t)spec.SectionHeaderOffset(0));
	}

	SECTION("sections") {
		struct Expected { PCWSTR Name; uint32_t Offset; } const expected[] = {
			{ L"Section .text", SyntheticPE::TextOffset }, { L"Section .data", SyntheticPE::DataOffset }, { L"Section .rdata", SyntheticPE::RdataOffset },
		};
		for (auto& e : expected) {
			auto r = Find(regions, e.Name);
			REQUIRE(r != nullptr);
			CHECK(r->Offset == e.Offset);
			CHECK(r->Length == SyntheticPE::SectionSize);
		}
	}

	SECTION("directories are located through their RVAs") {
		auto exp = Find(regions, L"Export Directory");
		REQUIRE(exp != nullptr);
		CHECK(exp->Offset == SyntheticPE::RdataOffset + (SyntheticPE::ExportDirRva - SyntheticPE::RdataRva));
		CHECK(exp->Length == SyntheticPE::ExportDirSize);

		auto imp = Find(regions, L"Import Directory");
		REQUIRE(imp != nullptr);
		CHECK(imp->Offset == SyntheticPE::RdataOffset + (SyntheticPE::ImportDirRva - SyntheticPE::RdataRva));

		auto iat = Find(regions, L"IAT Directory");
		REQUIRE(iat != nullptr);
		CHECK(iat->Length == spec.IatSize());

		// the smaller region must come with the section that contains it
		auto rdata = Find(regions, L"Section .rdata");
		CHECK(exp->Offset >= rdata->Offset);
		CHECK(exp->Offset + exp->Length <= rdata->Offset + rdata->Length);
	}

	SECTION("there is no overlay") {
		CHECK(Find(regions, L"Overlay") == nullptr);
	}
}

TEST_CASE("Data after the last section is the overlay", "[regions]") {
	auto bytes = SyntheticPE{}.Build();
	bytes.resize(bytes.size() + 100, 0x5A);
	TempFile file(bytes, L".dll");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));

	auto regions = BuildPERegions(pe);
	auto overlay = Find(regions, L"Overlay");
	REQUIRE(overlay != nullptr);
	CHECK(overlay->Offset == SyntheticPE::FileSize);
	CHECK(overlay->Length == 100);
}

TEST_CASE("File offsets map to RVAs", "[regions]") {
	TempFile file(SyntheticPE{}.Build(), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));
	DWORD rva = 0;

	SECTION("inside a section") {
		REQUIRE(FileOffsetToRva(pe, SyntheticPE::TextOffset, rva));
		CHECK(rva == SyntheticPE::TextRva);
		REQUIRE(FileOffsetToRva(pe, SyntheticPE::DataOffset + 0x25, rva));
		CHECK(rva == SyntheticPE::DataRva + 0x25);
		REQUIRE(FileOffsetToRva(pe, SyntheticPE::RdataOffset + SyntheticPE::SectionSize - 1, rva));
		CHECK(rva == SyntheticPE::RdataRva + SyntheticPE::SectionSize - 1);
	}
	SECTION("the headers map to themselves") {
		REQUIRE(FileOffsetToRva(pe, 0x40, rva));
		CHECK(rva == 0x40);
		REQUIRE(FileOffsetToRva(pe, SyntheticPE::HeadersSize - 1, rva));
		CHECK(rva == SyntheticPE::HeadersSize - 1);
	}
	SECTION("offsets that are not mapped") {
		CHECK_FALSE(FileOffsetToRva(pe, SyntheticPE::FileSize, rva));
		CHECK_FALSE(FileOffsetToRva(pe, 0xFFFFFFFF, rva));
	}
}

TEST_CASE("Regions of a real file", "[regions][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	auto path = std::wstring(dir) + L"\\kernel32.dll";
	PEFile pe;
	if (!pe.Open(path))
		SKIP("kernel32.dll could not be opened");

	auto regions = BuildPERegions(pe);
	CHECK(Find(regions, L"DOS Header") != nullptr);
	CHECK(Find(regions, L"Section .text") != nullptr);
	CHECK(Find(regions, L"Export Directory") != nullptr);
	CHECK(Find(regions, L"Import Directory") != nullptr);
	for (auto& r : regions) {
		CAPTURE(r.Name);
		CHECK(r.Offset + r.Length <= (int64_t)pe.GetFileSize());
	}
	if (pe.GetFileInfo()->HasRichHdr)
		CHECK(Find(regions, L"Rich Header") != nullptr);
}
