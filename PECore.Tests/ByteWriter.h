#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Little-endian writer for building resource templates by hand in tests.
struct ByteWriter {
	std::vector<uint8_t> Bytes;

	ByteWriter& U8(uint8_t v) { Bytes.push_back(v); return *this; }
	ByteWriter& U16(uint16_t v) { U8(v & 0xFF); return U8(v >> 8); }
	ByteWriter& U32(uint32_t v) { U16(v & 0xFFFF); return U16((uint16_t)(v >> 16)); }
	ByteWriter& Align(size_t a) { while (Bytes.size() % a) U8(0); return *this; }
	// zero-terminated UTF-16 string
	ByteWriter& Str(std::wstring const& s) { for (auto c : s) U16(c); return U16(0); }

	std::span<const std::byte> Span() const { return std::as_bytes(std::span(Bytes)); }
};
