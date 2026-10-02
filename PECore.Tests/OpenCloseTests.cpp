#include "TestCommon.h"

TEST_CASE("A default constructed PEFile is empty", "[open]") {
	PEFile pe;
	CHECK_FALSE(pe);
	CHECK(pe.GetPath().empty());
	CHECK(pe.GetFileSize() == 0);
	CHECK(pe.GetData() == nullptr);
	CHECK(pe.GetExport() == nullptr);
	CHECK(pe.GetSecHeaders()->empty());
	CHECK(pe.GetImport()->empty());
}

TEST_CASE("Open fails gracefully on bad input", "[open]") {
	PEFile pe;
	auto bytes = SyntheticPE{}.Build();

	SECTION("missing file") {
		CHECK_FALSE(pe.Open(L"C:\\this\\path\\does\\not\\exist\\file.exe"));
	}
	SECTION("empty file") {
		TempFile file(std::vector<uint8_t>{});
		CHECK_FALSE(pe.Open(file.Path()));
	}
	SECTION("a text file") {
		TempFile file(std::string("hello, this is certainly not a PE file"));
		CHECK_FALSE(pe.Open(file.Path()));
	}
	SECTION("only the DOS magic") {
		TempFile file(std::vector<uint8_t>{ 'M', 'Z' });
		CHECK_FALSE(pe.Open(file.Path()));
	}
	SECTION("a DOS header without a PE header") {
		bytes.resize(sizeof(IMAGE_DOS_HEADER));
		TempFile file(bytes);
		CHECK_FALSE(pe.Open(file.Path()));
	}
	SECTION("e_lfanew points beyond the end of the file") {
		reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data())->e_lfanew = 0x100000;
		TempFile file(bytes);
		CHECK_FALSE(pe.Open(file.Path()));
	}
	SECTION("corrupt PE signature") {
		bytes[SyntheticPE::ELfanew] = 'X';
		TempFile file(bytes);
		CHECK_FALSE(pe.Open(file.Path()));
	}

	// a failed Open never leaves a half open file behind
	CHECK_FALSE(pe);
	CHECK(pe.GetPath().empty());
}

TEST_CASE("Open, then Close", "[open]") {
	SyntheticPE spec;
	auto bytes = spec.Build();
	TempFile file(bytes, L".dll");

	PEFile pe;
	REQUIRE(pe.Open(file.Path()));
	CHECK(pe);
	CHECK(pe.GetPath() == file.Path());
	CHECK(pe.GetFileSize() == bytes.size());
	CHECK(pe.GetData() != nullptr);
	CHECK(pe.GetSecHeaders()->size() == 3);

	pe.Close();
	CHECK_FALSE(pe);
	CHECK(pe.GetPath().empty());
	CHECK(pe.GetFileSize() == 0);
	CHECK(pe.GetData() == nullptr);
	CHECK(pe.GetSecHeaders()->empty());
	CHECK(pe.GetDataDirs()->empty());
	CHECK(pe.GetImport()->empty());
	CHECK(pe.GetExport() == nullptr);

	SECTION("closing twice is harmless") {
		pe.Close();
		CHECK_FALSE(pe);
	}
}

TEST_CASE("Opening another file replaces the previous one", "[open]") {
	SyntheticPE spec64, spec32;
	spec32.Is64 = false;
	TempFile file64(spec64.Build(), L".dll"), file32(spec32.Build(), L".dll");

	PEFile pe;
	REQUIRE(pe.Open(file64.Path()));
	CHECK(pe.GetFileInfo()->IsPE64);

	REQUIRE(pe.Open(file32.Path()));
	CHECK(pe.GetPath() == file32.Path());
	CHECK(pe.GetFileInfo()->IsPE32);
	CHECK_FALSE(pe.GetFileInfo()->IsPE64);
	CHECK(pe.GetImageBase() == spec32.ImageBase());

	SECTION("a failed Open closes the file that was open") {
		TempFile text(std::string("not a PE"));
		CHECK_FALSE(pe.Open(text.Path()));
		CHECK_FALSE(pe);
		CHECK(pe.GetSecHeaders()->empty());
	}
}

TEST_CASE("PEFile can be moved", "[open]") {
	TempFile file(SyntheticPE{}.Build(), L".dll");
	PEFile source;
	REQUIRE(source.Open(file.Path()));
	auto const size = source.GetFileSize();

	SECTION("move construction") {
		PEFile target(std::move(source));
		REQUIRE(target);
		CHECK(target.GetPath() == file.Path());
		CHECK(target.GetFileSize() == size);
		CHECK(target.GetSecHeaders()->size() == 3);
		CHECK(target.GetData()[0] == 'M');
	}
	SECTION("move assignment") {
		PEFile target;
		target = std::move(source);
		REQUIRE(target);
		CHECK(target.GetPath() == file.Path());
		CHECK(target.GetSecHeaders()->size() == 3);
	}
}

TEST_CASE("Raw file access", "[read]") {
	SyntheticPE spec;
	auto bytes = spec.Build();
	TempFile file(bytes, L".dll");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));
	auto const size = pe.GetFileSize();

	SECTION("Read copies bytes from the file") {
		char magic[2]{};
		REQUIRE(pe.Read(0, 2, magic));
		CHECK(magic[0] == 'M');
		CHECK(magic[1] == 'Z');

		BYTE code[4]{};
		REQUIRE(pe.Read(SyntheticPE::TextOffset, 4, code));
		CHECK(code[0] == 0xC3);
		CHECK(code[1] == 0xCC);
	}
	SECTION("typed Read") {
		CHECK(pe.Read<WORD>(0) == IMAGE_DOS_SIGNATURE);
		CHECK(pe.Read<DWORD>(SyntheticPE::ELfanew) == IMAGE_NT_SIGNATURE);
	}
	SECTION("Read at the very end of the file") {
		BYTE b = 0;
		CHECK(pe.Read(size - 1, 1, &b));
		CHECK_FALSE(pe.Read(size, 1, &b));
		CHECK_FALSE(pe.Read(size - 1, 2, &b));
	}
	SECTION("Read never overflows") {
		BYTE buffer[16]{};
		CHECK_FALSE(pe.Read(0xFFFFFFFF, 2, buffer));
		CHECK_FALSE(pe.Read(0xFFFFFFF0, 0x20, buffer));
		CHECK_FALSE(pe.Read(8, 0xFFFFFFFF, buffer));
	}
	SECTION("GetSpan and GetData agree with the file") {
		auto span = pe.GetSpan(SyntheticPE::DataOffset, 16);
		REQUIRE(span.size() == 16);
		for (size_t i = 0; i < span.size(); i++)
			CHECK(std::to_integer<uint8_t>(span[i]) == i);	// .data holds 0, 1, 2, ...

		auto whole = pe.GetSpan(0, size);
		CHECK(memcmp(whole.data(), bytes.data(), size) == 0);
		CHECK(memcmp(pe.GetData(), bytes.data(), size) == 0);
	}
}
