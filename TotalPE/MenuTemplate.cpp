#include "pch.h"
#include "MenuTemplate.h"

namespace {
	struct Reader {
		std::span<const std::byte> Data;
		size_t Pos{ 0 };
		bool Ok{ true };

		uint32_t Read(size_t size) {
			if (!Ok || Pos + size > Data.size()) {
				Ok = false;
				return 0;
			}
			uint32_t v = 0;
			for (size_t i = 0; i < size; i++)
				v |= (uint32_t)std::to_integer<uint8_t>(Data[Pos + i]) << (8 * i);
			Pos += size;
			return v;
		}
		uint16_t U16() { return (uint16_t)Read(2); }
		uint32_t U32() { return Read(4); }
		void Align(size_t a) { Pos = (Pos + a - 1) & ~(a - 1); }

		std::wstring Str() {
			std::wstring s;
			for (auto w = U16(); Ok && w; w = U16())
				s += (wchar_t)w;
			return s;
		}
	};

	constexpr int MaxDepth = 32;

	// legacy MENUITEMTEMPLATE option flags
	constexpr WORD MF_GRAYED_ = 0x1, MF_DISABLED_ = 0x2, MF_CHECKED_ = 0x8, MF_POPUP_ = 0x10,
		MF_BARBREAK_ = 0x20, MF_BREAK_ = 0x40, MF_END_ = 0x80, MF_DEFAULT_ = 0x1000, MF_RIGHT_ = 0x4000;

	bool ParseLevel(Reader& r, std::vector<MenuNode>& nodes, bool ex, int depth) {
		if (depth > MaxDepth)
			return false;
		for (;;) {
			MenuNode n;
			bool last;
			if (ex) {
				r.Align(4);
				n.Type = r.U32();
				n.State = r.U32();
				n.Id = r.U32();
				auto info = r.U16();
				n.Text = r.Str();
				r.Align(4);
				n.Popup = info & 1;
				last = info & 0x80;
				if (n.Popup)
					n.HelpId = r.U32();
				n.Separator = n.Type & MFT_SEPARATOR;
			}
			else {
				auto opt = r.U16();
				n.Popup = opt & MF_POPUP_;
				last = opt & MF_END_;
				if (!n.Popup)
					n.Id = r.U16();
				n.Text = r.Str();
				if (opt & (MF_GRAYED_ | MF_DISABLED_))
					n.State |= MFS_GRAYED;
				if (opt & MF_CHECKED_)
					n.State |= MFS_CHECKED;
				if (opt & MF_DEFAULT_)
					n.State |= MFS_DEFAULT;
				if (opt & MF_BARBREAK_)
					n.Type |= MFT_MENUBARBREAK;
				if (opt & MF_BREAK_)
					n.Type |= MFT_MENUBREAK;
				if (opt & MF_RIGHT_)
					n.Type |= MFT_RIGHTJUSTIFY;
				n.Separator = !n.Popup && ((opt & 0x800) || (n.Id == 0 && n.Text.empty() && !(opt & ~MF_END_)));
				if (n.Separator)
					n.Type |= MFT_SEPARATOR;
			}
			if (!r.Ok)
				return false;
			if (n.Popup && !ParseLevel(r, n.Children, ex, depth + 1))
				return false;
			nodes.push_back(std::move(n));
			if (last)
				return true;
		}
	}
}

bool MenuTemplate::Parse(std::span<const std::byte> data, MenuTemplate& menu) {
	Reader r{ data };
	auto version = r.U16();
	auto offset = r.U16();
	if (!r.Ok || version > 1)
		return false;
	menu.Extended = version == 1;
	r.Pos = 4 + (size_t)offset;
	// keep whatever was parsed even if the data is truncated
	ParseLevel(r, menu.Items, menu.Extended, 0);
	return !menu.Items.empty();
}

HMENU MenuTemplate::BuildPopup(std::vector<MenuNode> const& items) {
	auto hMenu = ::CreatePopupMenu();
	if (!hMenu)
		return nullptr;
	for (auto& n : items) {
		MENUITEMINFO mii{ sizeof(mii) };
		std::wstring text = n.Text;
		if (n.Separator) {
			mii.fMask = MIIM_FTYPE;
			mii.fType = MFT_SEPARATOR;
		}
		else {
			mii.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_STRING;
			// no owner-draw / bitmap items: there is nobody to draw them
			mii.fType = n.Type & (MFT_MENUBREAK | MFT_MENUBARBREAK | MFT_RIGHTJUSTIFY | MFT_RADIOCHECK);
			mii.fState = n.State & (MFS_GRAYED | MFS_CHECKED | MFS_DEFAULT);
			mii.wID = n.Id;
			mii.dwTypeData = text.data();
			if (n.Popup) {
				mii.fMask |= MIIM_SUBMENU;
				mii.hSubMenu = BuildPopup(n.Children);
			}
		}
		if (!::InsertMenuItem(hMenu, ::GetMenuItemCount(hMenu), TRUE, &mii) && mii.hSubMenu)
			::DestroyMenu(mii.hSubMenu);
	}
	return hMenu;
}
