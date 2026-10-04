#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

enum class StringEncoding : uint8_t {
	Ascii,
	Utf16,		// little endian, characters in the ASCII range (as most strings in Windows binaries are)
};

const wchar_t* StringEncodingToString(StringEncoding encoding);

// A run of printable characters found in the data.
struct FoundString {
	uint32_t Offset;		// of the first byte in the data
	uint32_t Length;		// in characters (the size in bytes is twice that for UTF-16)
	StringEncoding Encoding;
	std::wstring Text;		// at most MaxTextLength characters of the string
};

struct StringScanOptions {
	uint32_t MinLength{ 5 };
	bool Ascii{ true };
	bool Utf16{ true };
	static constexpr uint32_t MaxTextLength = 4096;
};

// The strings in the data, sorted by offset. Printable characters are 0x20-0x7E and the tab.
// UTF-16 strings are found at even and odd offsets alike.
std::vector<FoundString> FindStrings(std::span<const std::byte> data, StringScanOptions const& options = {});
