#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class ElfFile;

// The debug information of an ELF file: its DWARF (the compile units and the functions), and where the debug information is
// when it is in a separate file (.gnu_debuglink, the build ID). DWARF 2 to 5; sections compressed with zlib (SHF_COMPRESSED,
// and the older .zdebug_ sections).

struct DwarfSection {
	std::string Name;
	uint64_t Size{};				// in the file
	uint64_t UncompressedSize{};	// the same as Size if it is not compressed
	std::wstring Compression;		// "zlib", or empty
};

struct DwarfUnit {
	uint64_t Offset{};				// in .debug_info
	uint16_t Version{};
	uint8_t UnitType{};				// DW_UT_compile, DW_UT_partial... (1 for DWARF before 5)
	uint8_t AddressSize{};
	std::string Name, Producer, CompDir;
	uint32_t Language{};
	uint64_t LowPc{}, HighPc{};
	bool HasRange{ false };
	uint32_t Functions{};
};

struct DwarfFunction {
	std::string Name;				// as it is in the source ("run"); the linkage name is the symbol ("_ZN6Widget3runEv")
	std::string LinkageName;
	uint64_t LowPc{}, HighPc{};
	uint32_t Unit{};				// index in Units
	bool External{ false };
	uint32_t Line{};				// of the declaration, 0 if unknown
};

struct DwarfInfo {
	std::vector<DwarfSection> Sections;
	std::vector<DwarfUnit> Units;
	std::vector<DwarfFunction> Functions;	// the ones with code (a low PC), by address
	std::vector<std::wstring> Problems;

	bool Empty() const { return Units.empty(); }
	static std::wstring LanguageName(uint32_t language);
	static std::wstring UnitTypeName(uint8_t type);
};

// The DWARF of an ELF file (not of a separate debug file)
DwarfInfo ReadDwarf(ElfFile const& elf);

struct ElfDebugLink {
	std::string File;		// the name of the debug file
	uint32_t Crc{};			// CRC-32 of the debug file
	bool Present{ false };
};

ElfDebugLink ReadDebugLink(ElfFile const& elf);

// Where the separate debug file may be: next to the file, in its .debug directory and, for a file in a WSL distribution
// (\\wsl.localhost\<distro>\...), in /usr/lib/debug by the build ID and by the path
std::vector<std::wstring> DebugFileCandidates(std::wstring const& path, ElfDebugLink const& link, std::string const& buildId);

uint32_t Crc32(std::span<const std::byte> data);

// The debug information of a file: in the file, or in the separate debug file if one is found (that matches: the CRC of
// .gnu_debuglink, or the build ID)
struct ElfDebugInfo {
	ElfDebugLink Link;
	std::wstring DebugFile;			// the separate debug file that the DWARF comes from; empty if it is in the file or nowhere
	std::vector<std::wstring> Searched;	// where the debug file was looked for, when it was not found
	DwarfInfo Dwarf;
};

ElfDebugInfo LoadElfDebugInfo(ElfFile const& elf);
