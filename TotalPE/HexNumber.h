#pragma once

#include <cstdint>
#include <string>

// Parses a hexadecimal number with an optional 0x prefix or h suffix, such as "1A40", "0x140001A40" or "1A40h".
// Returns false for anything else (a sign, an empty string, more than 16 digits, other characters).
bool ParseHexNumber(std::wstring const& text, uint64_t& value);
