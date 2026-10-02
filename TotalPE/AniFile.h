#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// One image of an animated cursor or icon. The 'icon' chunk of an .ani file is a complete .ico or .cur file;
// this keeps those bytes and describes the best (largest) image in them.
struct AniFrame {
	std::vector<uint8_t> Data;	// the .ico / .cur file
	uint32_t ImageOffset{};		// the image (the DIB or PNG) inside Data
	uint32_t ImageSize{};
	int Width{}, Height{};
	int BitCount{};
	int HotspotX{}, HotspotY{};	// cursors only
	bool IsCursor{ false };
};

// A parsed animated cursor / icon: the RIFF "ACON" format of .ani files (RT_ANICURSOR and RT_ANIICON resources).
struct AniFile {
	static constexpr uint32_t MaxFrames = 4096;
	static constexpr uint32_t MaxSteps = 65536;
	static constexpr uint32_t JiffiesPerSecond = 60;

	uint32_t Width{}, Height{}, BitCount{}, Planes{};
	uint32_t DisplayRate{ 10 };				// default duration of a step, in jiffies (1/60 second)
	uint32_t Flags{};						// AF_ICON (1), AF_SEQUENCE (2)
	std::wstring Title, Author;				// from the INFO list, if present
	std::vector<AniFrame> Frames;
	std::vector<uint32_t> Sequence;			// per step, the index of the frame to show
	std::vector<uint32_t> Rates;			// per step, the duration in jiffies; empty if every step uses DisplayRate

	static bool Parse(std::span<const std::byte> data, AniFile& ani, std::wstring* error = nullptr);

	size_t StepCount() const { return Sequence.size(); }
	AniFrame const& StepFrame(size_t step) const { return Frames[Sequence[step]]; }
	uint32_t StepJiffies(size_t step) const;
	uint32_t StepMilliseconds(size_t step) const;
	uint32_t TotalMilliseconds() const;
};
