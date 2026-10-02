#include "TestCommon.h"
#include <ClrMetadata.h>

namespace {
	struct Opened {
		TempFile File;
		PEFile Pe;
		explicit Opened(std::vector<uint8_t> const& bytes) : File(bytes, L".dll") {
			REQUIRE(Pe.Open(File.Path()));
		}
	};
}

TEST_CASE("The CLR header and metadata of a .NET file are parsed", "[clr]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	spec.Clr = true;
	CAPTURE(spec.Is64);
	Opened file(spec.Build());

	ClrMetadata clr;
	REQUIRE(clr.Parse(file.Pe));
	REQUIRE(clr.HasHeader());
	REQUIRE(clr.HasMetadata());

	SECTION("header") {
		auto& h = clr.Header();
		CHECK(h.cb == sizeof(IMAGE_COR20_HEADER));
		CHECK(h.MajorRuntimeVersion == 2);
		CHECK(h.MinorRuntimeVersion == 5);
		CHECK(h.MetaData.VirtualAddress == SyntheticPE::ClrMetadataRva);
		CHECK(h.MetaData.Size == SyntheticPE::ClrMetadataSize);
		CHECK((h.Flags & COMIMAGE_FLAGS_ILONLY) != 0);
		CHECK(h.EntryPointToken == 0x06000001);
	}

	SECTION("metadata root and streams") {
		CHECK(clr.Version() == L"v4.0.30319");
		auto& streams = clr.Streams();
		REQUIRE(streams.size() == 2);
		CHECK(streams[0].Name == "#~");
		CHECK(streams[0].Offset == 0x40);
		CHECK(streams[0].Size == 108);
		CHECK(streams[1].Name == "#Strings");
		CHECK(streams[1].Size == 40);
	}

	SECTION("tables") {
		auto& tables = clr.Tables();
		REQUIRE(tables.size() == 3);
		CHECK(tables[0].Id == 0x00);
		CHECK(tables[0].Rows == 1);
		CHECK(tables[1].Id == 0x20);
		CHECK(tables[1].Rows == 1);
		CHECK(tables[2].Id == 0x23);
		CHECK(tables[2].Rows == 2);
		CHECK(std::wstring(ClrMetadata::TableName(0x00)) == L"Module");
		CHECK(std::wstring(ClrMetadata::TableName(0x20)) == L"Assembly");
		CHECK(std::wstring(ClrMetadata::TableName(0x23)) == L"AssemblyRef");
	}

	SECTION("module and assembly names") {
		CHECK(clr.ModuleName() == L"Test.dll");
		REQUIRE(clr.HasAssembly());
		auto& a = clr.Assembly();
		CHECK(a.Name == L"TestAsm");
		CHECK(a.Culture.empty());
		CHECK(a.Major == 1);
		CHECK(a.Minor == 2);
		CHECK(a.Build == 3);
		CHECK(a.Revision == 4);
	}

	SECTION("assembly references") {
		auto& refs = clr.References();
		REQUIRE(refs.size() == 2);
		CHECK(refs[0].Name == L"mscorlib");
		CHECK(refs[0].Major == 4);
		CHECK(refs[0].Minor == 0);
		CHECK(refs[1].Name == L"System.Core");
		CHECK(refs[1].Major == 3);
		CHECK(refs[1].Minor == 5);
	}
}

TEST_CASE("A native file has no CLR header", "[clr]") {
	Opened file(SyntheticPE{}.Build());
	ClrMetadata clr;
	CHECK_FALSE(clr.Parse(file.Pe));
	CHECK_FALSE(clr.HasHeader());
	CHECK_FALSE(clr.HasMetadata());
	CHECK(clr.Streams().empty());
}

TEST_CASE("Damaged CLR metadata is handled", "[clr]") {
	SyntheticPE spec;
	spec.Clr = true;
	auto original = spec.Build();
	auto metadata = SyntheticPE::DataOffset + (SyntheticPE::ClrMetadataRva - SyntheticPE::DataRva);

	SECTION("a bad metadata signature keeps the header but finds no metadata") {
		auto bytes = original;
		bytes[metadata] = 'X';
		Opened file(bytes);
		ClrMetadata clr;
		REQUIRE(clr.Parse(file.Pe));
		CHECK(clr.HasHeader());
		CHECK_FALSE(clr.HasMetadata());
	}
	SECTION("a metadata size that is too small") {
		auto bytes = original;
		auto cor = SyntheticPE::DataOffset + (SyntheticPE::ClrHeaderRva - SyntheticPE::DataRva);
		reinterpret_cast<IMAGE_COR20_HEADER*>(bytes.data() + cor)->MetaData.Size = 8;
		Opened file(bytes);
		ClrMetadata clr;
		REQUIRE(clr.Parse(file.Pe));
		CHECK_FALSE(clr.HasMetadata());
	}
	SECTION("every single byte of the metadata can be damaged") {
		for (size_t i = 0; i < SyntheticPE::ClrMetadataSize; i++) {
			for (uint8_t value : { (uint8_t)0x00, (uint8_t)0xFF, (uint8_t)0x7F }) {
				auto bytes = original;
				bytes[metadata + i] = value;
				Opened file(bytes);
				ClrMetadata clr;
				clr.Parse(file.Pe);	// must not crash or read outside the file
			}
		}
	}
}

TEST_CASE("System .NET assemblies are parsed", "[clr][system]") {
	WCHAR windows[MAX_PATH];
	::GetWindowsDirectoryW(windows, MAX_PATH);
	std::wstring path = std::wstring(windows) + L"\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
	if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		SKIP("the .NET Framework is not installed");

	PEFile pe;
	REQUIRE(pe.Open(path));
	ClrMetadata clr;
	REQUIRE(clr.Parse(pe));
	CHECK(clr.HasMetadata());
	CHECK(clr.Version().starts_with(L"v4."));
	CHECK(clr.HasAssembly());
	CHECK(clr.Assembly().Name == L"mscorlib");
	CHECK(clr.Assembly().Major == 4);
	CHECK(clr.Tables().size() > 10);
}
