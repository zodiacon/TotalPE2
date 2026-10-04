#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What a resource holds, as far as its type and its first bytes tell
enum class ResourceContent {
	Binary,
	Image,				// PNG, JPEG, GIF or a bitmap file
	Xml,
	Html,
	Text,
	RegistryScript,		// an ATL registrar script (.rgs)
	TypeLib,
	Font,				// TrueType, OpenType or a Windows (.fnt) font
	Executable,			// a PE file embedded in the resource
};

const wchar_t* ResourceContentToString(ResourceContent content);

// typeId is the numeric type (RT_*), 0 if the type has a name; typeName is that name ("REGISTRY", "TYPELIB")
ResourceContent DetectResourceContent(std::span<const std::byte> data, uint16_t typeId, std::wstring_view typeName);

// The text of a resource as UTF-8: UTF-16 (with or without a byte order mark), UTF-8, or the ANSI code page
std::string ResourceTextToUtf8(std::span<const std::byte> data);

// A resource as a file of its own: the data (a bitmap gets the file header that resources leave out) and the extension that fits
struct ResourceFile {
	std::vector<std::byte> Data;
	std::wstring Extension;		// without the dot
};
ResourceFile MakeResourceFile(std::span<const std::byte> data, uint16_t typeId, std::wstring_view typeName);

// The family name of a TrueType or OpenType font (its 'name' table), or the face name of a Windows .fnt font; empty if not known
std::wstring GetFontFaceName(std::span<const std::byte> data);
