#pragma once

#include <cstdint>
#include <string>

// The name pefile gives to an ordinal of ws2_32.dll, wsock32.dll or oleaut32.dll (lowercase file name), or null if
// the library is another one or the ordinal is not in pefile's table. Used for the import hash only.
const char* ImphashOrdinalName(std::string const& dll, uint16_t ordinal);
