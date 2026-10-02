#include "TestCommon.h"
#include <Authenticode.h>
#include <fstream>
#include <bcrypt.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	Bytes Der(uint8_t tag, Bytes const& content) {
		Bytes b{ tag };
		if (content.size() < 0x80)
			b.push_back((uint8_t)content.size());
		else if (content.size() < 0x100) {
			b.push_back(0x81);
			b.push_back((uint8_t)content.size());
		}
		else {
			b.push_back(0x82);
			b.push_back((uint8_t)(content.size() >> 8));
			b.push_back((uint8_t)content.size());
		}
		b.insert(b.end(), content.begin(), content.end());
		return b;
	}

	Bytes Cat(std::initializer_list<Bytes> parts) {
		Bytes all;
		for (auto& p : parts)
			all.insert(all.end(), p.begin(), p.end());
		return all;
	}

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }

	const Bytes Sha256Oid{ 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01 };	// 2.16.840.1.101.3.4.2.1
	const Bytes Sha1Oid{ 0x2B, 0x0E, 0x03, 0x02, 0x1A };							// 1.3.14.3.2.26
	const Bytes SpcPeImageDataOid{ 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F };	// 1.3.6.1.4.1.311.2.1.15

	// SpcIndirectDataContent: { { type, value }, { { algorithm, NULL }, digest } }
	Bytes SpcIndirectData(Bytes const& algorithmOid, Bytes const& digest) {
		return Der(0x30, Cat({
			Der(0x30, Cat({ Der(0x06, SpcPeImageDataOid), Der(0x30, {}) })),
			Der(0x30, Cat({ Der(0x30, Cat({ Der(0x06, algorithmOid), Der(0x05, {}) })), Der(0x04, digest) })),
		}));
	}

	Bytes Digest(size_t size) {
		Bytes d(size);
		for (size_t i = 0; i < size; i++)
			d[i] = (uint8_t)(i * 7 + 1);
		return d;
	}

	// the Authenticode hash by the rules of the specification, from the headers of the file and not from our code
	Bytes ExpectedDigest(Bytes const& file, bool is64, PCWSTR algorithm, uint32_t certOffset = 0, uint32_t certSize = 0) {
		uint32_t lfanew;
		memcpy(&lfanew, file.data() + 0x3C, 4);
		size_t checksum = lfanew + (is64 ? offsetof(IMAGE_NT_HEADERS64, OptionalHeader.CheckSum) : offsetof(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum));
		size_t certEntry = lfanew + (is64 ? offsetof(IMAGE_NT_HEADERS64, OptionalHeader.DataDirectory) : offsetof(IMAGE_NT_HEADERS32, OptionalHeader.DataDirectory)) +
			IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
		Bytes kept;
		for (size_t i = 0; i < file.size(); i++) {
			if (i >= checksum && i < checksum + 4)
				continue;
			if (i >= certEntry && i < certEntry + 8)
				continue;
			if (certSize && i >= certOffset && i < (size_t)certOffset + certSize)
				continue;
			kept.push_back(file[i]);
		}
		return HashBytes(algorithm, AsBytes(kept));
	}

	void SetCertificate(SyntheticPE const& spec, Bytes& file, uint32_t offset, uint32_t size) {
		auto dir = reinterpret_cast<IMAGE_DATA_DIRECTORY*>(file.data() + spec.OptionalHeaderOffset() + (spec.Is64 ? 112 : 96)) + IMAGE_DIRECTORY_ENTRY_SECURITY;
		dir->VirtualAddress = offset;
		dir->Size = size;
	}

	Bytes Digest(Bytes const& file, PCWSTR algorithm, bool* ok = nullptr) {
		TempFile temp(file, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(temp.Path()));
		auto d = ComputeAuthenticodeDigest(pe, algorithm);
		if (ok)
			*ok = !d.empty();
		return d;
	}
}

TEST_CASE("Hashes of bytes", "[authenticode]") {
	std::string abc = "abc";
	auto bytes = std::as_bytes(std::span(abc));
	CHECK(BytesToHexString(HashBytes(BCRYPT_SHA1_ALGORITHM, bytes)) == L"A9993E364706816ABA3E25717850C26C9CD0D89D");
	CHECK(BytesToHexString(HashBytes(BCRYPT_MD5_ALGORITHM, bytes)) == L"900150983CD24FB0D6963F7D28E17F72");
	CHECK(HashBytes(BCRYPT_SHA256_ALGORITHM, bytes).size() == 32);
	CHECK(HashBytes(BCRYPT_SHA512_ALGORITHM, bytes).size() == 64);
	CHECK(HashBytes(L"NoSuchAlgorithm", bytes).empty());
}

TEST_CASE("The hash of a signature is read from SpcIndirectDataContent", "[authenticode]") {
	std::string oid;
	std::vector<uint8_t> digest;

	SECTION("SHA-256") {
		auto d = Digest(32);
		REQUIRE(ParseSpcIndirectData(AsBytes(SpcIndirectData(Sha256Oid, d)), oid, digest));
		CHECK(oid == "2.16.840.1.101.3.4.2.1");
		CHECK(digest == d);
		CHECK(DigestAlgorithmName(oid) == L"SHA-256");
		CHECK(std::wstring(DigestAlgorithmCng(oid)) == BCRYPT_SHA256_ALGORITHM);
	}
	SECTION("SHA-1") {
		auto d = Digest(20);
		REQUIRE(ParseSpcIndirectData(AsBytes(SpcIndirectData(Sha1Oid, d)), oid, digest));
		CHECK(oid == "1.3.14.3.2.26");
		CHECK(digest == d);
		CHECK(DigestAlgorithmName(oid) == L"SHA-1");
	}
	SECTION("a long digest needs a long form length") {
		auto d = Digest(200);
		REQUIRE(ParseSpcIndirectData(AsBytes(SpcIndirectData(Sha256Oid, d)), oid, digest));
		CHECK(digest == d);
	}
	SECTION("an algorithm that is not known") {
		REQUIRE(ParseSpcIndirectData(AsBytes(SpcIndirectData({ 0x2A, 0x03, 0x04 }, Digest(16))), oid, digest));
		CHECK(oid == "1.2.3.4");
		CHECK(DigestAlgorithmName(oid).empty());
		CHECK(DigestAlgorithmCng(oid) == nullptr);
	}
}

TEST_CASE("Content that is not SpcIndirectDataContent is rejected", "[authenticode]") {
	std::string oid;
	std::vector<uint8_t> digest;
	CHECK_FALSE(ParseSpcIndirectData({}, oid, digest));
	CHECK_FALSE(ParseSpcIndirectData(AsBytes(Bytes{ 0x04, 0x01, 0x00 }), oid, digest));	// an OCTET STRING

	auto good = SpcIndirectData(Sha256Oid, Digest(32));
	SECTION("every truncation") {
		for (size_t length = 0; length < good.size(); length++)
			CHECK_FALSE(ParseSpcIndirectData(AsBytes(good).first(length), oid, digest));
	}
	SECTION("every byte damaged") {
		for (size_t i = 0; i < good.size(); i++)
			for (uint8_t value : { (uint8_t)0x00, (uint8_t)0xFF, (uint8_t)0x30 }) {
				auto copy = good;
				copy[i] = value;
				ParseSpcIndirectData(AsBytes(copy), oid, digest);	// must not read outside
			}
	}
	SECTION("a length that promises more than there is") {
		auto bad = good;
		bad[1] = 0x84;
		CHECK_FALSE(ParseSpcIndirectData(AsBytes(bad), oid, digest));
	}
}

TEST_CASE("The time of a time stamp token", "[authenticode]") {
	auto text = [](std::string const& s) { return Bytes(s.begin(), s.end()); };
	auto tst = [&](std::string const& time) {
		return Der(0x30, Cat({ Der(0x02, { 1 }), Der(0x06, { 0x2A, 0x03 }), Der(0x30, Der(0x04, Digest(8))), Der(0x02, { 0x12, 0x34 }), Der(0x18, text(time)) }));
	};

	FILETIME ft{};
	REQUIRE(ParseTimestampInfo(AsBytes(tst("20240131123456Z")), ft));
	SYSTEMTIME st{};
	REQUIRE(::FileTimeToSystemTime(&ft, &st));
	CHECK(st.wYear == 2024);
	CHECK(st.wMonth == 1);
	CHECK(st.wDay == 31);
	CHECK(st.wHour == 12);
	CHECK(st.wMinute == 34);
	CHECK(st.wSecond == 56);

	REQUIRE(ParseTimestampInfo(AsBytes(tst("20240131123456.789Z")), ft));	// fractions of a second are ignored

	CHECK_FALSE(ParseTimestampInfo(AsBytes(tst("2024")), ft));
	CHECK_FALSE(ParseTimestampInfo(AsBytes(tst("2024AB31123456Z")), ft));
	CHECK_FALSE(ParseTimestampInfo(AsBytes(tst("20241331123456Z")), ft));	// month 13
	CHECK_FALSE(ParseTimestampInfo({}, ft));
	CHECK_FALSE(ParseTimestampInfo(AsBytes(Der(0x30, Der(0x02, { 1 }))), ft));	// no time in it
	auto good = tst("20240131123456Z");
	for (size_t length = 0; length < good.size(); length++)
		CHECK_FALSE(ParseTimestampInfo(AsBytes(good).first(length), ft));
}

TEST_CASE("The Authenticode hash of a file", "[authenticode]") {
	for (bool is64 : { true, false }) {
		SyntheticPE spec;
		spec.Is64 = is64;
		auto file = spec.Build();
		INFO(std::string(is64 ? "PE32+" : "PE32"));

		SECTION("an unsigned file") {
			CHECK(Digest(file, BCRYPT_SHA256_ALGORITHM) == ExpectedDigest(file, is64, BCRYPT_SHA256_ALGORITHM));
			CHECK(Digest(file, BCRYPT_SHA1_ALGORITHM) == ExpectedDigest(file, is64, BCRYPT_SHA1_ALGORITHM));
		}
		SECTION("the checksum and the certificate entry do not count") {
			auto before = Digest(file, BCRYPT_SHA256_ALGORITHM);
			auto changed = file;
			uint32_t lfanew;
			memcpy(&lfanew, changed.data() + 0x3C, 4);
			auto checksum = lfanew + (is64 ? offsetof(IMAGE_NT_HEADERS64, OptionalHeader.CheckSum) : offsetof(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum));
			changed[checksum] ^= 0xFF;
			CHECK(Digest(changed, BCRYPT_SHA256_ALGORITHM) == before);

			// a certificate table at the end: neither the entry nor the table counts
			auto signed_ = changed;
			signed_.insert(signed_.end(), 0x80, 0xCD);
			SetCertificate(spec, signed_, SyntheticPE::FileSize, 0x80);
			CHECK(Digest(signed_, BCRYPT_SHA256_ALGORITHM) == before);
			CHECK(Digest(signed_, BCRYPT_SHA256_ALGORITHM) == ExpectedDigest(signed_, is64, BCRYPT_SHA256_ALGORITHM, SyntheticPE::FileSize, 0x80));
		}
		SECTION("everything else does") {
			auto before = Digest(file, BCRYPT_SHA256_ALGORITHM);
			for (size_t at : { (size_t)0x44, (size_t)0x70, (size_t)SyntheticPE::TextOffset, (size_t)SyntheticPE::DataOffset + 5, (size_t)SyntheticPE::FileSize - 1 }) {
				auto changed = file;
				changed[at] ^= 1;
				CHECK(Digest(changed, BCRYPT_SHA256_ALGORITHM) != before);
			}
		}
		SECTION("an overlay counts") {
			auto before = Digest(file, BCRYPT_SHA256_ALGORITHM);
			auto withOverlay = file;
			withOverlay.insert(withOverlay.end(), { 1, 2, 3, 4, 5 });
			auto after = Digest(withOverlay, BCRYPT_SHA256_ALGORITHM);
			CHECK(after != before);
			CHECK(after == ExpectedDigest(withOverlay, is64, BCRYPT_SHA256_ALGORITHM));
		}
		SECTION("an overlay in front of the certificate table counts, the table does not") {
			auto signed_ = file;
			signed_.insert(signed_.end(), { 1, 2, 3, 4, 5, 6, 7, 8 });
			signed_.insert(signed_.end(), 0x40, 0xEE);
			SetCertificate(spec, signed_, SyntheticPE::FileSize + 8, 0x40);
			CHECK(Digest(signed_, BCRYPT_SHA256_ALGORITHM) == ExpectedDigest(signed_, is64, BCRYPT_SHA256_ALGORITHM, SyntheticPE::FileSize + 8, 0x40));
		}
	}

	SECTION("an unknown algorithm") {
		bool ok = true;
		SyntheticPE spec;
		Digest(spec.Build(), L"NoSuchAlgorithm", &ok);
		CHECK_FALSE(ok);
	}
}

TEST_CASE("Data that is not a signature", "[authenticode]") {
	auto sig = AnalyzeSignature({}, nullptr);
	CHECK_FALSE(sig.Decoded);
	CHECK_FALSE(sig.Error.empty());
	CHECK_FALSE(sig.DigestMatches());

	Bytes junk = Digest(300);
	sig = AnalyzeSignature(AsBytes(junk), nullptr);
	CHECK_FALSE(sig.Decoded);

	auto rows = DescribeSignature(sig);
	REQUIRE(rows.size() == 1);
	CHECK(rows[0].first == L"Note");
}

TEST_CASE("A file without a signature", "[authenticode]") {
	SyntheticPE spec;
	TempFile temp(spec.Build(), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(temp.Path()));
	auto result = AnalyzeAuthenticode(pe);
	CHECK(result.Signatures.empty());
	CHECK(result.Trust.empty());
}

TEST_CASE("Signatures are described as rows", "[authenticode]") {
	SignatureSummary sig;
	sig.Decoded = true;
	sig.DigestAlgorithm = L"SHA-256";
	sig.SignedDigest = { 0xAB, 0xCD };
	sig.ComputedDigest = { 0xAB, 0xCD };
	sig.HasSigner = true;
	sig.Signer.Subject = L"Contoso";
	sig.Signer.Issuer = L"Contoso CA";
	sig.Signer.SerialNumber = L"01";
	sig.Signer.Thumbprint = L"ABCD";
	TimestampSummary ts;
	ts.Kind = L"RFC 3161";
	ts.Signer = L"Time Authority";
	sig.Timestamps.push_back(ts);
	sig.Certificates.push_back(sig.Signer);
	sig.Nested.push_back(sig);

	auto rows = DescribeSignature(sig);
	auto find = [&](std::wstring const& name) -> std::wstring const* {
		for (auto& r : rows)
			if (r.first == name)
				return &r.second;
		return nullptr;
	};
	REQUIRE(find(L"Digest Algorithm"));
	CHECK(*find(L"Digest Algorithm") == L"SHA-256");
	CHECK(*find(L"Digest in Signature") == L"ABCD");
	CHECK(*find(L"Digest Check") == L"The digests match");
	CHECK(*find(L"Subject") == L"Contoso");
	REQUIRE(find(L"Timestamp (RFC 3161)"));
	CHECK(find(L"Timestamp (RFC 3161)")->find(L"Time Authority") != std::wstring::npos);
	CHECK(find(L"Certificate 1"));
	CHECK(find(L"Nested 1: Digest Algorithm"));	// a nested signature has its own rows

	sig.ComputedDigest = { 0x00 };
	rows = DescribeSignature(sig);
	CHECK(find(L"Digest Check")->find(L"DO NOT match") != std::wstring::npos);
}

namespace {
	// a system file with an embedded signature, or an empty path
	std::wstring FindSignedFile() {
		WCHAR dir[MAX_PATH];
		::GetSystemDirectoryW(dir, MAX_PATH);
		for (auto name : { L"appverif.exe", L"MRT.exe", L"MpSigStub.exe", L"bootre.exe", L"vmnat.exe" }) {
			auto path = std::wstring(dir) + L"\\" + name;
			PEFile pe;
			if (pe.Open(path) && !pe.GetSecurity()->empty())
				return path;
		}
		return L"";
	}
}

TEST_CASE("The signature of a system file", "[authenticode][system]") {
	auto path = FindSignedFile();
	if (path.empty())
		SKIP("no system file with an embedded signature was found");
	PEFile pe;
	REQUIRE(pe.Open(path));

	auto result = AnalyzeAuthenticode(pe);
	REQUIRE_FALSE(result.Signatures.empty());
	auto& sig = result.Signatures[0];
	INFO(Narrow(path));
	REQUIRE(sig.Decoded);
	CHECK_FALSE(sig.DigestAlgorithm.empty());
	CHECK_FALSE(sig.SignedDigest.empty());
	CHECK(sig.DigestMatches());
	REQUIRE(sig.HasSigner);
	CHECK(sig.Signer.Subject.find(L"Microsoft") != std::wstring::npos);
	CHECK(sig.Certificates.size() >= 2);
	CHECK_FALSE(sig.Signer.Thumbprint.empty());
	CHECK(result.TrustCode == 0);
	CHECK_FALSE(result.Trust.empty());

	// Microsoft signs with time stamps
	bool anyTimestamp = !sig.Timestamps.empty();
	for (auto& n : sig.Nested)
		anyTimestamp = anyTimestamp || !n.Timestamps.empty();
	CHECK(anyTimestamp);
	for (auto& n : sig.Nested)
		CHECK(n.DigestMatches());
}

TEST_CASE("A signed file that was changed", "[authenticode][system]") {
	auto path = FindSignedFile();
	if (path.empty())
		SKIP("no system file with an embedded signature was found");

	// the file with a byte changed in the first section
	std::ifstream in(path, std::ios::binary);
	Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	PEFile original;
	REQUIRE(original.Open(path));
	uint32_t at = 0;
	for (auto& s : *original.GetSecHeaders())
		if (s.SecHdr.SizeOfRawData >= 16) {
			at = s.SecHdr.PointerToRawData + 8;
			break;
		}
	REQUIRE(at != 0);
	bytes[at] ^= 0x55;

	TempFile temp(bytes, L".exe");
	PEFile pe;
	REQUIRE(pe.Open(temp.Path()));
	auto result = AnalyzeAuthenticode(pe);
	REQUIRE_FALSE(result.Signatures.empty());
	CHECK_FALSE(result.Signatures[0].DigestMatches());
	CHECK((uint32_t)result.TrustCode == 0x80096010);	// TRUST_E_BAD_DIGEST
	CHECK(result.Trust.find(L"changed") != std::wstring::npos);
}
