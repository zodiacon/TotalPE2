#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <Windows.h>

// A version resource (RT_VERSION): the fixed file information, and the strings and the languages of StringFileInfo and VarFileInfo.
struct VersionInfo {
	struct StringTable {
		std::wstring LangCodePage;	// the key of the table: 8 hex digits, language and code page ("040904B0")
		uint16_t Language{ 0 };		// the language id and code page that the key spells (0 if it does not)
		uint16_t CodePage{ 0 };
		std::vector<std::pair<std::wstring, std::wstring>> Strings;
	};
	struct Translation {
		uint16_t Language{ 0 };
		uint16_t CodePage{ 0 };
	};

	bool Valid{ false };
	bool HasFixedInfo{ false };
	VS_FIXEDFILEINFO Fixed{};
	std::vector<StringTable> Tables;
	std::vector<Translation> Translations;	// the "Translation" value of VarFileInfo

	// The value of a string in the table of a language (the first table if the language is empty). Empty if there is none.
	std::wstring GetString(std::wstring const& name, std::wstring const& langCodePage = L"") const;
};

// Parses the data of a version resource. Damaged resources give what could be read before the damage (Valid is false only if
// not even the root is there).
VersionInfo ParseVersionInfo(std::span<const std::byte> data);

// "English (United States)" for a language id, "" if unknown.
std::wstring LanguageName(uint16_t langId);

// "Unicode (1200)", "Western European (1252)" for a code page, with the number alone if the system does not know it.
std::wstring CodePageName(uint16_t codePage);
