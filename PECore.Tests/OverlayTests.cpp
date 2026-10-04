#include "TestCommon.h"
#include <Overlay.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	OverlayInfo Analyze(Bytes const& file) {
		TempFile temp(file, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(temp.Path()));
		return FindOverlay(pe);
	}

	void SetCertificateDirectory(SyntheticPE const& spec, Bytes& file, uint32_t offset, uint32_t size) {
		auto dir = reinterpret_cast<IMAGE_DATA_DIRECTORY*>(file.data() + spec.OptionalHeaderOffset() + (spec.Is64 ? 112 : 96)) + IMAGE_DIRECTORY_ENTRY_SECURITY;
		dir->VirtualAddress = offset;
		dir->Size = size;
	}

	Bytes Append(Bytes file, Bytes const& data) {
		file.insert(file.end(), data.begin(), data.end());
		return file;
	}

	Bytes Filled(size_t size, uint8_t value) { return Bytes(size, value); }

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }
}

TEST_CASE("A file without an overlay", "[overlay]") {
	SyntheticPE spec;
	auto info = Analyze(spec.Build());
	CHECK(info.Empty());
	CHECK(info.Size == 0);
}

TEST_CASE("Data after the last section is the overlay", "[overlay]") {
	for (bool is64 : { true, false }) {
		SyntheticPE spec;
		spec.Is64 = is64;
		Bytes data{ 'P', 'K', 3, 4, 0, 0, 0, 0, 1, 2, 3, 4 };
		auto info = Analyze(Append(spec.Build(), data));
		CHECK_FALSE(info.Empty());
		CHECK(info.Offset == SyntheticPE::FileSize);
		CHECK(info.Size == data.size());
		CHECK(info.Kind == L"ZIP archive");
		CHECK(info.CertificateSize == 0);
	}
}

TEST_CASE("The certificate table is not the overlay", "[overlay]") {
	SyntheticPE spec;
	auto file = spec.Build();

	SECTION("a certificate table at the end") {
		auto signed_ = Append(file, Filled(0x100, 0xAB));
		SetCertificateDirectory(spec, signed_, SyntheticPE::FileSize, 0x100);
		auto info = Analyze(signed_);
		CHECK(info.Empty());
		CHECK(info.CertificateOffset == SyntheticPE::FileSize);
		CHECK(info.CertificateSize == 0x100);
	}
	SECTION("an overlay in front of the certificate table") {
		auto signed_ = Append(Append(file, Bytes{ 'M', 'Z', 0, 0, 0, 0, 0, 0 }), Filled(0x100, 0xAB));
		SetCertificateDirectory(spec, signed_, SyntheticPE::FileSize + 8, 0x100);
		auto info = Analyze(signed_);
		CHECK(info.Offset == SyntheticPE::FileSize);
		CHECK(info.Size == 8);
		CHECK(info.Kind == L"MZ executable");
		CHECK(info.CertificateOffset == SyntheticPE::FileSize + 8);
	}
	SECTION("an overlay behind the certificate table") {
		auto signed_ = Append(Append(file, Filled(0x100, 0xAB)), Bytes{ '%', 'P', 'D', 'F', '-', '1', '.', '7' });
		SetCertificateDirectory(spec, signed_, SyntheticPE::FileSize, 0x100);
		auto info = Analyze(signed_);
		CHECK(info.Offset == SyntheticPE::FileSize + 0x100);
		CHECK(info.Size == 8);
		CHECK(info.Kind == L"PDF document");
	}
	SECTION("a certificate table that is not in the file is ignored") {
		auto broken = Append(file, Filled(0x40, 1));
		SetCertificateDirectory(spec, broken, SyntheticPE::FileSize + 0x20, 0x1000);
		auto info = Analyze(broken);
		CHECK(info.Size == 0x40);
		CHECK(info.CertificateSize == 0);
	}
}

TEST_CASE("The entropy of the overlay", "[overlay]") {
	SyntheticPE spec;
	CHECK(Analyze(Append(spec.Build(), Filled(256, 0))).Entropy == Catch::Approx(0.0));

	Bytes all(256);
	for (int i = 0; i < 256; i++)
		all[i] = (uint8_t)i;
	CHECK(Analyze(Append(spec.Build(), all)).Entropy == Catch::Approx(8.0));
}

TEST_CASE("Data is identified by its first bytes", "[overlay]") {
	auto id = [](Bytes b) { b.resize(std::max<size_t>(b.size(), 64), 0xFF); return IdentifyData(std::as_bytes(std::span(b))); };

	CHECK(id({ 'P', 'K', 3, 4 }) == L"ZIP archive");
	CHECK(id({ '7', 'z', 0xBC, 0xAF, 0x27, 0x1C }) == L"7-Zip archive");
	CHECK(id({ 'R', 'a', 'r', '!', 0x1A, 0x07, 0 }) == L"RAR archive");
	CHECK(id({ 'M', 'S', 'C', 'F' }) == L"Cabinet (CAB) archive");
	CHECK(id({ 0x1F, 0x8B, 8 }) == L"gzip data");
	CHECK(id({ 'B', 'Z', 'h', '9' }) == L"bzip2 data");
	CHECK(id({ 0xFD, '7', 'z', 'X', 'Z', 0 }) == L"XZ data");
	CHECK(id({ 0x28, 0xB5, 0x2F, 0xFD }) == L"Zstandard data");
	CHECK(id({ 0x89, 'P', 'N', 'G' }) == L"PNG image");
	CHECK(id({ 0xFF, 0xD8, 0xFF, 0xE0 }) == L"JPEG image");
	CHECK(id({ '<', '?', 'x', 'm', 'l', ' ' }) == L"XML text");
	CHECK(id({ 'M', 'Z' }) == L"MZ executable");
	CHECK(id({ 0x30, 0x82, 1, 2 }) == L"ASN.1 data (a certificate or a signature, perhaps)");
	CHECK(id({ 0, 0, 0, 0, 0xEF, 0xBE, 0xAD, 0xDE, 'N', 'u', 'l', 'l', 's', 'o', 'f', 't', 'I', 'n', 's', 't' }) == L"NSIS installer data");

	SECTION("a PE file is told from other MZ data") {
		Bytes pe(0x100, 0);
		pe[0] = 'M'; pe[1] = 'Z'; pe[0x3C] = 0x80;
		pe[0x80] = 'P'; pe[0x81] = 'E';
		CHECK(IdentifyData(AsBytes(pe)) == L"PE file");
	}
	SECTION("text") {
		std::string text = "just some plain text here";
		CHECK(IdentifyData(std::as_bytes(std::span(text))) == L"Text");
	}
	SECTION("what is not recognized") {
		CHECK(IdentifyData({}).empty());
		CHECK(IdentifyData(AsBytes(Bytes{ 1 })).empty());
		CHECK(IdentifyData(AsBytes(Bytes{ 0x01, 0x02, 0x03, 0x04, 0x80, 0x90 })).empty());
	}
}

TEST_CASE("SHA-256 of bytes", "[overlay]") {
	std::string abc = "abc";
	CHECK(Sha256Hex(std::as_bytes(std::span(abc))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(Sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA-1 of bytes", "[overlay]") {
	std::string abc = "abc";
	CHECK(Sha1Hex(std::as_bytes(std::span(abc))) == "a9993e364706816aba3e25717850c26c9cd0d89d");
	CHECK(Sha1Hex({}) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}
