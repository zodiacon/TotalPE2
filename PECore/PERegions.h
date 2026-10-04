#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <PEFile.h>

// A named part of a PE file, used to color the hex view by structure.
struct HexRegion {
	int64_t Offset{};	// file offset
	int64_t Length{};
	std::wstring Name;
	int Color{};		// index into the view's palette
};

constexpr int HexRegionColorCount = 12;

// Headers, sections, data directories, certificate and overlay of a PE file.
std::vector<HexRegion> BuildPERegions(PEFile const& pe);

// Maps a file offset to an RVA. Returns false if the offset is not part of the mapped image.
bool FileOffsetToRva(PEFile const& pe, uint64_t offset, DWORD& rva);
