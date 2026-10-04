#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// An ELF file (Linux, the BSDs...): an executable, a shared object, a relocatable object or a core dump; 32 or 64 bit, either byte order

struct ElfProgramHeader {
	uint32_t Type{}, Flags{};
	uint64_t Offset{}, VirtualAddress{}, PhysicalAddress{}, FileSize{}, MemorySize{}, Align{};
};

struct ElfSection {
	std::string Name;
	uint32_t Type{};
	uint64_t Flags{}, Address{}, Offset{}, Size{};
	uint32_t Link{}, Info{};
	uint64_t AddressAlign{}, EntrySize{};
};

struct ElfSymbol {
	std::string Name;
	uint64_t Value{}, Size{};
	uint8_t Type{}, Bind{}, Visibility{};
	uint32_t SectionIndex{};	// SHN_UNDEF, SHN_ABS, SHN_COMMON... or the index of a section
	bool Dynamic{ false };		// from .dynsym (what the dynamic linker sees), not .symtab
	uint32_t Index{};			// in its table
};

struct ElfDynamicEntry {
	int64_t Tag{};
	uint64_t Value{};
	std::string Text;			// the string of DT_NEEDED, DT_SONAME, DT_RPATH and DT_RUNPATH
};

struct ElfRelocation {
	uint32_t Section{};			// the section of the relocations
	uint64_t Offset{};			// an address (executables, shared objects), or an offset in the target section (relocatable objects)
	uint32_t Type{};
	uint32_t SymbolIndex{};
	std::string SymbolName;
	int64_t Addend{};
	bool HasAddend{ false };	// RELA
	bool Relr{ false };			// a packed relative relocation (SHT_RELR)
};

struct ElfNote {
	std::string Owner;			// "GNU", "Go", "stapsdt"...
	uint32_t Type{};
	std::vector<std::byte> Description;
	std::string Section;		// where it is: a section's name, or "PT_NOTE"
};

class ElfFile {
public:
	ElfFile() = default;
	ElfFile(ElfFile const&) = delete;
	ElfFile& operator=(ElfFile const&) = delete;
	ElfFile(ElfFile&&) = default;
	ElfFile& operator=(ElfFile&&) = default;

	static bool IsElf(std::span<const std::byte> data);
	static bool IsElfFile(std::wstring_view path);

	bool Open(std::wstring_view path);
	// The data is copied
	bool Parse(std::span<const std::byte> data);
	void Close();
	explicit operator bool() const { return !m_Data.empty(); }

	std::wstring const& Path() const { return m_Path; }
	std::span<const std::byte> Data() const { return m_Data; }

	bool Is64() const { return m_Is64; }
	bool BigEndian() const { return m_BigEndian; }
	uint8_t OsAbi() const { return m_OsAbi; }
	uint8_t AbiVersion() const { return m_AbiVersion; }
	uint16_t Type() const { return m_Type; }
	uint16_t Machine() const { return m_Machine; }
	uint32_t Flags() const { return m_Flags; }
	uint64_t Entry() const { return m_Entry; }
	uint64_t ProgramHeaderOffset() const { return m_PhOffset; }
	uint64_t SectionHeaderOffset() const { return m_ShOffset; }

	std::vector<ElfProgramHeader> const& ProgramHeaders() const { return m_ProgramHeaders; }
	std::vector<ElfSection> const& Sections() const { return m_Sections; }
	std::vector<ElfSymbol> const& Symbols() const { return m_Symbols; }
	std::vector<ElfDynamicEntry> const& Dynamic() const { return m_Dynamic; }
	std::vector<ElfRelocation> const& Relocations() const { return m_Relocations; }
	std::vector<ElfNote> const& Notes() const { return m_Notes; }
	std::vector<std::wstring> const& Problems() const { return m_Problems; }

	std::string const& Interpreter() const { return m_Interpreter; }
	// the DT_NEEDED libraries, in order
	std::vector<std::string> NeededLibraries() const;
	std::string DynamicString(int64_t tag) const;	// the first DT_SONAME, DT_RUNPATH...
	// the GNU build ID as hex, empty if there is none
	std::string BuildId() const;

	// The data of a section in the file (empty for SHT_NOBITS)
	std::span<const std::byte> SectionData(size_t index) const;
	// The file offset of a virtual address, by the PT_LOAD segments (by the sections if there are none); -1 if it is not in the file
	int64_t OffsetOfAddress(uint64_t address) const;
	// The section that holds a virtual address (allocated sections only); -1 if none
	int SectionOfAddress(uint64_t address) const;

	static std::wstring TypeName(uint16_t type);
	static std::wstring MachineName(uint16_t machine);
	static std::wstring OsAbiName(uint8_t abi);
	static std::wstring SegmentTypeName(uint32_t type);
	static std::wstring SegmentFlagsToString(uint32_t flags);
	static std::wstring SectionTypeName(uint32_t type);
	static std::wstring SectionFlagsToString(uint64_t flags);
	static std::wstring SymbolTypeName(uint8_t type);
	static std::wstring SymbolBindName(uint8_t bind);
	static std::wstring SymbolVisibilityName(uint8_t visibility);
	static std::wstring DynamicTagName(int64_t tag);
	static std::wstring RelocationTypeName(uint16_t machine, uint32_t type);
	static std::wstring NoteTypeName(std::string const& owner, uint32_t type);
	// what a note says: a build ID in hex, the ABI tag's OS and version... (hex bytes for the others)
	std::wstring NoteDescription(ElfNote const& note) const;

private:
	uint16_t U16(uint64_t offset) const;
	uint32_t U32(uint64_t offset) const;
	uint64_t U64(uint64_t offset) const;
	uint64_t Word(uint64_t offset) const { return m_Is64 ? U64(offset) : U32(offset); }
	bool Fits(uint64_t offset, uint64_t size) const { return offset <= m_Data.size() && m_Data.size() - offset >= size; }
	std::string StringAt(uint32_t stringSection, uint64_t offset) const;

	void ReadProgramHeaders(uint16_t entrySize, uint32_t count);
	void ReadSections(uint16_t entrySize, uint32_t count, uint32_t namesIndex);
	void ReadSymbols();
	void ReadDynamic();
	void ReadRelocations();
	void ReadNotes();
	void ReadNotes(uint64_t offset, uint64_t size, uint64_t align, std::string const& where);

	std::vector<std::byte> m_Copy;
	std::span<const std::byte> m_Data;
	std::wstring m_Path;
	bool m_Is64{ false }, m_BigEndian{ false };
	uint8_t m_OsAbi{}, m_AbiVersion{};
	uint16_t m_Type{}, m_Machine{};
	uint32_t m_Flags{};
	uint64_t m_Entry{}, m_PhOffset{}, m_ShOffset{};
	std::vector<ElfProgramHeader> m_ProgramHeaders;
	std::vector<ElfSection> m_Sections;
	std::vector<ElfSymbol> m_Symbols;
	std::vector<ElfDynamicEntry> m_Dynamic;
	std::vector<ElfRelocation> m_Relocations;
	std::vector<ElfNote> m_Notes;
	std::string m_Interpreter;
	std::vector<std::wstring> m_Problems;
};
