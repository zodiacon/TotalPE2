#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// The syntax of setup information (.inf) files, as REGINST resources hold them (the scripts of Advanced INF Setup that
// register a module): what each character of a line is, for coloring.

enum class InfStyle : uint8_t {
	Default,
	Comment,		// ; to the end of the line
	Section,		// [Name]
	Key,			// what is before = (Signature=..., AddReg=...)
	Operator,		// = and the commas between fields
	String,			// "..." ("" is a quote in it)
	StringKey,		// %Name%: replaced by its text from the [Strings] section (in strings as well)
	Number,			// 0, 0x10001
	RootKey,		// HKLM, HKCU, HKCR, HKU, HKCC, HKR as the first field of a registry line
};

// The style of each character of one line (with or without its line break)
std::vector<InfStyle> InfLineStyles(std::string_view line);
