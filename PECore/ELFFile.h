#pragma once

#include "BinaryFile.h"

struct ELFInfo {
    bool         Is32Bit{};
    bool         IsLittleEndian{};
    std::wstring ClassName;
    std::wstring Endianness;
    std::wstring OSABI;
    std::wstring FileType;
    std::wstring Machine;
    uint64_t     EntryPoint{};
    uint32_t     Flags{};
    uint32_t     SectionCount{};
    uint32_t     SegmentCount{};
    uint32_t     StaticSymbolCount{};
    uint32_t     DynamicSymbolCount{};
    uint32_t     DynamicEntryCount{};
    uint32_t     RelocationCount{};
    uint32_t     NoteCount{};
};

struct ELFSectionEntry {
    std::wstring Name;
    std::wstring TypeStr;
    uint32_t     Type{};
    uint64_t     Flags{};
    std::wstring FlagsStr;
    uint64_t     VirtualAddress{};
    uint64_t     FileOffset{};
    uint64_t     Size{};
    uint64_t     Alignment{};
    uint32_t     EntrySize{};
};

struct ELFSegmentEntry {
    std::wstring TypeStr;
    uint32_t     Type{};
    uint32_t     Flags{};
    std::wstring FlagsStr;
    uint64_t     FileOffset{};
    uint64_t     VirtualAddress{};
    uint64_t     PhysicalAddress{};
    uint64_t     FileSize{};
    uint64_t     MemorySize{};
    uint64_t     Alignment{};
};

struct ELFSymbolEntry {
    std::wstring Name;
    uint64_t     Value{};
    uint64_t     Size{};
    std::wstring TypeStr;
    std::wstring BindingStr;
    std::wstring VisibilityStr;
    uint16_t     SectionIndex{};
    bool         IsDynamic{};
};

struct ELFDynamicEntry {
    std::wstring TagStr;
    uint64_t     Value{};
    std::wstring ValueStr;
};

struct ELFRelocationEntry {
    uint64_t     Address{};
    uint32_t     Type{};
    int64_t      Addend{};
    bool         IsRela{};
    std::wstring SymbolName;
    std::wstring SectionName;
};

struct ELFNoteEntry {
    std::wstring Name;
    uint32_t     Type{};
    uint32_t     DescriptionSize{};
};

class ELFFile : public BinaryFile {
public:
    ELFFile();
    ~ELFFile();
    ELFFile(ELFFile&&) noexcept;
    ELFFile& operator=(ELFFile&&) noexcept;

    bool Open(std::wstring_view path) override;
    void Close() override;

    ELFInfo                          const& GetELFInfo()         const;
    std::vector<ELFSectionEntry>     const& GetELFSections()     const;
    std::vector<ELFSegmentEntry>     const& GetSegments()         const;
    std::vector<ELFSymbolEntry>      const& GetAllSymbols()       const;
    std::vector<ELFDynamicEntry>     const& GetDynamicEntries()   const;
    std::vector<ELFRelocationEntry>  const& GetRelocations()      const;
    std::vector<ELFNoteEntry>        const& GetNotes()            const;

private:
    void BuildCaches();

    struct LiefImpl;
    std::unique_ptr<LiefImpl> m_Impl;

    ELFInfo                         m_Info{};
    std::vector<ELFSectionEntry>    m_ELFSections;
    std::vector<ELFSegmentEntry>    m_Segments;
    std::vector<ELFSymbolEntry>     m_AllSymbols;
    std::vector<ELFDynamicEntry>    m_DynamicEntries;
    std::vector<ELFRelocationEntry> m_Relocations;
    std::vector<ELFNoteEntry>       m_Notes;
};
