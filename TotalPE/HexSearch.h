#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class HexSearchType {
	Hex,		// "4D 5A ?? 00", ?? and single ? nibbles are wildcards
	Ascii,		// text, searched as UTF-8
	Unicode,	// text, searched as UTF-16 (little endian)
};

struct HexFindOptions {
	std::wstring Text;
	HexSearchType Type{ HexSearchType::Hex };
	bool MatchCase{ false };
	bool Down{ true };
};

struct HexPattern {
	std::vector<uint8_t> Bytes;
	std::vector<uint8_t> Mask;	// bits that must match (0 = wildcard)
	std::vector<uint8_t> Fold;	// non-zero: byte is a letter that matches case-insensitively
	bool empty() const { return Bytes.empty(); }
};

namespace HexSearch {
	bool BuildPattern(HexFindOptions const& options, HexPattern& pattern, std::wstring& error);

	// Searches data[0, size). Forward search starts at 'start' and goes up, backward search starts at 'start' and goes down.
	// Returns the offset of the match or -1.
	int64_t Find(const uint8_t* data, int64_t size, HexPattern const& pattern, int64_t start, bool forward);
}
