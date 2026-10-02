#include "TestCommon.h"
#include <HexFormat.h>

namespace {
	std::vector<uint8_t> Seq(size_t n) {
		std::vector<uint8_t> v(n);
		for (size_t i = 0; i < n; i++)
			v[i] = (uint8_t)i;
		return v;
	}
}

TEST_CASE("Hex strings", "[format]") {
	std::vector<uint8_t> data{ 0x4D, 0x5A, 0x00, 0xFF };
	CHECK(HexFormat::ToHexString(data, true) == L"4D 5A 00 FF");
	CHECK(HexFormat::ToHexString(data, false) == L"4D5A00FF");
	CHECK(HexFormat::ToHexString({}, true).empty());
	CHECK(HexFormat::ToHexString(std::vector<uint8_t>{ 0x0A }, true) == L"0A");
}

TEST_CASE("C arrays", "[format]") {
	SECTION("a short array") {
		std::vector<uint8_t> data{ 1, 2, 0xAB };
		CHECK(HexFormat::ToCArray(data) == L"unsigned char data[3] = {\r\n    0x01, 0x02, 0xAB\r\n};");
	}
	SECTION("16 bytes per row, no trailing comma") {
		auto text = HexFormat::ToCArray(Seq(17));
		CHECK(text.find(L"0x0F,\r\n    0x10\r\n};") != std::wstring::npos);
		CHECK(text.starts_with(L"unsigned char data[17] = {"));
	}
	SECTION("empty") {
		CHECK(HexFormat::ToCArray({}) == L"unsigned char data[0] = {\r\n};");
	}
}

TEST_CASE("C# arrays", "[format]") {
	std::vector<uint8_t> data{ 1, 2 };
	CHECK(HexFormat::ToCSharpArray(data) == L"byte[] data = new byte[] {\r\n    0x01, 0x02\r\n};");
}

TEST_CASE("Python bytes", "[format]") {
	CHECK(HexFormat::ToPythonBytes({}) == L"data = b\"\"");
	CHECK(HexFormat::ToPythonBytes(std::vector<uint8_t>{ 0x4D, 0x5A }) == L"data = (\r\n    b\"\\x4d\\x5a\"\r\n)");

	auto text = HexFormat::ToPythonBytes(Seq(17));
	CHECK(text.find(L"\\x0f\"\r\n    b\"\\x10\"") != std::wstring::npos);	// a new literal after 16 bytes
}

TEST_CASE("Base64", "[format]") {
	auto b64 = [](std::string const& s) {
		return HexFormat::ToBase64(std::span<const uint8_t>((const uint8_t*)s.data(), s.size()));
	};
	// the test vectors of RFC 4648
	CHECK(b64("") == L"");
	CHECK(b64("f") == L"Zg==");
	CHECK(b64("fo") == L"Zm8=");
	CHECK(b64("foo") == L"Zm9v");
	CHECK(b64("foob") == L"Zm9vYg==");
	CHECK(b64("fooba") == L"Zm9vYmE=");
	CHECK(b64("foobar") == L"Zm9vYmFy");

	// every byte value, including the characters that differ between alphabets
	std::vector<uint8_t> data{ 0xFB, 0xFF, 0xBF };
	CHECK(HexFormat::ToBase64(data) == L"+/+/");
}
