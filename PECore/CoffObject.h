#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A COFF object file (.obj), as compilers write them: a regular one, or a "bigobj" (more than 65279 sections)

struct CoffSection {
	std::string Name;
	uint32_t VirtualSize{}, VirtualAddress{};
	uint32_t SizeOfRawData{}, PointerToRawData{};
	uint32_t PointerToRelocations{}, PointerToLinenumbers{};
	uint32_t NumberOfRelocations{};		// the real number when it does not fit in the header (IMAGE_SCN_LNK_NRELOC_OVFL)
	bool RelocationOverflow{ false };	// then the first relocation is that number
	uint16_t NumberOfLinenumbers{};
	uint32_t Characteristics{};
};

struct CoffSymbol {
	uint32_t Index{};			// in the symbol table (the auxiliary records count, so the indices are not consecutive)
	std::string Name;
	uint32_t Value{};
	int32_t SectionNumber{};	// 1-based; 0 undefined (external), -1 absolute, -2 debug
	uint16_t Type{};
	uint8_t StorageClass{};
	uint8_t AuxCount{};
	std::string Details;		// what the auxiliary records say: a file name, a section's definition, the default of a weak external...
};

// A COFF line number (old compilers; CodeView has the lines now): the first entry of a function has line 0 and the function's symbol,
// the others the offset of the line in the section and its number relative to the function's first line
struct CoffLineNumber {
	uint32_t Section{};				// 0-based
	uint32_t SymbolIndexOrOffset{};
	uint16_t Line{};
};

struct CoffRelocation {
	uint32_t Section{};			// 0-based
	uint32_t Offset{};			// in the section
	uint32_t SymbolIndex{};
	uint16_t Type{};
};

class CoffObject {
public:
	CoffObject() = default;
	// m_Data refers to m_Copy: a copy would refer to the data of the original
	CoffObject(CoffObject const&) = delete;
	CoffObject& operator=(CoffObject const&) = delete;
	CoffObject(CoffObject&&) = default;
	CoffObject& operator=(CoffObject&&) = default;

	// The header of an object: a known machine (or none), no optional header, and tables that fit in the data
	static bool IsObject(std::span<const std::byte> data);
	static bool IsObjectHeader(std::span<const std::byte> header, uint64_t fileSize);
	static bool IsObjectFile(std::wstring_view path);

	bool Open(std::wstring_view path);
	// The data is copied
	bool Parse(std::span<const std::byte> data);
	void Close();
	explicit operator bool() const { return !m_Data.empty(); }

	std::wstring const& Path() const { return m_Path; }
	std::span<const std::byte> Data() const { return m_Data; }
	uint16_t Machine() const { return m_Machine; }
	uint32_t TimeDateStamp() const { return m_TimeDateStamp; }
	uint16_t Characteristics() const { return m_Characteristics; }
	bool BigObj() const { return m_BigObj; }
	uint32_t SymbolTableOffset() const { return m_SymbolTable; }
	uint32_t SymbolTableCount() const { return m_SymbolCount; }	// with the auxiliary records
	uint32_t StringTableSize() const { return m_StringTableSize; }

	std::vector<CoffSection> const& Sections() const { return m_Sections; }
	std::vector<CoffSymbol> const& Symbols() const { return m_Symbols; }
	std::vector<CoffRelocation> const& Relocations() const { return m_Relocations; }
	std::vector<CoffLineNumber> const& LineNumbers() const { return m_LineNumbers; }
	// What did not make sense (a table outside the file...): what could be read is there anyway
	std::vector<std::wstring> const& Problems() const { return m_Problems; }

	// The raw data of a section in the file (empty for .bss and the like)
	std::span<const std::byte> SectionData(size_t index) const;
	// The symbol by its index in the symbol table; null for an index that is an auxiliary record, or outside the table
	CoffSymbol const* SymbolByIndex(uint32_t index) const;
	// The options for the linker (the .drectve section): "/DEFAULTLIB:..." and the like
	std::string Directives() const;

	static std::wstring StorageClassName(uint8_t storageClass);
	static std::wstring SectionNumberName(int32_t number);
	static std::wstring RelocationTypeName(uint16_t machine, uint16_t type);
	static std::wstring CharacteristicsToString(uint16_t characteristics);

private:
	void ReadSections(size_t tableOffset, uint32_t count);
	void ReadSymbols(size_t symbolSize);
	void ReadRelocations();
	void ReadLineNumbers();
	std::string StringAt(uint32_t offset) const;
	std::string SectionName(const char* raw) const;

	std::vector<std::byte> m_Copy;
	std::span<const std::byte> m_Data;
	std::wstring m_Path;
	uint16_t m_Machine{}, m_Characteristics{};
	uint32_t m_TimeDateStamp{}, m_SymbolTable{}, m_SymbolCount{}, m_StringTableSize{};
	bool m_BigObj{ false };
	std::vector<CoffSection> m_Sections;
	std::vector<CoffSymbol> m_Symbols;
	std::vector<int> m_SymbolPositions;		// for each index of the symbol table, the position in m_Symbols (-1 for an auxiliary record)
	std::vector<CoffRelocation> m_Relocations;
	std::vector<CoffLineNumber> m_LineNumbers;
	std::vector<std::wstring> m_Problems;
};
