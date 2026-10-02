#include "TestCommon.h"
#include <MenuTemplate.h>
#include "ByteWriter.h"

namespace {
	// legacy MENUITEMTEMPLATE option flags
	constexpr uint16_t Grayed = 0x1, Checked = 0x8, Popup = 0x10, End = 0x80;

	// File: Open (100), ---, Exit (101, checked) | Help: About (200, grayed)
	ByteWriter LegacyMenu() {
		ByteWriter w;
		w.U16(0).U16(0);								// version, offset
		w.U16(Popup).Str(L"&File");
		w.U16(0).U16(100).Str(L"&Open\tCtrl+O");
		w.U16(0).U16(0).Str(L"");						// separator
		w.U16(Checked | End).U16(101).Str(L"E&xit");
		w.U16(Popup | End).Str(L"&Help");
		w.U16(Grayed | End).U16(200).Str(L"&About");
		return w;
	}

	// the same menu in the extended format: items are DWORD aligned
	ByteWriter ExtendedMenu() {
		ByteWriter w;
		w.U16(1).U16(4).U32(0);							// version, offset, help id
		auto item = [&](uint32_t type, uint32_t state, uint32_t id, uint16_t info, std::wstring const& text, bool popup) {
			w.Align(4);
			w.U32(type).U32(state).U32(id).U16(info).Str(text);
			w.Align(4);
			if (popup)
				w.U32(0);								// help id of a popup
		};
		item(0, 0, 0, 0x01, L"&File", true);
		item(0, 0, 100, 0, L"&Open\tCtrl+O", false);
		item(MFT_SEPARATOR, 0, 0, 0, L"", false);
		item(0, MFS_CHECKED, 101, 0x80, L"E&xit", false);
		item(0, 0, 0, 0x01 | 0x80, L"&Help", true);
		item(0, MFS_GRAYED, 200, 0x80, L"&About", false);
		return w;
	}

	void CheckStructure(MenuTemplate const& menu) {
		REQUIRE(menu.Items.size() == 2);

		auto& file = menu.Items[0];
		CHECK(file.Popup);
		CHECK(file.Text == L"&File");
		REQUIRE(file.Children.size() == 3);
		CHECK_FALSE(file.Children[0].Popup);
		CHECK(file.Children[0].Id == 100);
		CHECK(file.Children[0].Text == L"&Open\tCtrl+O");
		CHECK(file.Children[1].Separator);
		CHECK(file.Children[2].Id == 101);
		CHECK((file.Children[2].State & MFS_CHECKED) != 0);

		auto& help = menu.Items[1];
		CHECK(help.Popup);
		REQUIRE(help.Children.size() == 1);
		CHECK(help.Children[0].Id == 200);
		CHECK((help.Children[0].State & MFS_GRAYED) != 0);
		CHECK((help.Children[0].State & MFS_CHECKED) == 0);
	}
}

TEST_CASE("A legacy MENU resource is parsed", "[menu]") {
	MenuTemplate menu;
	REQUIRE(MenuTemplate::Parse(LegacyMenu().Span(), menu));
	CHECK_FALSE(menu.Extended);
	CheckStructure(menu);
}

TEST_CASE("A MENUEX resource is parsed", "[menu]") {
	MenuTemplate menu;
	REQUIRE(MenuTemplate::Parse(ExtendedMenu().Span(), menu));
	CHECK(menu.Extended);
	CheckStructure(menu);
}

TEST_CASE("A popup menu can be built from a parsed menu", "[menu]") {
	MenuTemplate menu;
	REQUIRE(MenuTemplate::Parse(LegacyMenu().Span(), menu));

	auto hMenu = MenuTemplate::BuildPopup(menu.Items);
	REQUIRE(hMenu != nullptr);
	CHECK(::GetMenuItemCount(hMenu) == 2);

	auto hFile = ::GetSubMenu(hMenu, 0);
	REQUIRE(hFile != nullptr);
	CHECK(::GetMenuItemCount(hFile) == 3);
	CHECK(::GetMenuItemID(hFile, 0) == 100);
	CHECK((::GetMenuState(hFile, 1, MF_BYPOSITION) & MF_SEPARATOR) != 0);
	CHECK((::GetMenuState(hFile, 2, MF_BYPOSITION) & MF_CHECKED) != 0);

	WCHAR text[64]{};
	::GetMenuStringW(hFile, 0, text, 64, MF_BYPOSITION);
	CHECK(std::wstring(text) == L"&Open\tCtrl+O");

	auto hHelp = ::GetSubMenu(hMenu, 1);
	REQUIRE(hHelp != nullptr);
	CHECK((::GetMenuState(hHelp, 0, MF_BYPOSITION) & MF_GRAYED) != 0);

	::DestroyMenu(hMenu);
}

TEST_CASE("Damaged menu resources are rejected or truncated, never overrun", "[menu]") {
	MenuTemplate menu;

	SECTION("empty and tiny input") {
		CHECK_FALSE(MenuTemplate::Parse({}, menu));
		ByteWriter w;
		w.U16(0);
		CHECK_FALSE(MenuTemplate::Parse(w.Span(), menu));
	}
	SECTION("an unknown version") {
		ByteWriter w;
		w.U16(7).U16(0).U16(0).U16(0);
		CHECK_FALSE(MenuTemplate::Parse(w.Span(), menu));
	}
	SECTION("a header that points past the data") {
		ByteWriter w;
		w.U16(0).U16(0x4000).U16(0);
		CHECK_FALSE(MenuTemplate::Parse(w.Span(), menu));
	}
	SECTION("every truncation of a valid menu") {
		for (auto text : { LegacyMenu(), ExtendedMenu() })
			for (size_t length = 0; length < text.Bytes.size(); length++) {
				MenuTemplate m;
				MenuTemplate::Parse(text.Span().first(length), m);	// whatever it returns, it must stay inside the buffer
			}
	}
	SECTION("a menu nested far deeper than any real menu") {
		ByteWriter w;
		w.U16(0).U16(0);
		for (int i = 0; i < 200; i++)
			w.U16(Popup).Str(L"x");
		MenuTemplate m;
		MenuTemplate::Parse(w.Span(), m);	// must not overflow the stack
	}
}
