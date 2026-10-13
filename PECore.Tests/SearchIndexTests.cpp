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

TEST_CASE("The best matches come first", "[search]") {
	SearchIndex index;
	index.Add({ .Kind = SearchKind::String, .Name = L"CreateFileW" });
	index.Add({ .Kind = SearchKind::Import, .Name = L"NtCreateFile" });
	index.Add({ .Kind = SearchKind::Import, .Name = L"CreateFileW" });
	index.Add({ .Kind = SearchKind::Symbol, .Name = L"recreatefile" });
	index.Add({ .Kind = SearchKind::Export, .Name = L"CreateFile" });
	index.Add({ .Kind = SearchKind::View, .Name = L"Imports" });

	SECTION("exact, prefix, word start, inside") {
		auto found = index.FindBest(L"createfile", AllSearchKinds, 100);
		REQUIRE(found.size() == 5);
		CHECK(index[found[0]].Name == L"CreateFile");					// the same
		CHECK(index[found[1]].Kind == SearchKind::Import);				// a prefix: an import before the string
		CHECK(index[found[2]].Kind == SearchKind::String);
		CHECK(index[found[3]].Name == L"NtCreateFile");				// a word starts there
		CHECK(index[found[4]].Name == L"recreatefile");				// inside
	}
	SECTION("words must all be in the name") {
		auto found = index.FindBest(L"file nt", AllSearchKinds, 100);
		REQUIRE(found.size() == 1);
		CHECK(index[found[0]].Name == L"NtCreateFile");
	}
	SECTION("the limit keeps the best") {
		auto found = index.FindBest(L"create", AllSearchKinds, 2);
		REQUIRE(found.size() == 2);
		CHECK(index[found[0]].Name == L"CreateFile");
		CHECK(index[found[1]].Name == L"CreateFileW");
	}
	SECTION("the kinds asked for") {
		auto found = index.FindBest(L"imp", SearchKindBit(SearchKind::View), 100);
		REQUIRE(found.size() == 1);
		CHECK(index[found[0]].Name == L"Imports");
	}
	SECTION("ranks") {
		CHECK(SearchIndex::MatchRank(L"GetProcAddress", L"getprocaddress", L"proc") == 2);
		CHECK(SearchIndex::MatchRank(L"get_proc", L"get_proc", L"proc") == 2);
		CHECK(SearchIndex::MatchRank(L"preprocess", L"preprocess", L"proc") == 3);
		CHECK(SearchIndex::MatchRank(L"Proc", L"proc", L"proc") == 0);
		CHECK(SearchIndex::MatchRank(L"Process", L"process", L"proc") == 1);
		CHECK(SearchIndex::MatchRank(L"abc", L"abc", L"x") == -1);
		CHECK(SearchIndex::MatchRank(L"abc", L"abc", L"") == -1);
		// the last part of a qualified name
		CHECK(SearchIndex::MatchRank(L"Sample.Box`1", L"sample.box`1", L"box") == 1);
		CHECK(SearchIndex::MatchRank(L"CMainFrame::OnFileOpen", L"cmainframe::onfileopen", L"onfileopen") == 0);
		CHECK(SearchIndex::MatchRank(L"Sample.Box`1/Inner", L"sample.box`1/inner", L"box") == 2);
	}
}
