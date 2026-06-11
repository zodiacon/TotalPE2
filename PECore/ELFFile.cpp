#include "pch.h"
#include "ELFFile.h"

#include <LIEF/ELF.hpp>
#include <LIEF/ELF/DynamicEntryLibrary.hpp>
#include <unordered_set>
#include <LIEF/ELF/DynamicSharedObject.hpp>
#include <LIEF/ELF/DynamicEntryRpath.hpp>
#include <LIEF/ELF/DynamicEntryRunPath.hpp>

static std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring SectionFlagsToStr(uint64_t flags) {
    std::wstring s;
    if (flags & 0x1)   s += L'W'; // SHF_WRITE
    if (flags & 0x2)   s += L'A'; // SHF_ALLOC
    if (flags & 0x4)   s += L'X'; // SHF_EXECINSTR
    if (flags & 0x10)  s += L'M'; // SHF_MERGE
    if (flags & 0x20)  s += L'S'; // SHF_STRINGS
    if (flags & 0x400) s += L'T'; // SHF_TLS
    return s;
}

static std::wstring SegmentFlagsToStr(uint32_t flags) {
    std::wstring s;
    if (flags & 0x4) s += L'R'; // PF_R
    if (flags & 0x2) s += L'W'; // PF_W
    if (flags & 0x1) s += L'X'; // PF_X
    return s;
}

struct ELFFile::LiefImpl {
    std::unique_ptr<LIEF::ELF::Binary> binary;
};

ELFFile::ELFFile()  = default;
ELFFile::~ELFFile() = default;
ELFFile::ELFFile(ELFFile&&) noexcept            = default;
ELFFile& ELFFile::operator=(ELFFile&&) noexcept = default;

bool ELFFile::Open(std::wstring_view path) {
    Close();

    if (!OpenRaw(path))
        return false;

    auto binary = LIEF::ELF::Parser::parse(WideToUtf8(path));
    if (!binary) {
        BinaryFile::Close();
        return false;
    }

    m_Impl = std::make_unique<LiefImpl>();
    m_Impl->binary = std::move(binary);
    m_Format = BinaryFormat::ELF;

    BuildCaches();
    return true;
}

void ELFFile::Close() {
    BinaryFile::Close();
    m_Impl.reset();
    m_Info        = {};
    m_ELFSections.clear();
    m_Segments.clear();
    m_AllSymbols.clear();
    m_DynamicEntries.clear();
    m_Relocations.clear();
    m_Notes.clear();
}

ELFInfo const&                          ELFFile::GetELFInfo()         const { return m_Info;         }
std::vector<ELFSectionEntry>    const&  ELFFile::GetELFSections()     const { return m_ELFSections;  }
std::vector<ELFSegmentEntry>    const&  ELFFile::GetSegments()         const { return m_Segments;     }
std::vector<ELFSymbolEntry>     const&  ELFFile::GetAllSymbols()       const { return m_AllSymbols;   }
std::vector<ELFDynamicEntry>    const&  ELFFile::GetDynamicEntries()   const { return m_DynamicEntries; }
std::vector<ELFRelocationEntry> const&  ELFFile::GetRelocations()      const { return m_Relocations;  }
std::vector<ELFNoteEntry>       const&  ELFFile::GetNotes()            const { return m_Notes;        }

void ELFFile::BuildCaches() {
    auto& bin = *m_Impl->binary;
    auto& hdr = bin.header();

    m_Is32Bit    = static_cast<size_t>(hdr.identity_class()) == 1; // ELFCLASS32 = 1
    m_ImageBase  = bin.imagebase();
    m_EntryPoint = bin.entrypoint();

    m_Info.Is32Bit        = m_Is32Bit;
    m_Info.IsLittleEndian = static_cast<size_t>(hdr.identity_data()) == 1; // ELFDATA2LSB = 1
    m_Info.ClassName      = m_Is32Bit ? L"ELF32" : L"ELF64";
    m_Info.Endianness     = m_Info.IsLittleEndian ? L"Little Endian" : L"Big Endian";
    m_Info.OSABI          = Utf8ToWide(LIEF::ELF::to_string(hdr.identity_os_abi()));
    m_Info.FileType       = Utf8ToWide(LIEF::ELF::to_string(hdr.file_type()));
    m_Info.Machine        = Utf8ToWide(LIEF::ELF::to_string(hdr.machine_type()));
    m_Info.EntryPoint     = m_EntryPoint;
    m_Info.Flags          = hdr.processor_flag();

    // Sections
    for (auto const& sec : bin.sections()) {
        ELFSectionEntry e;
        e.Name           = Utf8ToWide(sec.name());
        e.Type           = static_cast<uint32_t>(sec.type());
        e.TypeStr        = Utf8ToWide(LIEF::ELF::to_string(sec.type()));
        e.Flags          = sec.flags();
        e.FlagsStr       = SectionFlagsToStr(sec.flags());
        e.VirtualAddress = sec.virtual_address();
        e.FileOffset     = sec.offset();
        e.Size           = sec.size();
        e.Alignment      = sec.alignment();
        e.EntrySize      = static_cast<uint32_t>(sec.entry_size());
        m_ELFSections.push_back(e);

        GenericSection gs;
        gs.Name           = e.Name;
        gs.VirtualAddress = e.VirtualAddress;
        gs.FileOffset     = e.FileOffset;
        gs.Size           = e.Size;
        gs.Flags          = static_cast<uint32_t>(e.Flags);
        m_GenSections.push_back(std::move(gs));
    }
    m_Info.SectionCount = static_cast<uint32_t>(m_ELFSections.size());

    // Segments
    for (auto const& seg : bin.segments()) {
        ELFSegmentEntry e;
        e.Type            = static_cast<uint32_t>(seg.type());
        e.TypeStr         = Utf8ToWide(LIEF::ELF::to_string(seg.type()));
        e.Flags           = static_cast<uint32_t>(seg.flags());
        e.FlagsStr        = SegmentFlagsToStr(e.Flags);
        e.FileOffset      = seg.file_offset();
        e.VirtualAddress  = seg.virtual_address();
        e.PhysicalAddress = seg.physical_address();
        e.FileSize        = seg.physical_size();
        e.MemorySize      = seg.virtual_size();
        e.Alignment       = seg.alignment();
        m_Segments.push_back(std::move(e));
    }
    m_Info.SegmentCount = static_cast<uint32_t>(m_Segments.size());

    // Build set of dynamic symbol addresses for fast lookup
    std::unordered_set<const LIEF::ELF::Symbol*> dynPtrs;
    for (auto const& sym : bin.dynamic_symbols())
        dynPtrs.insert(&sym);

    uint32_t staticCount = 0, dynCount = 0;
    for (auto const& sym : bin.symbols()) {
        bool isDynamic = dynPtrs.count(&sym) > 0;
        ELFSymbolEntry e;
        e.Name          = Utf8ToWide(sym.name());
        e.Value         = sym.value();
        e.Size          = sym.size();
        e.TypeStr       = Utf8ToWide(LIEF::ELF::to_string(sym.type()));
        e.BindingStr    = Utf8ToWide(LIEF::ELF::to_string(sym.binding()));
        e.VisibilityStr = Utf8ToWide(LIEF::ELF::to_string(sym.visibility()));
        e.SectionIndex  = sym.section_idx();
        e.IsDynamic     = isDynamic;

        if (isDynamic) {
            auto binding = static_cast<uint8_t>(sym.binding());
            if (sym.value() != 0 && (binding == 1 || binding == 2)) // STB_GLOBAL=1, STB_WEAK=2
                m_GenExports.push_back({ e.Name, e.Value });
            if (!e.Name.empty())
                m_GenSymbols.push_back({ e.Name, e.Value, e.Size });
            dynCount++;
        } else {
            staticCount++;
        }
        m_AllSymbols.push_back(std::move(e));
    }
    m_Info.StaticSymbolCount  = staticCount;
    m_Info.DynamicSymbolCount = dynCount;

    // Dynamic entries
    for (auto const& de : bin.dynamic_entries()) {
        ELFDynamicEntry e;
        e.TagStr = Utf8ToWide(LIEF::ELF::to_string(de.tag()));
        e.Value  = de.value();

        if (auto* lib = dynamic_cast<const LIEF::ELF::DynamicEntryLibrary*>(&de)) {
            e.ValueStr = Utf8ToWide(lib->name());
            m_GenImports.push_back({ e.ValueStr, {} });
        }
        else if (auto so = dynamic_cast<const LIEF::ELF::DynamicSharedObject*>(&de))
            e.ValueStr = Utf8ToWide(so->name());
        else if (auto rp = dynamic_cast<const LIEF::ELF::DynamicEntryRpath*>(&de))
            e.ValueStr = Utf8ToWide(rp->rpath());
        else if (auto rn = dynamic_cast<const LIEF::ELF::DynamicEntryRunPath*>(&de))
            e.ValueStr = Utf8ToWide(rn->runpath());

        m_DynamicEntries.push_back(std::move(e));
    }
    m_Info.DynamicEntryCount = static_cast<uint32_t>(m_DynamicEntries.size());

    // Relocations
    for (auto const& reloc : bin.relocations()) {
        ELFRelocationEntry e;
        e.Address  = reloc.address();
        e.Type     = static_cast<uint32_t>(reloc.type());
        e.Addend   = reloc.addend();
        e.IsRela   = reloc.is_rela();
        if (reloc.has_symbol() && reloc.symbol() != nullptr)
            e.SymbolName = Utf8ToWide(reloc.symbol()->name());
        if (reloc.has_section() && reloc.section() != nullptr)
            e.SectionName = Utf8ToWide(reloc.section()->name());
        m_Relocations.push_back(std::move(e));
    }
    m_Info.RelocationCount = static_cast<uint32_t>(m_Relocations.size());

    // Notes
    for (auto const& note : bin.notes()) {
        ELFNoteEntry e;
        e.Name            = Utf8ToWide(note.name());
        e.Type            = static_cast<uint32_t>(note.type());
        e.DescriptionSize = static_cast<uint32_t>(note.description().size());
        m_Notes.push_back(std::move(e));
    }
    m_Info.NoteCount = static_cast<uint32_t>(m_Notes.size());
}
