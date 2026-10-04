#include "TestCommon.h"
#include <SearchIndex.h>

namespace {
	SearchIndex BuildIndex() {
		SearchIndex index;
		index.Add({ .Kind = SearchKind::ImportModule, .Name = L"KERNEL32.dll" });
		index.Add({ .Kind = SearchKind::Import, .Name = L"CreateFileW", .Details = L"KERNEL32.dll", .Owner = L"KERNEL32.dll" });
		index.Add({ .Kind = SearchKind::Import, .Name = L"ReadFile", .Details = L"KERNEL32.dll", .Owner = L"KERNEL32.dll" });
		index.Add({ .Kind = SearchKind::Export, .Name = L"DllGetClassObject", .Value = 1 });
		index.Add({ .Kind = SearchKind::String, .Name = L"Cannot create the file", .Value = 0x400 });
		index.Add({ .Kind = SearchKind::Resource, .Name = L"Icon\\#1", .Index = 0 });
		index.Add({ .Kind = SearchKind::Symbol, .Name = L"CMainFrame::OnFileOpen", .Value = 0x140001000 });
		index.Add({ .Kind = SearchKind::Section, .Name = L".text", .Index = 0 });
		return index;
	}
}

TEST_CASE("Search All finds names that contain the text", "[search]") {
	auto index = BuildIndex();
	REQUIRE(index.Size() == 8);

	SECTION("case insensitive by default, in the order of the index") {
		auto found = index.Find(L"file", false, AllSearchKinds, 100);
		REQUIRE(found.size() == 4);
		CHECK(index[found[0]].Name == L"CreateFileW");
		CHECK(index[found[1]].Name == L"ReadFile");
		CHECK(index[found[2]].Kind == SearchKind::String);
		CHECK(index[found[3]].Kind == SearchKind::Symbol);
	}
	SECTION("matching the case") {
		auto found = index.Find(L"File", true, AllSearchKinds, 100);
		CHECK(found.size() == 3);	// not "the file"
		CHECK(index.Find(L"FILE", true, AllSearchKinds, 100).empty());
	}
	SECTION("only the kinds asked for") {
		auto found = index.Find(L"file", false, SearchKindBit(SearchKind::Import) | SearchKindBit(SearchKind::Symbol), 100);
		REQUIRE(found.size() == 3);
		CHECK(index[found[2]].Name == L"CMainFrame::OnFileOpen");
		CHECK(index.Find(L"kernel32", false, SearchKindBit(SearchKind::Import), 100).empty());	// the module is not the import's name
		CHECK(index.Find(L"kernel32", false, SearchKindBit(SearchKind::ImportModule), 100).size() == 1);
	}
	SECTION("at most the limit") {
		bool truncated = false;
		auto found = index.Find(L"e", false, AllSearchKinds, 2, &truncated);
		CHECK(found.size() == 2);
		CHECK(truncated);
		index.Find(L"text", false, AllSearchKinds, 2, &truncated);
		CHECK_FALSE(truncated);
	}
	SECTION("nothing for no text") {
		CHECK(index.Find(L"", false, AllSearchKinds, 100).empty());
	}
	SECTION("the names of the kinds") {
		CHECK(std::wstring(SearchIndex::KindName(SearchKind::ImportModule)) == L"Imported Module");
		CHECK(std::wstring(SearchIndex::KindName(SearchKind::Section)) == L"Section");
	}
	index.Clear();
	CHECK(index.Empty());
	CHECK(index.Find(L"file", false, AllSearchKinds, 100).empty());
}
