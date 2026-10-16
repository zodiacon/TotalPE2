#pragma once

#include <map>
#include <span>
#include <string>

//
// The colors of code (disassembly, C/C++, IL, XML, INF...) in the views that show it, of the light theme and of the dark
// one: each element has its defaults, and the user can change them (Options > Colors), which the settings keep (only those
// changed, as "Asm.Comment=008000;C.Keyword=0000FF,b").
//
struct CodeColorElement {
	char const* Key;			// "Asm.Comment"
	PCWSTR Language;			// "Disassembly"
	PCWSTR Name;				// "Comment"
	COLORREF Light, Dark;
	bool Bold{ false };
};

struct CodeColor {
	COLORREF Color;
	bool Bold;
	bool operator==(CodeColor const&) const = default;
};

namespace CodeColors {
	// the elements, by language
	std::span<CodeColorElement const> Elements();
	// an element's default, of a theme
	CodeColor Default(CodeColorElement const& element, bool dark);
	// the color of an element (its key), of a theme: the user's, or its default
	CodeColor Get(char const* key, bool dark);
	// those of a theme that differ from their defaults (by key), and replacing them all
	std::map<std::string, CodeColor> Changed(bool dark);
	void SetChanged(bool dark, std::map<std::string, CodeColor> const& changed);
}
