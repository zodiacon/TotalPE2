#include "TestCommon.h"
#include "SyntheticPE.h"
#include <PEFile.h>
#include <PEDiff.h>
#include <algorithm>

namespace {
	DiffItem const* Find(PEDiff const& diff, DiffCategory category, std::wstring const& name) {
		auto it = std::ranges::find_if(diff.Items, [&](auto const& i) { return i.Category == category && i.Name == name; });
		return it == diff.Items.end() ? nullptr : &*it;
	}

	struct Pair {
		TempFile LeftFile, RightFile;
		PEFile Left, Right;
		Pair(std::vector<uint8_t> const& left, std::vector<uint8_t> const& right) : LeftFile(left, L".dll"), RightFile(right, L".dll") {
			REQUIRE(Left.Open(LeftFile.Path()));
			REQUIRE(Right.Open(RightFile.Path()));
		}
	};
}

TEST_CASE("A file compared with itself is identical", "[diff]") {
	auto data = SyntheticPE{}.Build();
	Pair files(data, data);
	auto diff = ComparePEFiles(files.Left, files.Right);
	CHECK(diff.Identical);
	CHECK(diff.Count(DiffStatus::Changed) == 0);
	CHECK(diff.Count(DiffStatus::Added) == 0);
	CHECK(diff.Count(DiffStatus::Removed) == 0);
	CHECK(diff.Count(DiffStatus::Same) == diff.Items.size());

	auto text = Find(diff, DiffCategory::Section, L".text");
	REQUIRE(text);
	CHECK(text->Where.Place == DiffPlace::Section);
	CHECK(text->Where.Index == 0);
	CHECK(text->Left == text->Right);
	auto alpha = Find(diff, DiffCategory::Export, L"Alpha");
	REQUIRE(alpha);
	CHECK(alpha->Where.Place == DiffPlace::Export);
	CHECK(alpha->Where.Name == L"Alpha");
	REQUIRE(Find(diff, DiffCategory::Import, L"kernel32.dll!ExitProcess"));
	REQUIRE(Find(diff, DiffCategory::Header, L"Entry Point"));
}

TEST_CASE("The differences between two versions of a file", "[diff]") {
	SyntheticPE before;
	SyntheticPE after;
	after.ImportModule = "user32.dll";
	auto newer = after.Build();
	// a different time stamp, and a changed byte in the code
	*(uint32_t*)(newer.data() + SyntheticPE::ELfanew + 8) = SyntheticPE::TimeDateStamp + 100;
	newer[SyntheticPE::TextOffset + 0x20] ^= 0xFF;
	Pair files(before.Build(), newer);
	auto diff = ComparePEFiles(files.Left, files.Right);
	CHECK_FALSE(diff.Identical);

	SECTION("headers") {
		auto stamp = Find(diff, DiffCategory::Header, L"Time/Date Stamp");
		REQUIRE(stamp);
		CHECK(stamp->Status == DiffStatus::Changed);
		CHECK(stamp->Left != stamp->Right);
		CHECK(stamp->Details.empty());		// the values say what changed
		auto machine = Find(diff, DiffCategory::Header, L"Machine");
		REQUIRE(machine);
		CHECK(machine->Status == DiffStatus::Same);
	}
	SECTION("the content of a section") {
		auto text = Find(diff, DiffCategory::Section, L".text");
		REQUIRE(text);
		CHECK(text->Status == DiffStatus::Changed);
		CHECK(text->Details == L"content: 1 of 512 bytes differ");
		CHECK(Find(diff, DiffCategory::Section, L".data")->Status == DiffStatus::Same);
	}
	SECTION("imports") {
		auto removed = Find(diff, DiffCategory::Import, L"kernel32.dll");
		REQUIRE(removed);
		CHECK(removed->Status == DiffStatus::Removed);
		CHECK(removed->Where.Place == DiffPlace::ImportModule);
		CHECK(removed->Right.empty());
		auto added = Find(diff, DiffCategory::Import, L"user32.dll");
		REQUIRE(added);
		CHECK(added->Status == DiffStatus::Added);
		CHECK(added->Where.Place == DiffPlace::None);	// it is not in this file
		CHECK(added->Left.empty());
		CHECK(Find(diff, DiffCategory::Import, L"user32.dll!ExitProcess")->Status == DiffStatus::Added);
		CHECK(Find(diff, DiffCategory::Import, L"kernel32.dll!ExitProcess")->Status == DiffStatus::Removed);
	}
	SECTION("the whole file") {
		auto content = Find(diff, DiffCategory::File, L"Content");
		REQUIRE(content);
		CHECK(content->Status == DiffStatus::Changed);
		CHECK(Find(diff, DiffCategory::Export, L"Beta")->Status == DiffStatus::Same);
	}
}

TEST_CASE("A 32-bit and a 64-bit file", "[diff]") {
	SyntheticPE pe32;
	pe32.Is64 = false;
	Pair files(pe32.Build(), SyntheticPE{}.Build());
	auto diff = ComparePEFiles(files.Left, files.Right);
	auto magic = Find(diff, DiffCategory::Header, L"Magic");
	REQUIRE(magic);
	CHECK(magic->Status == DiffStatus::Changed);
	CHECK(magic->Left == L"PE32");
	CHECK(magic->Right == L"PE32+");
	CHECK(Find(diff, DiffCategory::Header, L"Image Base")->Status == DiffStatus::Changed);
	CHECK(std::wstring(PEDiff::StatusName(DiffStatus::Added)) == L"Added");
	CHECK(std::wstring(PEDiff::CategoryName(DiffCategory::Directory)) == L"Data Directory");
}

TEST_CASE("Two versions of a system DLL", "[diff][system]") {
	PEFile a, b;
	if (!a.Open(L"C:\\Windows\\System32\\kernel32.dll") || !b.Open(L"C:\\Windows\\SysWOW64\\kernel32.dll"))
		SKIP("kernel32.dll is not in both System32 and SysWOW64");
	auto diff = ComparePEFiles(a, b);
	CHECK_FALSE(diff.Identical);
	CHECK(Find(diff, DiffCategory::Header, L"Magic")->Status == DiffStatus::Changed);
	// the same exports, with other ordinals
	CHECK(diff.Items.size() > 1000);
	CHECK(std::ranges::count_if(diff.Items, [](auto const& i) {
		return i.Category == DiffCategory::Export && (i.Status == DiffStatus::Same || i.Status == DiffStatus::Changed);
	}) > 1000);
	auto exp = std::ranges::find_if(diff.Items, [](auto const& i) { return i.Category == DiffCategory::Export && i.Status == DiffStatus::Changed; });
	REQUIRE(exp != diff.Items.end());
	CHECK(exp->Details == L"ordinal");
	auto version = Find(diff, DiffCategory::Version, L"FileDescription");
	REQUIRE(version);
	CHECK(version->Status == DiffStatus::Same);
}
