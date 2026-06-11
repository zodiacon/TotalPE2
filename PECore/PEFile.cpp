#include "pch.h"
#include "PEFile.h"

// Pull LIEF only in the .cpp.
// WinTrust.h (included via pch.h) defines SPC_INDIRECT_DATA_OBJID as a macro
// which conflicts with the static constexpr member of the same name in LIEF.
// Undefine it before pulling in LIEF's signature headers.
#pragma push_macro("SPC_INDIRECT_DATA_OBJID")
#undef SPC_INDIRECT_DATA_OBJID
#include <LIEF/PE.hpp>
#pragma pop_macro("SPC_INDIRECT_DATA_OBJID")
#include <LIEF/PE/ResourceData.hpp>
#include <LIEF/PE/ResourceDirectory.hpp>
#include <LIEF/PE/exceptions_info/RuntimeFunctionX64.hpp>

// ── helpers ───────────────────────────────────────────────────────────────

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

static std::wstring U16ToWide(std::u16string_view u) {
    // u16string_view and wstring_view share the same code unit size on Windows
    return std::wstring(reinterpret_cast<const wchar_t*>(u.data()), u.size());
}

// ── opaque LIEF wrapper ───────────────────────────────────────────────────

struct PEFile::LiefImpl {
    std::unique_ptr<LIEF::PE::Binary> binary;
};

// ── PEFile public API ─────────────────────────────────────────────────────

PEFile::PEFile()  = default;
PEFile::~PEFile() = default;
PEFile::PEFile(PEFile&&) noexcept            = default;
PEFile& PEFile::operator=(PEFile&&) noexcept = default;

bool PEFile::Open(std::wstring_view path) {
    Close();

    if (!OpenRaw(path))
        return false;

    LIEF::PE::ParserConfig cfg;
    cfg.parse_exceptions = true;
    auto binary = LIEF::PE::Parser::parse(WideToUtf8(path), cfg);
    if (!binary) {
        BinaryFile::Close();
        return false;
    }

    m_Impl = std::make_unique<LiefImpl>();
    m_Impl->binary = std::move(binary);
    m_Format = BinaryFormat::PE;

    BuildCaches();
    return true;
}

void PEFile::Close() {
    BinaryFile::Close();
    m_Impl.reset();
    m_Info = {};
    m_NtHeader = {};
    m_DosHeader = {};
    m_Sections.clear();
    m_DataDirs.clear();
    m_Imports.clear();
    m_Exports = {};
    m_Debug.clear();
    m_Security.clear();
    m_Tls = {};
    m_LoadConfig = {};
    m_Relocations.clear();
    m_Exceptions.clear();
    m_DelayImports.clear();
    m_FlatResources.clear();
    m_ResRawData.clear();
}

PEFileInfo const*        PEFile::GetFileInfo()    const { return &m_Info; }
PENtHeader const*        PEFile::GetNTHeader()    const { return &m_NtHeader; }
IMAGE_DOS_HEADER const*  PEFile::GetMSDOSHeader() const { return &m_DosHeader; }
PESECHDR_VEC const*      PEFile::GetSecHeaders()  const { return &m_Sections; }
PEDATADIR_VEC const*     PEFile::GetDataDirs()    const { return &m_DataDirs; }
PEIMPORT_VEC const*      PEFile::GetImport()      const { return &m_Imports; }
PEExport const*          PEFile::GetExport()      const { return m_Info.HasExport ? &m_Exports : nullptr; }
PEDEBUG_VEC const*       PEFile::GetDebug()       const { return &m_Debug; }
PESECURITY_VEC const*    PEFile::GetSecurity()    const { return &m_Security; }
PETls const*             PEFile::GetTLS()         const { return m_Info.HasTLS ? &m_Tls : nullptr; }
PELoadConfig const*      PEFile::GetLoadConfig()  const { return m_Info.HasLoadConfig ? &m_LoadConfig : nullptr; }
PERELOC_VEC const*       PEFile::GetRelocations() const { return &m_Relocations; }
PEEXCEPTION_VEC const*   PEFile::GetExceptions()  const { return &m_Exceptions; }
PEDELAYIMPORT_VEC const* PEFile::GetDelayImport() const { return &m_DelayImports; }
PERichHeader const*      PEFile::GetRichHeader()  const { return m_Info.HasRichHdr ? &m_RichHeader : nullptr; }

PERESFLAT_VEC const& PEFile::GetFlatResources() const { return m_FlatResources; }

uint64_t PEFile::GetOffsetFromRVA(ULONGLONG rva) const noexcept {
    return m_Impl->binary->rva_to_offset(rva);
}

// ── cache population ──────────────────────────────────────────────────────

void PEFile::BuildCaches() {
    auto& pe = *m_Impl->binary;

    // FileInfo
    auto magic = pe.optional_header().magic();
    m_Info.IsPE32        = (magic == LIEF::PE::PE_TYPE::PE32);
    m_Info.IsPE64        = (magic == LIEF::PE::PE_TYPE::PE32_PLUS);
    m_Info.HasRichHdr    = pe.rich_header() != nullptr;
    m_Info.HasResource   = pe.resources() != nullptr;
    m_Info.HasImport     = !pe.imports().empty();
    m_Info.HasExport     = pe.has_exports();
    m_Info.HasDebug      = !pe.debug().empty();
    m_Info.HasReloc      = !pe.relocations().empty();
    m_Info.HasException  = pe.has_exceptions();
    m_Info.HasSecurity   = !pe.signatures().empty();
    m_Info.HasTLS        = pe.has_tls();
    m_Info.HasLoadConfig = pe.has_configuration();
    m_Info.HasDelayImport= !pe.delay_imports().empty();
    m_Info.HasSections   = !pe.sections().empty();
    m_Info.HasDataDirs   = !pe.data_directories().empty();
    // COM descriptor = CLR runtime header data directory has non-zero size
    if (auto* clrDir = pe.data_directory(LIEF::PE::DataDirectory::TYPES::CLR_RUNTIME_HEADER))
        m_Info.HasCOMDescr = (clrDir->size() > 0);
    else
        m_Info.HasCOMDescr = false;

    // DOS header
    if (m_FileSize >= sizeof(IMAGE_DOS_HEADER))
        memcpy(&m_DosHeader, m_Raw.get(), sizeof(IMAGE_DOS_HEADER));

    // NT header — cast raw bytes (LIEF already validated the signature)
    DWORD e_lfanew = m_DosHeader.e_lfanew;
    m_NtHeader.dwOffset = e_lfanew;
    if (m_Info.IsPE64) {
        if (e_lfanew + sizeof(IMAGE_NT_HEADERS64) <= m_FileSize)
            memcpy(&m_NtHeader.NTHdr64, m_Raw.get() + e_lfanew, sizeof(IMAGE_NT_HEADERS64));
    }
    else {
        if (e_lfanew + sizeof(IMAGE_NT_HEADERS32) <= m_FileSize)
            memcpy(&m_NtHeader.NTHdr32, m_Raw.get() + e_lfanew, sizeof(IMAGE_NT_HEADERS32));
    }

    // Populate BinaryFile base fields
    m_Is32Bit   = m_Info.IsPE32;
    m_ImageBase = m_Info.IsPE64 ? m_NtHeader.NTHdr64.OptionalHeader.ImageBase
                                : (uint64_t)m_NtHeader.NTHdr32.OptionalHeader.ImageBase;
    {
        uint32_t entryRva = m_Info.IsPE64 ? m_NtHeader.NTHdr64.OptionalHeader.AddressOfEntryPoint
                                           : m_NtHeader.NTHdr32.OptionalHeader.AddressOfEntryPoint;
        m_EntryPoint = m_ImageBase + entryRva;
    }

    // Sections
    for (auto const& s : pe.sections()) {
        PESectionHeader sec{};
        auto nameStr = s.name();
        memcpy(sec.SecHdr.Name, nameStr.c_str(), std::min(nameStr.size(), (size_t)8));
        sec.SecHdr.Misc.VirtualSize    = s.virtual_size();
        sec.SecHdr.VirtualAddress      = (DWORD)s.virtual_address();
        sec.SecHdr.SizeOfRawData       = (DWORD)s.size();
        sec.SecHdr.PointerToRawData    = (DWORD)s.offset();
        sec.SecHdr.Characteristics     = s.characteristics();
        sec.SecHdr.NumberOfLinenumbers = s.numberof_line_numbers();
        sec.SecHdr.NumberOfRelocations = s.numberof_relocations();
        sec.SectionName = nameStr;
        m_Sections.push_back(std::move(sec));
    }

    // Data directories (LIEF always gives exactly 16 entries)
    for (auto const& d : pe.data_directories()) {
        PEDataDirectory dir{};
        dir.DataDir.VirtualAddress = d.RVA();
        dir.DataDir.Size           = d.size();
        if (d.has_section())
            dir.Section = d.section()->name();
        m_DataDirs.push_back(std::move(dir));
    }

    // Imports
    for (auto const& mod : pe.imports()) {
        PEImport imp{};
        imp.ModuleName               = mod.name();
        imp.ImportDesc.TimeDateStamp = mod.timedatestamp();
        imp.ImportDesc.Name          = mod.import_address_table_rva(); // best approximation

        for (auto const& e : mod.entries()) {
            PEImportFunction fn{};
            fn.FuncName = e.name();
            fn.ImpByName.Hint = e.hint();

            auto nameLen = std::min(e.name().size(), sizeof(fn.ImpByName.Name) - 1);
            memcpy(fn.ImpByName.Name, e.name().c_str(), nameLen);

            if (e.is_ordinal()) {
                // Signal ordinal-only by leaving Name[0] == 0
                fn.ImpByName.Name[0] = 0;
                if (m_Info.IsPE64)
                    fn.unThunk.Thunk64.u1.Ordinal = (ULONGLONG)e.ordinal() | IMAGE_ORDINAL_FLAG64;
                else
                    fn.unThunk.Thunk32.u1.Ordinal = (ULONG)e.ordinal() | IMAGE_ORDINAL_FLAG32;
            }
            imp.ImportFunc.push_back(std::move(fn));
        }
        m_Imports.push_back(std::move(imp));
    }

    // Exports
    if (auto const* expPtr = pe.get_export()) {
        m_Exports.ModuleName = expPtr->name();
        m_Exports.ExportDesc.Base = expPtr->ordinal_base();

        for (auto const& e : expPtr->entries()) {
            PEExportFunction fn{};
            fn.FuncName  = e.name();
            fn.FuncRVA   = (DWORD)e.value();
            fn.Ordinal   = (DWORD)e.ordinal();
            fn.NameRVA   = 0; // LIEF doesn't expose individual name RVAs easily
            if (e.is_forwarded())
                fn.ForwarderName = e.forward_information().function;
            m_Exports.Funcs.push_back(std::move(fn));
        }
    }

    // Debug
    for (auto const& dbg : pe.debug()) {
        PEDebug d{};
        d.Directory.Characteristics  = dbg.characteristics();
        d.Directory.TimeDateStamp    = dbg.timestamp();
        d.Directory.MajorVersion     = (WORD)dbg.major_version();
        d.Directory.MinorVersion     = (WORD)dbg.minor_version();
        d.Directory.Type             = (DWORD)dbg.type();
        d.Directory.SizeOfData       = dbg.sizeof_data();
        d.Directory.AddressOfRawData = dbg.addressof_rawdata();
        d.Directory.PointerToRawData = dbg.pointerto_rawdata();
        if (auto const* cv = dbg.as<LIEF::PE::CodeViewPDB>())
            d.PdbPath = cv->filename();
        m_Debug.push_back(std::move(d));
    }

    // Security — the certificate table uses raw file offsets, not RVAs.
    // Walk the raw bytes directly to reconstruct WIN_CERTIFICATE records.
    if (m_Info.HasSecurity) {
        // DATA_DIRECTORY[4] (certificate table) holds a file offset, not RVA
        constexpr int CERT_DIR = 4;
        if ((size_t)CERT_DIR < m_DataDirs.size()) {
            // offsetof gives the true header size (8); sizeof(WIN_CERTIFICATE) is 12
            // because BYTE bCertificate[ANYSIZE_ARRAY] pads the struct to 12.
            constexpr uint32_t CertHdrSize = offsetof(WIN_CERTIFICATE, bCertificate);
            uint32_t offset = m_DataDirs[CERT_DIR].DataDir.VirtualAddress; // file offset for this dir
            uint32_t limit  = offset + m_DataDirs[CERT_DIR].DataDir.Size;
            while (offset + CertHdrSize <= limit && offset + CertHdrSize <= m_FileSize) {
                auto* wc = (WIN_CERTIFICATE*)(m_Raw.get() + offset);
                if (wc->dwLength < CertHdrSize) break;

                PESecurity sec{};
                sec.Offset                   = offset;
                sec.WinCert.dwLength         = wc->dwLength;
                sec.WinCert.wRevision        = wc->wRevision;
                sec.WinCert.wCertificateType = wc->wCertificateType;

                uint32_t dataLen = wc->dwLength - CertHdrSize;
                if (dataLen && offset + CertHdrSize + dataLen <= m_FileSize) {
                    sec.CertData.assign(m_Raw.get() + offset + CertHdrSize,
                                       m_Raw.get() + offset + CertHdrSize + dataLen);
                }
                m_Security.push_back(std::move(sec));

                // Entries are DWORD-aligned
                offset += (wc->dwLength + 7) & ~7u;
            }
        }
    }

    // TLS
    if (auto const* tlsPtr = pe.tls()) {
        auto const& t = *tlsPtr;
        auto [rawStart, rawEnd] = t.addressof_raw_data();
        auto* tlsDir = pe.data_directory(LIEF::PE::DataDirectory::TYPES::TLS_TABLE);
        m_Tls.dwOffset = tlsDir ? (DWORD)GetOffsetFromRVA(tlsDir->RVA()) : 0;

        if (m_Info.IsPE64) {
            m_Tls.unTLS.TLSDir64.StartAddressOfRawData = rawStart;
            m_Tls.unTLS.TLSDir64.EndAddressOfRawData   = rawEnd;
            m_Tls.unTLS.TLSDir64.AddressOfCallBacks    = t.addressof_callbacks();
            m_Tls.unTLS.TLSDir64.AddressOfIndex        = t.addressof_index();
            m_Tls.unTLS.TLSDir64.Characteristics       = t.characteristics();
        }
        else {
            m_Tls.unTLS.TLSDir32.StartAddressOfRawData = (DWORD)rawStart;
            m_Tls.unTLS.TLSDir32.EndAddressOfRawData   = (DWORD)rawEnd;
            m_Tls.unTLS.TLSDir32.AddressOfCallBacks    = (DWORD)t.addressof_callbacks();
            m_Tls.unTLS.TLSDir32.AddressOfIndex        = (DWORD)t.addressof_index();
            m_Tls.unTLS.TLSDir32.Characteristics       = t.characteristics();
        }
        for (uint64_t cb : t.callbacks())
            m_Tls.TLSCallbacks.push_back(cb);
    }

    // Load Configuration — cast raw bytes (field set varies with OS/SDK version)
    if (pe.has_configuration()) {
        if (auto* lcDir = pe.data_directory(LIEF::PE::DataDirectory::TYPES::LOAD_CONFIG_TABLE)) {
            auto off = GetOffsetFromRVA(lcDir->RVA());
            m_LoadConfig.dwOffset = (DWORD)off;
            if (m_Info.IsPE64) {
                uint32_t copySize = std::min((uint32_t)sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64),
                                            (uint32_t)(m_FileSize - off));
                memcpy(&m_LoadConfig.LCD64, m_Raw.get() + off, copySize);
            }
            else {
                uint32_t copySize = std::min((uint32_t)sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32),
                                            (uint32_t)(m_FileSize - off));
                memcpy(&m_LoadConfig.LCD32, m_Raw.get() + off, copySize);
            }
        }
    }

    // Relocations
    for (auto const& r : pe.relocations()) {
        PERelocation rel{};
        rel.BaseReloc.VirtualAddress = (DWORD)r.virtual_address();
        for (auto const& e : r.entries()) {
            PERelocData rd{};
            rd.RelocType   = (WORD)(int)e.type();
            rd.RelocOffset = e.position();
            rel.RelocData.push_back(rd);
        }
        rel.BaseReloc.SizeOfBlock = (DWORD)(sizeof(IMAGE_BASE_RELOCATION) + rel.RelocData.size() * sizeof(WORD));
        m_Relocations.push_back(std::move(rel));
    }

    // Exceptions
    for (auto const& exc : pe.exceptions()) {
        PEException e{};
        e.RuntimeFuncEntry.BeginAddress = exc.rva_start();
        if (auto const* x64 = exc.as<LIEF::PE::RuntimeFunctionX64>()) {
            e.RuntimeFuncEntry.EndAddress        = x64->rva_end();
            e.RuntimeFuncEntry.UnwindInfoAddress = x64->unwind_rva();
        }
        m_Exceptions.push_back(e);
    }

    // Delay Imports
    for (auto const& di : pe.delay_imports()) {
        PEDelayImport dim{};
        dim.ModuleName = di.name();
        dim.DelayImpDesc.grAttrs     = di.attribute();
        dim.DelayImpDesc.rvaDLLName  = 0; // LIEF doesn't expose individual name RVAs
        dim.DelayImpDesc.rvaHmod     = di.handle();
        dim.DelayImpDesc.rvaIAT      = di.iat();
        dim.DelayImpDesc.rvaINT      = di.names_table();
        dim.DelayImpDesc.rvaBoundIAT = di.biat();
        dim.DelayImpDesc.rvaUnloadIAT= di.uiat();
        dim.DelayImpDesc.dwTimeStamp = di.timestamp();

        for (auto const& e : di.entries()) {
            PEDelayImportFunc fn{};
            fn.FuncName = e.name();
            fn.ImpByName.Hint = e.hint();
            auto nameLen = std::min(e.name().size(), sizeof(fn.ImpByName.Name) - 1);
            memcpy(fn.ImpByName.Name, e.name().c_str(), nameLen);
            if (e.is_ordinal()) fn.ImpByName.Name[0] = 0;

            if (m_Info.IsPE64) {
                fn.unThunk.st64.ImportAddressTable.u1.Function = e.value();
                fn.unThunk.st64.ImportNameTable.u1.Function    = e.iat_value();
            }
            else {
                fn.unThunk.st32.ImportAddressTable.u1.Function = (DWORD)e.value();
                fn.unThunk.st32.ImportNameTable.u1.Function    = (DWORD)e.iat_value();
            }
            dim.DelayImpFunc.push_back(std::move(fn));
        }
        m_DelayImports.push_back(std::move(dim));
    }

    // Rich Header
    if (auto const* rh = pe.rich_header()) {
        m_RichHeader.Key = rh->key();

        // Scan raw bytes for the "Rich" signature to find the file offset
        DWORD scanLimit = std::min<DWORD>(m_DosHeader.e_lfanew, m_FileSize);
        auto* p = m_Raw.get();
        for (DWORD i = 0; i + 8 <= scanLimit; i += 4) {
            if (*reinterpret_cast<const DWORD*>(p + i) == 0x68636952u /*'hciR'*/)
            {
                // Walk backwards to find "DanS" XOR key
                for (DWORD j = i; j >= 4; j -= 4) {
                    DWORD v = *reinterpret_cast<const DWORD*>(p + j - 4) ^ rh->key();
                    if (v == 0x536E6144u /*'SnaD'*/) { m_RichHeader.Offset = j - 4; break; }
                }
                break;
            }
        }

        for (auto const& e : rh->entries()) {
            PERichEntry entry;
            entry.ProductId = (WORD)e.id();
            entry.BuildId   = (WORD)e.build_id();
            entry.Count     = e.count();
            m_RichHeader.Entries.push_back(entry);
        }
    }

    // Generic collections for BinaryFile base
    for (auto const& sec : m_Sections)
        m_GenSections.push_back({ Utf8ToWide(sec.SectionName), sec.SecHdr.VirtualAddress,
                                   sec.SecHdr.PointerToRawData, sec.SecHdr.SizeOfRawData,
                                   sec.SecHdr.Characteristics });

    for (auto const& imp : m_Imports) {
        GenericImport gi;
        gi.Library = Utf8ToWide(imp.ModuleName);
        for (auto const& fn : imp.ImportFunc)
            if (fn.ImpByName.Name[0])
                gi.Functions.push_back(Utf8ToWide(fn.FuncName));
        m_GenImports.push_back(std::move(gi));
    }

    if (m_Info.HasExport)
        for (auto const& fn : m_Exports.Funcs)
            m_GenExports.push_back({ Utf8ToWide(fn.FuncName), m_ImageBase + fn.FuncRVA });

    // Resources
    BuildResources();
}

// ── Resource tree flattener ───────────────────────────────────────────────

void PEFile::BuildResources() {
    auto const* root = m_Impl->binary->resources();
    if (!root) return;

    m_ResRawData.reserve(4 * 1024 * 1024);

    for (auto const& typeNode : root->childs()) {
        std::wstring typeStr;
        WORD typeID = 0;
        if (typeNode.has_name())
            typeStr = U16ToWide(typeNode.name());
        else
            typeID = (WORD)typeNode.id();

        for (auto const& nameNode : typeNode.childs()) {
            std::wstring nameStr;
            WORD nameID = 0;
            if (nameNode.has_name())
                nameStr = U16ToWide(nameNode.name());
            else
                nameID = (WORD)nameNode.id();

            for (auto const& langNode : nameNode.childs()) {
                auto const* data = langNode.cast<LIEF::PE::ResourceData>();
                if (!data) continue;

                PEResFlat flat{};
                flat.TypeStr = typeStr;
                flat.TypeID  = typeID;
                flat.NameStr = nameStr;
                flat.NameID  = nameID;
                flat.LangID  = (WORD)langNode.id();

                auto const content = data->content(); // span<const uint8_t>
                size_t base = m_ResRawData.size();
                auto const* src = reinterpret_cast<const std::byte*>(content.data());
                m_ResRawData.insert(m_ResRawData.end(), src, src + content.size());
                flat.Data = std::span<const std::byte>(m_ResRawData.data() + base, content.size());
                m_FlatResources.push_back(std::move(flat));
            }
        }
    }
}
