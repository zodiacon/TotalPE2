#include "TestCommon.h"
#include <VersionInfo.h>
#include "ByteWriter.h"

namespace {
	// {length, value length, type, key, padding, value, padding, children}: the nodes of a version resource.
	// A node starts at a multiple of 4, so the alignment can be done relative to the node.
	ByteWriter Node(std::wstring const& key, uint16_t type, uint16_t valueLength, std::vector<uint8_t> const& value = {}, std::vector<ByteWriter> const& children = {}) {
		ByteWriter w;
		w.U16(0).U16(valueLength).U16(type).Str(key).Align(4);
		w.Bytes.insert(w.Bytes.end(), value.begin(), value.end());
		for (auto& c : children) {
			w.Align(4);
			w.Bytes.insert(w.Bytes.end(), c.Bytes.begin(), c.Bytes.end());
		}
		w.Bytes[0] = (uint8_t)(w.Bytes.size() & 0xFF);
		w.Bytes[1] = (uint8_t)(w.Bytes.size() >> 8);
		return w;
	}

	ByteWriter Text(std::wstring const& key, std::wstring const& value) {
		ByteWriter v;
		v.Str(value);
		return Node(key, 1, (uint16_t)(value.size() + 1), v.Bytes);
	}

	std::vector<uint8_t> FixedInfo() {
		ByteWriter f;
		f.U32(0xFEEF04BD).U32(0x00010000).U32(0x00010002).U32(0x00030004).U32(0x00050006).U32(0x00070008);
		f.U32(0x3F).U32(VS_FF_DEBUG).U32(VOS_NT_WINDOWS32).U32(VFT_DLL).U32(0).U32(0).U32(0);
		return f.Bytes;
	}

	ByteWriter FullResource() {
		auto strings = Node(L"StringFileInfo", 1, 0, {}, {
			Node(L"040904B0", 1, 0, {}, {
				Text(L"CompanyName", L"Contoso"),
				Text(L"FileDescription", L"A test"),
				Text(L"ProductName", L""),
			}),
			Node(L"040704E4", 1, 0, {}, {
				Text(L"CompanyName", L"Contoso AG"),
			}),
		});
		ByteWriter translation;
		translation.U16(0x0409).U16(0x04B0).U16(0x0407).U16(0x04E4);
		auto vars = Node(L"VarFileInfo", 1, 0, {}, { Node(L"Translation", 0, 8, translation.Bytes) });
		return Node(L"VS_VERSION_INFO", 0, (uint16_t)sizeof(VS_FIXEDFILEINFO), FixedInfo(), { strings, vars });
	}
}

TEST_CASE("A version resource is parsed", "[version]") {
	auto res = FullResource();
	auto info = ParseVersionInfo(res.Span());
	REQUIRE(info.Valid);

	SECTION("the fixed information") {
		REQUIRE(info.HasFixedInfo);
		CHECK(info.Fixed.dwFileVersionMS == 0x00010002);
		CHECK(info.Fixed.dwFileVersionLS == 0x00030004);
		CHECK(info.Fixed.dwProductVersionMS == 0x00050006);
		CHECK(info.Fixed.dwFileFlags == VS_FF_DEBUG);
		CHECK(info.Fixed.dwFileType == VFT_DLL);
	}
	SECTION("the string tables") {
		REQUIRE(info.Tables.size() == 2);
		CHECK(info.Tables[0].LangCodePage == L"040904B0");
		CHECK(info.Tables[0].Language == 0x0409);
		CHECK(info.Tables[0].CodePage == 0x04B0);
		REQUIRE(info.Tables[0].Strings.size() == 3);
		CHECK(info.Tables[0].Strings[0] == std::pair<std::wstring, std::wstring>{ L"CompanyName", L"Contoso" });
		CHECK(info.Tables[0].Strings[2] == std::pair<std::wstring, std::wstring>{ L"ProductName", L"" });
		CHECK(info.Tables[1].Language == 0x0407);
		CHECK(info.Tables[1].CodePage == 0x04E4);
	}
	SECTION("looking up strings") {
		CHECK(info.GetString(L"CompanyName") == L"Contoso");			// the first table
		CHECK(info.GetString(L"FileDescription") == L"A test");
		CHECK(info.GetString(L"CompanyName", L"040704E4") == L"Contoso AG");
		CHECK(info.GetString(L"CompanyName", L"040704e4") == L"Contoso AG");	// the key is hexadecimal
		CHECK(info.GetString(L"FileDescription", L"040704E4") == L"");			// not in that table
		CHECK(info.GetString(L"Missing") == L"");
	}
	SECTION("the translations") {
		REQUIRE(info.Translations.size() == 2);
		CHECK(info.Translations[0].Language == 0x0409);
		CHECK(info.Translations[0].CodePage == 0x04B0);
		CHECK(info.Translations[1].Language == 0x0407);
		CHECK(info.Translations[1].CodePage == 0x04E4);
	}
}

TEST_CASE("A version resource without fixed information", "[version]") {
	auto res = Node(L"VS_VERSION_INFO", 0, 0, {}, { Node(L"StringFileInfo", 1, 0, {}, { Node(L"040904B0", 1, 0, {}, { Text(L"Comments", L"x") }) }) });
	auto info = ParseVersionInfo(res.Span());
	REQUIRE(info.Valid);
	CHECK_FALSE(info.HasFixedInfo);
	CHECK(info.GetString(L"Comments") == L"x");
}

TEST_CASE("A fixed information block with the wrong signature is ignored", "[version]") {
	auto fixed = FixedInfo();
	fixed[0] = 0;
	auto res = Node(L"VS_VERSION_INFO", 0, (uint16_t)sizeof(VS_FIXEDFILEINFO), fixed);
	auto info = ParseVersionInfo(res.Span());
	CHECK(info.Valid);
	CHECK_FALSE(info.HasFixedInfo);
}

TEST_CASE("Data that is not a version resource", "[version]") {
	CHECK_FALSE(ParseVersionInfo({}).Valid);
	ByteWriter junk;
	for (int i = 0; i < 64; i++)
		junk.U8((uint8_t)(i * 37));
	CHECK_FALSE(ParseVersionInfo(junk.Span()).Valid);
	CHECK_FALSE(ParseVersionInfo(Node(L"SOMETHING_ELSE", 0, 0).Span()).Valid);
}

TEST_CASE("Damaged version resources never read outside the data", "[version]") {
	auto res = FullResource();

	SECTION("every truncation") {
		for (size_t length = 0; length < res.Bytes.size(); length++)
			ParseVersionInfo(res.Span().first(length));
	}
	SECTION("every byte damaged") {
		for (size_t i = 0; i < res.Bytes.size(); i++)
			for (uint8_t value : { (uint8_t)0x00, (uint8_t)0xFF, (uint8_t)0x7F }) {
				auto copy = res;
				copy.Bytes[i] = value;
				ParseVersionInfo(copy.Span());
			}
	}
}

TEST_CASE("Language and code page names", "[version]") {
	auto english = LanguageName(0x0409);
	CHECK(english.find(L"English") != std::wstring::npos);
	CHECK(LanguageName(0) == L"Neutral");
	CHECK(CodePageName(1200) == L"Unicode (1200)");
	CHECK(CodePageName(0) == L"Neutral");
	CHECK(CodePageName(1252).find(L"1252") != std::wstring::npos);
	CHECK(CodePageName(12345) == L"12345");	// not a code page
}

TEST_CASE("The version resource of a system DLL", "[version][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	PEFile pe;
	if (!pe.Open(std::wstring(dir) + L"\\kernel32.dll"))
		SKIP("kernel32.dll could not be opened");

	for (auto& res : pe.GetFlatResources()) {
		if (MAKEINTRESOURCE(res.TypeID) != RT_VERSION)
			continue;
		auto info = ParseVersionInfo(res.Data);
		REQUIRE(info.Valid);
		CHECK(info.HasFixedInfo);
		CHECK(info.GetString(L"CompanyName").find(L"Microsoft") != std::wstring::npos);
		CHECK_FALSE(info.GetString(L"FileVersion").empty());
		CHECK_FALSE(info.Translations.empty());
		return;
	}
	FAIL("kernel32.dll has no version resource");
}
