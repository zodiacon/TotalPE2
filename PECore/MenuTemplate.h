#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct MenuNode {
	std::wstring Text;
	DWORD Id{};
	DWORD Type{};		// MFT_* flags
	DWORD State{};		// MFS_* flags
	DWORD HelpId{};
	bool Popup{ false };
	bool Separator{ false };
	std::vector<MenuNode> Children;
};

// Parsed MENU / MENUEX resource.
struct MenuTemplate {
	bool Extended{ false };
	std::vector<MenuNode> Items;

	static bool Parse(std::span<const std::byte> data, MenuTemplate& menu);

	// Creates a real popup menu from the children of a node. The caller owns (and must destroy) the result.
	static HMENU BuildPopup(std::vector<MenuNode> const& items);
};
