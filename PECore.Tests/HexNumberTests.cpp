#include "TestCommon.h"
#include <HexNumber.h>

namespace {
	bool Parse(std::wstring const& text, uint64_t& value) {
		value = 0xDEADBEEF;
		return ParseHexNumber(text, value);
	}
}

TEST_CASE("Hexadecimal numbers are parsed", "[hexnumber]") {
	uint64_t v;

	SECTION("plain digits") {
		REQUIRE(Parse(L"1A40", v));
		CHECK(v == 0x1A40);
		REQUIRE(Parse(L"0", v));
		CHECK(v == 0);
		REQUIRE(Parse(L"ff", v));
		CHECK(v == 0xFF);
	}
	SECTION("a 0x prefix or an h suffix") {
		REQUIRE(Parse(L"0x1A40", v));
		CHECK(v == 0x1A40);
		REQUIRE(Parse(L"0X1a40", v));
		CHECK(v == 0x1A40);
		REQUIRE(Parse(L"1A40h", v));
		CHECK(v == 0x1A40);
		REQUIRE(Parse(L"1A40H", v));
		CHECK(v == 0x1A40);
	}
	SECTION("surrounding white space is ignored") {
		REQUIRE(Parse(L"  0x10 \t", v));
		CHECK(v == 0x10);
	}
	SECTION("64 bit values") {
		REQUIRE(Parse(L"140001A40", v));
		CHECK(v == 0x140001A40ULL);
		REQUIRE(Parse(L"FFFFFFFFFFFFFFFF", v));
		CHECK(v == 0xFFFFFFFFFFFFFFFFULL);
		REQUIRE(Parse(L"0x7FFFFFFFFFFFFFFF", v));
		CHECK(v == 0x7FFFFFFFFFFFFFFFULL);
	}
	SECTION("a leading zero does not make it octal") {
		REQUIRE(Parse(L"0100", v));
		CHECK(v == 0x100);
	}
}

TEST_CASE("Things that are not hexadecimal numbers are rejected", "[hexnumber]") {
	uint64_t v;
	for (auto text : { L"", L"   ", L"0x", L"h", L"xyz", L"12G4", L"-1", L"+1", L"1 2", L"0x0x10", L"1.5", L"10000000000000000", L"0x10000000000000000" }) {
		CAPTURE(Narrow(text));
		CHECK_FALSE(Parse(text, v));
	}
}
