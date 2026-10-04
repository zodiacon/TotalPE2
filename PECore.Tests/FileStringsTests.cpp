#include "TestCommon.h"
#include <FileStrings.h>
#include <algorithm>

namespace {
	using Bytes = std::vector<uint8_t>;

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }

	void AppendAscii(Bytes& b, std::string_view s) {
		b.insert(b.end(), s.begin(), s.end());
	}

	void AppendUtf16(Bytes& b, std::string_view s) {
		for (auto ch : s) {
			b.push_back((uint8_t)ch);
			b.push_back(0);
		}
	}
}

TEST_CASE("ASCII strings are found with their offsets", "[strings]") {
	Bytes data{ 0, 1, 2 };
	AppendAscii(data, "Hello, World");
	data.push_back(0);
	data.push_back(0xFF);
	AppendAscii(data, "kernel32.dll");

	auto strings = FindStrings(AsBytes(data));
	REQUIRE(strings.size() == 2);
	CHECK(strings[0].Offset == 3);
	CHECK(strings[0].Length == 12);
	CHECK(strings[0].Encoding == StringEncoding::Ascii);
	CHECK(strings[0].Text == L"Hello, World");
	CHECK(strings[1].Offset == 17);
	CHECK(strings[1].Text == L"kernel32.dll");	// a string at the very end of the data
}

TEST_CASE("Strings shorter than the minimum are skipped", "[strings]") {
	Bytes data;
	AppendAscii(data, "abcd");
	data.push_back(0);
	AppendAscii(data, "abcde");
	data.push_back(0xFF);	// not a zero: "e" and a zero would start a UTF-16 string
	AppendUtf16(data, "wxyz");
	data.push_back(0xFF);
	data.push_back(0xFF);

	auto strings = FindStrings(AsBytes(data));
	REQUIRE(strings.size() == 1);
	CHECK(strings[0].Text == L"abcde");

	StringScanOptions options;
	options.MinLength = 4;
	CHECK(FindStrings(AsBytes(data), options).size() == 3);
}

TEST_CASE("UTF-16 strings are found at both alignments", "[strings]") {
	for (size_t padding : { 0, 1, 2, 3 }) {
		Bytes data(padding, 0xFF);
		AppendUtf16(data, "Software\\Microsoft");
		data.push_back(0);
		data.push_back(0);

		auto strings = FindStrings(AsBytes(data));
		INFO("padding " << padding);
		REQUIRE(strings.size() == 1);
		CHECK(strings[0].Offset == padding);
		CHECK(strings[0].Encoding == StringEncoding::Utf16);
		CHECK(strings[0].Length == 18);
		CHECK(strings[0].Text == L"Software\\Microsoft");
	}
}

TEST_CASE("The encodings can be turned off", "[strings]") {
	Bytes data;
	AppendAscii(data, "an ascii string");
	data.push_back(0xFF);
	AppendUtf16(data, "a wide string");

	StringScanOptions options;
	options.Utf16 = false;
	auto ascii = FindStrings(AsBytes(data), options);
	REQUIRE(ascii.size() == 1);
	CHECK(ascii[0].Encoding == StringEncoding::Ascii);

	options.Utf16 = true;
	options.Ascii = false;
	auto wide = FindStrings(AsBytes(data), options);
	REQUIRE(wide.size() == 1);
	CHECK(wide[0].Encoding == StringEncoding::Utf16);
	CHECK(wide[0].Offset == 16);

	options.Ascii = true;
	auto both = FindStrings(AsBytes(data), options);
	REQUIRE(both.size() == 2);
	CHECK(both[0].Offset < both[1].Offset);
}

TEST_CASE("Long strings keep their length but not all of their text", "[strings]") {
	Bytes data(10000, 'A');
	auto strings = FindStrings(AsBytes(data));
	REQUIRE(strings.size() == 1);
	CHECK(strings[0].Length == 10000);
	CHECK(strings[0].Text.size() == StringScanOptions::MaxTextLength);
}

TEST_CASE("No strings in empty or binary data", "[strings]") {
	CHECK(FindStrings({}).empty());
	Bytes data{ 0x90, 0x90, 0xCC, 0x00, 0xFF, 0x41, 0x00, 0x01 };
	CHECK(FindStrings(AsBytes(data)).empty());
}

TEST_CASE("Strings in a real binary", "[strings][system]") {
	PEFile pe;
	REQUIRE(pe.Open(L"C:\\Windows\\System32\\kernel32.dll"));
	auto strings = FindStrings(pe.GetSpan(0, pe.GetFileSize()));
	CHECK(strings.size() > 100);
	auto has = [&](std::wstring_view text, StringEncoding enc) {
		return std::ranges::any_of(strings, [&](auto const& s) { return s.Encoding == enc && s.Text == text; });
	};
	CHECK(has(L"!This program cannot be run in DOS mode.", StringEncoding::Ascii));
	CHECK(has(L"CompanyName", StringEncoding::Utf16));	// in the version resource
}
