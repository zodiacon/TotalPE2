#include "TestCommon.h"
#include <HexSearch.h>

namespace {
	HexPattern Build(HexSearchType type, std::wstring const& text, bool matchCase = true) {
		HexFindOptions options;
		options.Text = text;
		options.Type = type;
		options.MatchCase = matchCase;
		HexPattern pattern;
		std::wstring error;
		REQUIRE(HexSearch::BuildPattern(options, pattern, error));
		return pattern;
	}

	std::wstring BuildError(HexSearchType type, std::wstring const& text) {
		HexFindOptions options;
		options.Text = text;
		options.Type = type;
		HexPattern pattern;
		std::wstring error;
		REQUIRE_FALSE(HexSearch::BuildPattern(options, pattern, error));
		CHECK(pattern.empty());
		return error;
	}

	int64_t Find(std::vector<uint8_t> const& data, HexPattern const& p, int64_t start, bool forward = true) {
		return HexSearch::Find(data.data(), (int64_t)data.size(), p, start, forward);
	}

	std::vector<uint8_t> Bytes(std::string const& s) {
		return { s.begin(), s.end() };
	}
}

TEST_CASE("Hex patterns are parsed", "[search]") {
	SECTION("plain bytes") {
		auto p = Build(HexSearchType::Hex, L"4D 5A 90");
		CHECK(p.Bytes == std::vector<uint8_t>{ 0x4D, 0x5A, 0x90 });
		CHECK(p.Mask == std::vector<uint8_t>{ 0xFF, 0xFF, 0xFF });
	}
	SECTION("separators and prefixes are ignored") {
		CHECK(Build(HexSearchType::Hex, L"4d5a90").Bytes == std::vector<uint8_t>{ 0x4D, 0x5A, 0x90 });
		CHECK(Build(HexSearchType::Hex, L"0x4D, 0x5A\t0x90").Bytes == std::vector<uint8_t>{ 0x4D, 0x5A, 0x90 });
		CHECK(Build(HexSearchType::Hex, L"  de ad  be ef ").Bytes == std::vector<uint8_t>{ 0xDE, 0xAD, 0xBE, 0xEF });
	}
	SECTION("whole byte wildcards") {
		auto p = Build(HexSearchType::Hex, L"4D ?? 90");
		CHECK(p.Bytes.size() == 3);
		CHECK(p.Mask == std::vector<uint8_t>{ 0xFF, 0x00, 0xFF });
	}
	SECTION("nibble wildcards") {
		auto p = Build(HexSearchType::Hex, L"4? ?A");
		CHECK(p.Bytes == std::vector<uint8_t>{ 0x40, 0x0A });
		CHECK(p.Mask == std::vector<uint8_t>{ 0xF0, 0x0F });
	}
}

TEST_CASE("Invalid hex patterns are rejected with an explanation", "[search]") {
	CHECK_FALSE(BuildError(HexSearchType::Hex, L"").empty());
	CHECK_FALSE(BuildError(HexSearchType::Hex, L"4D 5").empty());		// odd number of digits
	CHECK_FALSE(BuildError(HexSearchType::Hex, L"4D ZZ").empty());		// not hex
	CHECK_FALSE(BuildError(HexSearchType::Hex, L"?? ?? ??").empty());	// would match everything
	CHECK_FALSE(BuildError(HexSearchType::Ascii, L"").empty());
}

TEST_CASE("Text patterns", "[search]") {
	SECTION("ASCII is UTF-8") {
		CHECK(Build(HexSearchType::Ascii, L"MZ").Bytes == std::vector<uint8_t>{ 'M', 'Z' });
		CHECK(Build(HexSearchType::Ascii, L"\x00E9").Bytes == std::vector<uint8_t>{ 0xC3, 0xA9 });
	}
	SECTION("Unicode is UTF-16 little endian") {
		CHECK(Build(HexSearchType::Unicode, L"AB").Bytes == std::vector<uint8_t>{ 'A', 0, 'B', 0 });
		CHECK(Build(HexSearchType::Unicode, L"\x20AC").Bytes == std::vector<uint8_t>{ 0xAC, 0x20 });
	}
	SECTION("letters are marked for case folding unless case matters") {
		auto folded = Build(HexSearchType::Ascii, L"a1", false);
		CHECK(folded.Fold == std::vector<uint8_t>{ 1, 0 });
		auto exact = Build(HexSearchType::Ascii, L"a1", true);
		CHECK(exact.Fold == std::vector<uint8_t>{ 0, 0 });
	}
}

TEST_CASE("Forward search", "[search]") {
	auto data = Bytes("..MZ....MZ..MZ");
	auto mz = Build(HexSearchType::Ascii, L"MZ");

	CHECK(Find(data, mz, 0) == 2);
	CHECK(Find(data, mz, 2) == 2);			// the start is inclusive
	CHECK(Find(data, mz, 3) == 8);
	CHECK(Find(data, mz, 9) == 12);
	CHECK(Find(data, mz, 13) == -1);
	CHECK(Find(data, mz, -5) == 2);			// a negative start means the beginning
	CHECK(Find(data, mz, 1000) == -1);
}

TEST_CASE("Backward search", "[search]") {
	auto data = Bytes("..MZ....MZ..MZ");
	auto mz = Build(HexSearchType::Ascii, L"MZ");

	CHECK(Find(data, mz, 100, false) == 12);	// a start beyond the end searches from the last possible position
	CHECK(Find(data, mz, 12, false) == 12);
	CHECK(Find(data, mz, 11, false) == 8);
	CHECK(Find(data, mz, 7, false) == 2);
	CHECK(Find(data, mz, 1, false) == -1);
}

TEST_CASE("Matches at the edges of the data", "[search]") {
	auto data = Bytes("MZ....MZ");
	auto mz = Build(HexSearchType::Ascii, L"MZ");
	CHECK(Find(data, mz, 0) == 0);
	CHECK(Find(data, mz, 1) == 6);			// the last two bytes
	CHECK(Find(data, mz, 0, false) == 0);

	// a pattern longer than the data
	CHECK(Find(Bytes("M"), mz, 0) == -1);
	CHECK(Find({}, mz, 0) == -1);
}

TEST_CASE("Wildcards match any value", "[search]") {
	std::vector<uint8_t> data{ 0x00, 0x4D, 0x11, 0x90, 0x4D, 0x22, 0x90, 0x4D, 0x22, 0x91 };
	auto p = Build(HexSearchType::Hex, L"4D ?? 90");

	CHECK(Find(data, p, 0) == 1);
	CHECK(Find(data, p, 2) == 4);
	CHECK(Find(data, p, 5) == -1);

	auto nibble = Build(HexSearchType::Hex, L"9?");
	CHECK(Find(data, nibble, 0) == 3);
	CHECK(Find(data, nibble, 4) == 6);
	CHECK(Find(data, nibble, 7) == 9);
	CHECK(Find(data, nibble, 0, false) == -1);	// nothing at or before offset 0
}

TEST_CASE("Case folding", "[search]") {
	auto data = Bytes("..Hello World..");

	CHECK(Find(data, Build(HexSearchType::Ascii, L"hello", false), 0) == 2);
	CHECK(Find(data, Build(HexSearchType::Ascii, L"HELLO WORLD", false), 0) == 2);
	CHECK(Find(data, Build(HexSearchType::Ascii, L"hello", true), 0) == -1);
	CHECK(Find(data, Build(HexSearchType::Ascii, L"Hello", true), 0) == 2);

	// digits and punctuation are not affected by folding: '0' (0x30) vs 'P' (0x50) differ by 0x20 only in a letter's bit
	CHECK(Find(Bytes("P"), Build(HexSearchType::Ascii, L"0", false), 0) == -1);
	CHECK(Find(Bytes("@"), Build(HexSearchType::Ascii, L"`", false), 0) == -1);
}

TEST_CASE("UTF-16 search", "[search]") {
	std::vector<uint8_t> data{ 'x', 0, 'H', 0, 'i', 0, 0, 0 };
	CHECK(Find(data, Build(HexSearchType::Unicode, L"Hi"), 0) == 2);
	CHECK(Find(data, Build(HexSearchType::Unicode, L"hI", false), 0) == 2);
	CHECK(Find(data, Build(HexSearchType::Unicode, L"hI", true), 0) == -1);
	// the ASCII bytes alone are not the UTF-16 string
	CHECK(Find(Bytes("Hi"), Build(HexSearchType::Unicode, L"Hi"), 0) == -1);
}

TEST_CASE("A large buffer is searched correctly", "[search]") {
	std::vector<uint8_t> data(1 << 20, 0xAA);
	data[(1 << 20) - 3] = 0xDE;
	data[(1 << 20) - 2] = 0xAD;
	data[(1 << 20) - 1] = 0xBE;
	auto p = Build(HexSearchType::Hex, L"DE AD BE");
	CHECK(Find(data, p, 0) == (1 << 20) - 3);
	CHECK(Find(data, p, 0, false) == -1);
	CHECK(Find(data, p, (1 << 20), false) == (1 << 20) - 3);
}
