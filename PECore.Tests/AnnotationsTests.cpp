#include "TestCommon.h"
#include <Annotations.h>
#include <Json.h>
#include <filesystem>

TEST_CASE("JSON is read and written", "[json]") {
	JsonValue value;
	std::string error;
	REQUIRE(JsonValue::Parse(R"({ "a": [1, 2.5, -3e2, true, false, null], "b": { "c": "x\"y\\z\n\u00e9\ud83d\ude00" }, "e": {} })", value, &error));
	REQUIRE(value.IsObject());
	auto const& a = value["a"].AsArray();
	REQUIRE(a.size() == 6);
	CHECK(a[0].AsNumber() == 1);
	CHECK(a[1].AsNumber() == 2.5);
	CHECK(a[2].AsNumber() == -300);
	CHECK(a[3].AsBool());
	CHECK_FALSE(a[4].AsBool(true));
	CHECK(a[5].IsNull());
	CHECK(value["b"]["c"].AsString() == "x\"y\\z\n\xC3\xA9\xF0\x9F\x98\x80");	// é and 😀 in UTF-8
	CHECK(value["e"].IsObject());
	CHECK(value["missing"].IsNull());
	CHECK(value["a"]["not an object"].IsNull());

	// written and read back: the same
	for (bool pretty : { true, false }) {
		JsonValue again;
		REQUIRE(JsonValue::Parse(value.Write(pretty), again));
		CHECK(again.Write(false) == value.Write(false));
	}
	CHECK(JsonValue(JsonValue::Object{ { "n", 4294967296.0 }, { "s", "\t" } }).Write(false) == R"({"n":4294967296,"s":"\t"})");

	for (auto bad : { "", "{", "[1,]", "{\"a\" 1}", "\"open", "tru", "[1] 2", "{\"a\":\"\x01\"}", "01x" }) {
		INFO(bad);
		CHECK_FALSE(JsonValue::Parse(bad, value, &error));
		CHECK_FALSE(error.empty());
	}
	// deep nesting is refused, not a stack overflow
	CHECK_FALSE(JsonValue::Parse(std::string(100000, '['), value));
}

TEST_CASE("Annotations are saved and read back", "[annotations]") {
	Annotations a;
	a.Functions[0x1000] = "Start";
	a.Variables[0x1000]["param:0"] = { "count", "uint", true };
	a.Variables[0x1000]["reg:EAX@0x140001020"] = { "total", "int" };
	a.Globals[0x5000] = { "g_flags", "DWORD", true };
	a.Comments[0x1010] = L"the loop \u00e9";
	a.Bookmarks = { { 0x2000, L"second" }, { 0x1000, L"first" } };

	Annotations b;
	REQUIRE(b.FromJson(a.ToJson()));
	CHECK(b.Functions == a.Functions);
	CHECK(b.Variables == a.Variables);
	CHECK(b.Globals == a.Globals);
	CHECK(b.Comments == a.Comments);
	CHECK(b.Bookmarks == a.Bookmarks);	// in their order

	// in a store of their own (written whole, read back; one that has nothing is deleted)
	auto folder = std::filesystem::temp_directory_path() / std::format(L"TotalPE.Annotations.{}", ::GetCurrentProcessId());
	auto store = (folder / L"sub" / L"x.json").wstring();
	Annotations saved;
	REQUIRE(saved.Load(store, L"C:\\file.exe"));	// none yet
	CHECK(saved.Empty());
	saved.Functions = a.Functions;
	REQUIRE(saved.Save());
	Annotations loaded;
	REQUIRE(loaded.Load(store, L"C:\\file.exe"));
	CHECK(loaded.Functions == a.Functions);
	loaded.Functions.clear();
	REQUIRE(loaded.Save());
	CHECK_FALSE(std::filesystem::exists(store));

	// what is not right is left out; text that is not JSON is not read
	Annotations c;
	REQUIRE(c.FromJson(R"({ "functions": { "0x10": "A", "nothex": "B", "0x20": "" }, "bookmarks": [ { "rva": "zz" }, { "rva": "0x30" } ] })"));
	CHECK(c.Functions == std::map<uint32_t, std::string>{ { 0x10, "A" } });
	REQUIRE(c.Bookmarks.size() == 1);
	CHECK(c.Bookmarks[0].Rva == 0x30);
	CHECK_FALSE(c.FromJson("not json"));
	std::filesystem::remove_all(folder);
}
