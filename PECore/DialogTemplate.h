#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct DlgControl {
	DWORD HelpId{};
	DWORD Style{};
	DWORD ExStyle{};
	short X{}, Y{}, CX{}, CY{};
	int Id{};
	std::wstring ClassName;
	std::wstring Title;
};

// Parsed DLGTEMPLATE / DLGTEMPLATEEX resource.
struct DlgTemplate {
	bool Extended{ false };
	DWORD HelpId{};
	DWORD Style{};
	DWORD ExStyle{};
	short X{}, Y{}, CX{}, CY{};
	std::wstring Menu;			// name or "#ordinal"
	std::wstring ClassName;
	std::wstring Title;
	bool HasFont{ false };
	WORD PointSize{};
	WORD Weight{};
	BYTE Italic{};
	BYTE Charset{};
	std::wstring Typeface;
	std::vector<DlgControl> Controls;

	static bool Parse(std::span<const std::byte> data, DlgTemplate& dlg);

	// Builds an extended template that is safe to instantiate inside this process as a child window:
	// no menu, no custom dialog class, no resource ordinals, WS_CHILD instead of WS_POPUP.
	std::vector<BYTE> BuildPreviewTemplate() const;
};
