#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class CoffObject;

// The CodeView debug information of an object file (compiled with /Z7, /Zi or /Zd): the .debug$S sections have the symbols,
// the source lines and the source files; the .debug$T (and .debug$P) sections have the types, or a reference to the PDB
// that has them (/Zi). Only the C13 format (signature 4) is read: compilers have written it since Visual C++ 2005.

// A block of a .debug$S section: symbols, lines, the file names...
struct CvSubsection {
	uint32_t Section{};			// 0-based
	uint32_t Offset{};			// of its header in the section
	uint32_t Type{};
	uint32_t Size{};			// of its data
};

// The places a record describes: in an object, the address of a function or a variable is a relocation against a symbol
struct CvTarget {
	int Section{ -1 };			// 0-based; -1 if the place is not known
	uint32_t Offset{};
};

struct CvSymbol {
	uint32_t Section{}, Offset{};	// the record: in which .debug$S section and where
	uint16_t Kind{};
	uint16_t Size{};				// of the record, with its length
	int Depth{};					// in a procedure, a block...
	std::wstring Name;
	std::wstring Details;
	CvTarget Target;
};

struct CvFile {
	uint32_t Section{}, Offset{};	// the entry in the checksums
	uint32_t Id{};					// what the lines refer to the file by: the offset of the entry in its subsection
	std::wstring Name;
	uint8_t ChecksumKind{};			// 0 none, 1 MD5, 2 SHA-1, 3 SHA-256
	std::wstring Checksum;			// in hex
};

struct CvLine {
	uint32_t Section{}, Offset{};	// the entry in the .debug$S section
	std::wstring Function;			// the symbol the lines are relative to
	std::wstring File;
	uint32_t CodeOffset{};			// in the function
	uint32_t Line{}, EndLine{};
	uint16_t Column{}, EndColumn{};	// 0 if the lines have no columns
	bool Statement{};
	CvTarget Target;
};

struct CvType {
	uint32_t Index{};				// 0x1000 and up
	uint32_t Section{}, Offset{};
	uint16_t Kind{};
	uint16_t Size{};				// of the record, with its length
	std::wstring Name;				// the declaration, as far as it can be told: "Point", "int*", "void(int, char const*)"...
	std::wstring Details;
};

struct CodeViewInfo {
	std::vector<CvSubsection> Subsections;
	std::vector<CvSymbol> Symbols;
	std::vector<CvLine> Lines;
	std::vector<CvFile> Files;
	std::vector<CvType> Types;
	std::wstring TypeServer;		// the PDB that has the types (/Zi), if any
	std::wstring Compiler;			// from S_COMPILE3: the language, the version...
	std::vector<std::wstring> Problems;

	bool Empty() const { return Subsections.empty() && Types.empty(); }

	static std::wstring SymbolKindName(uint16_t kind);
	static std::wstring TypeKindName(uint16_t kind);
	static std::wstring SubsectionName(uint32_t type);
	static std::wstring ChecksumKindName(uint8_t kind);
};

CodeViewInfo ReadCodeView(CoffObject const& obj);
