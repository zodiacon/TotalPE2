#pragma once

#include <Windows.h>
#include <WinTrust.h>
#include <delayimp.h>
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <memory>
#include <cstdint>

// ── Lookup maps (drop-in replacement for libpe::Map* used in PEImageView) ──

inline const std::unordered_map<DWORD, std::wstring_view> MapFileHdrMachine{
    { IMAGE_FILE_MACHINE_UNKNOWN,    L"IMAGE_FILE_MACHINE_UNKNOWN" },
    { IMAGE_FILE_MACHINE_TARGET_HOST,L"IMAGE_FILE_MACHINE_TARGET_HOST" },
    { IMAGE_FILE_MACHINE_I386,       L"IMAGE_FILE_MACHINE_I386" },
    { IMAGE_FILE_MACHINE_R3000,      L"IMAGE_FILE_MACHINE_R3000" },
    { IMAGE_FILE_MACHINE_R4000,      L"IMAGE_FILE_MACHINE_R4000" },
    { IMAGE_FILE_MACHINE_R10000,     L"IMAGE_FILE_MACHINE_R10000" },
    { IMAGE_FILE_MACHINE_WCEMIPSV2,  L"IMAGE_FILE_MACHINE_WCEMIPSV2" },
    { IMAGE_FILE_MACHINE_ALPHA,      L"IMAGE_FILE_MACHINE_ALPHA" },
    { IMAGE_FILE_MACHINE_SH3,        L"IMAGE_FILE_MACHINE_SH3" },
    { IMAGE_FILE_MACHINE_SH3DSP,     L"IMAGE_FILE_MACHINE_SH3DSP" },
    { IMAGE_FILE_MACHINE_SH3E,       L"IMAGE_FILE_MACHINE_SH3E" },
    { IMAGE_FILE_MACHINE_SH4,        L"IMAGE_FILE_MACHINE_SH4" },
    { IMAGE_FILE_MACHINE_SH5,        L"IMAGE_FILE_MACHINE_SH5" },
    { IMAGE_FILE_MACHINE_ARM,        L"IMAGE_FILE_MACHINE_ARM" },
    { IMAGE_FILE_MACHINE_THUMB,      L"IMAGE_FILE_MACHINE_THUMB" },
    { IMAGE_FILE_MACHINE_ARMNT,      L"IMAGE_FILE_MACHINE_ARMNT" },
    { IMAGE_FILE_MACHINE_AM33,       L"IMAGE_FILE_MACHINE_AM33" },
    { IMAGE_FILE_MACHINE_POWERPC,    L"IMAGE_FILE_MACHINE_POWERPC" },
    { IMAGE_FILE_MACHINE_POWERPCFP,  L"IMAGE_FILE_MACHINE_POWERPCFP" },
    { IMAGE_FILE_MACHINE_IA64,       L"IMAGE_FILE_MACHINE_IA64" },
    { IMAGE_FILE_MACHINE_MIPS16,     L"IMAGE_FILE_MACHINE_MIPS16" },
    { IMAGE_FILE_MACHINE_ALPHA64,    L"IMAGE_FILE_MACHINE_ALPHA64" },
    { IMAGE_FILE_MACHINE_MIPSFPU,    L"IMAGE_FILE_MACHINE_MIPSFPU" },
    { IMAGE_FILE_MACHINE_MIPSFPU16,  L"IMAGE_FILE_MACHINE_MIPSFPU16" },
    { IMAGE_FILE_MACHINE_TRICORE,    L"IMAGE_FILE_MACHINE_TRICORE" },
    { IMAGE_FILE_MACHINE_CEF,        L"IMAGE_FILE_MACHINE_CEF" },
    { IMAGE_FILE_MACHINE_EBC,        L"IMAGE_FILE_MACHINE_EBC" },
    { IMAGE_FILE_MACHINE_AMD64,      L"IMAGE_FILE_MACHINE_AMD64" },
    { IMAGE_FILE_MACHINE_M32R,       L"IMAGE_FILE_MACHINE_M32R" },
    { IMAGE_FILE_MACHINE_ARM64,      L"IMAGE_FILE_MACHINE_ARM64" },
    { IMAGE_FILE_MACHINE_CEE,        L"IMAGE_FILE_MACHINE_CEE" },
};

inline const std::unordered_map<DWORD, std::wstring_view> MapOptHdrMagic{
    { IMAGE_NT_OPTIONAL_HDR32_MAGIC, L"IMAGE_NT_OPTIONAL_HDR32_MAGIC" },
    { IMAGE_NT_OPTIONAL_HDR64_MAGIC, L"IMAGE_NT_OPTIONAL_HDR64_MAGIC" },
    { IMAGE_ROM_OPTIONAL_HDR_MAGIC,  L"IMAGE_ROM_OPTIONAL_HDR_MAGIC" },
};

inline const std::unordered_map<DWORD, std::wstring_view> MapOptHdrSubsystem{
    { IMAGE_SUBSYSTEM_UNKNOWN,                  L"IMAGE_SUBSYSTEM_UNKNOWN" },
    { IMAGE_SUBSYSTEM_NATIVE,                   L"IMAGE_SUBSYSTEM_NATIVE" },
    { IMAGE_SUBSYSTEM_WINDOWS_GUI,              L"IMAGE_SUBSYSTEM_WINDOWS_GUI" },
    { IMAGE_SUBSYSTEM_WINDOWS_CUI,              L"IMAGE_SUBSYSTEM_WINDOWS_CUI" },
    { IMAGE_SUBSYSTEM_OS2_CUI,                  L"IMAGE_SUBSYSTEM_OS2_CUI" },
    { IMAGE_SUBSYSTEM_POSIX_CUI,                L"IMAGE_SUBSYSTEM_POSIX_CUI" },
    { IMAGE_SUBSYSTEM_NATIVE_WINDOWS,           L"IMAGE_SUBSYSTEM_NATIVE_WINDOWS" },
    { IMAGE_SUBSYSTEM_WINDOWS_CE_GUI,           L"IMAGE_SUBSYSTEM_WINDOWS_CE_GUI" },
    { IMAGE_SUBSYSTEM_EFI_APPLICATION,          L"IMAGE_SUBSYSTEM_EFI_APPLICATION" },
    { IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER,  L"IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER" },
    { IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER,       L"IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER" },
    { IMAGE_SUBSYSTEM_EFI_ROM,                  L"IMAGE_SUBSYSTEM_EFI_ROM" },
    { IMAGE_SUBSYSTEM_XBOX,                     L"IMAGE_SUBSYSTEM_XBOX" },
    { IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION, L"IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION" },
    { IMAGE_SUBSYSTEM_XBOX_CODE_CATALOG,        L"IMAGE_SUBSYSTEM_XBOX_CODE_CATALOG" },
};

// ── PE data structures ─────────────────────────────────────────────────────

struct PEFileInfo {
    bool IsPE32{}, IsPE64{},
         HasRichHdr{}, HasResource{}, HasImport{}, HasExport{}, HasDebug{},
         HasReloc{}, HasException{}, HasSecurity{}, HasTLS{}, HasLoadConfig{},
         HasBoundImport{}, HasDelayImport{}, HasCOMDescr{},
         HasSections{}, HasDataDirs{};
};

struct PENtHeader {
    DWORD dwOffset{};
    union {
        IMAGE_NT_HEADERS32 NTHdr32;
        IMAGE_NT_HEADERS64 NTHdr64;
    };
};

struct PESectionHeader {
    DWORD                Offset{};
    IMAGE_SECTION_HEADER SecHdr{};
    std::string          SectionName;
};
using PESECHDR_VEC = std::vector<PESectionHeader>;

struct PEDataDirectory {
    IMAGE_DATA_DIRECTORY DataDir{};
    std::string          Section;
};
using PEDATADIR_VEC = std::vector<PEDataDirectory>;

struct PEExportFunction {
    DWORD       FuncRVA{};
    DWORD       Ordinal{};
    DWORD       NameRVA{};
    std::string FuncName;
    std::string ForwarderName;
};

struct PEExport {
    DWORD                         Offset{};
    IMAGE_EXPORT_DIRECTORY        ExportDesc{};
    std::string                   ModuleName;
    std::vector<PEExportFunction> Funcs;
};

struct PEImportFunction {
    struct {
        WORD Hint{};
        char Name[256]{};   // enough for any real import name
    } ImpByName;
    union {
        IMAGE_THUNK_DATA32 Thunk32{};
        IMAGE_THUNK_DATA64 Thunk64;
    } unThunk;
    std::string FuncName;
};

struct PEImport {
    DWORD                         Offset{};
    IMAGE_IMPORT_DESCRIPTOR       ImportDesc{};
    std::string                   ModuleName;
    std::vector<PEImportFunction> ImportFunc;
};
using PEIMPORT_VEC = std::vector<PEImport>;

// Resource flat entry — owns its string data (not views into file bytes)
struct PEResFlat {
    std::span<const std::byte> Data{};
    std::wstring               TypeStr;
    std::wstring               NameStr;
    std::wstring               LangStr;
    WORD                       TypeID{};
    WORD                       NameID{};
    WORD                       LangID{};
};
using PERESFLAT_VEC = std::vector<PEResFlat>;

struct PEDebug {
    IMAGE_DEBUG_DIRECTORY Directory{};
    std::string           PdbPath;
};
using PEDEBUG_VEC = std::vector<PEDebug>;

struct PESecurity {
    DWORD           Offset{};
    WIN_CERTIFICATE WinCert{};
    std::vector<BYTE> CertData; // full certificate bytes (after WIN_CERTIFICATE header)
};
using PESECURITY_VEC = std::vector<PESecurity>;

struct PERelocData {
    WORD RelocType{};
    WORD RelocOffset{};
};

struct PERelocation {
    IMAGE_BASE_RELOCATION    BaseReloc{};
    std::vector<PERelocData> RelocData;
};
using PERELOC_VEC = std::vector<PERelocation>;

struct PEException {
    DWORD                       Offset{};
    IMAGE_RUNTIME_FUNCTION_ENTRY RuntimeFuncEntry{};
};
using PEEXCEPTION_VEC = std::vector<PEException>;

struct PEDelayImportFunc {
    struct {
        WORD Hint{};
        char Name[256]{};
    } ImpByName;
    union {
        struct {
            IMAGE_THUNK_DATA32 ImportAddressTable;
            IMAGE_THUNK_DATA32 ImportNameTable;
            IMAGE_THUNK_DATA32 BoundImportAddressTable;
            IMAGE_THUNK_DATA32 UnloadInformationTable;
        } st32;
        struct {
            IMAGE_THUNK_DATA64 ImportAddressTable;
            IMAGE_THUNK_DATA64 ImportNameTable;
            IMAGE_THUNK_DATA64 BoundImportAddressTable;
            IMAGE_THUNK_DATA64 UnloadInformationTable;
        } st64;
    } unThunk{};
    std::string FuncName;
};

struct PEDelayImport {
    DWORD                           Offset{};
    ImgDelayDescr                   DelayImpDesc{};
    std::string                     ModuleName;
    std::vector<PEDelayImportFunc>  DelayImpFunc;
};
using PEDELAYIMPORT_VEC = std::vector<PEDelayImport>;

struct PETls {
    DWORD dwOffset{};
    union {
        IMAGE_TLS_DIRECTORY32 TLSDir32{};
        IMAGE_TLS_DIRECTORY64 TLSDir64;
    } unTLS;
    std::vector<ULONGLONG> TLSCallbacks;
};

struct PELoadConfig {
    DWORD dwOffset{};
    IMAGE_LOAD_CONFIG_DIRECTORY32 LCD32{};
    IMAGE_LOAD_CONFIG_DIRECTORY64 LCD64{};
};

struct PERichEntry {
    WORD  ProductId{};
    WORD  BuildId{};
    DWORD Count{};
};

struct PERichHeader {
    DWORD                    Key{};
    DWORD                    Offset{};   // file offset of the "DanS" marker
    std::vector<PERichEntry> Entries;
};

// ── PEFile ─────────────────────────────────────────────────────────────────

class PEFile {
public:
    PEFile();
    ~PEFile();

    // Non-copyable, movable
    PEFile(const PEFile&)            = delete;
    PEFile& operator=(const PEFile&) = delete;
    PEFile(PEFile&&)                 noexcept;
    PEFile& operator=(PEFile&&)      noexcept;

    bool Open(std::wstring_view path);
    void Close();

    std::wstring const& GetPath()     const;
    uint32_t            GetFileSize() const;

    bool Read(uint32_t offset, uint32_t size, void* buffer) const;
    template<typename T>
    T Read(uint32_t offset) const {
        T value{};
        Read(offset, sizeof(T), &value);
        return value;
    }

    const BYTE*                GetData()                              const;
    std::span<const std::byte> GetSpan(uint32_t offset, uint32_t size) const;

    explicit operator bool() const;

    // PE accessors (formerly reached through operator->() on libpe::Ilibpe*)
    PEFileInfo const*              GetFileInfo()    const;
    PENtHeader const*              GetNTHeader()    const;
    IMAGE_DOS_HEADER const*        GetMSDOSHeader() const;
    PESECHDR_VEC const*            GetSecHeaders()  const;
    PEDATADIR_VEC const*           GetDataDirs()    const;
    PEIMPORT_VEC const*            GetImport()      const;
    PEExport const*                GetExport()      const;
    PEDEBUG_VEC const*             GetDebug()       const;
    PESECURITY_VEC const*          GetSecurity()    const;
    PETls const*                   GetTLS()         const;
    PELoadConfig const*            GetLoadConfig()  const;
    PERELOC_VEC const*             GetRelocations() const;
    PEEXCEPTION_VEC const*         GetExceptions()  const;
    PEDELAYIMPORT_VEC const*       GetDelayImport() const;
    PERichHeader const*            GetRichHeader()  const;

    uint64_t  GetOffsetFromRVA(ULONGLONG rva) const noexcept;
    ULONGLONG GetImageBase()                  const noexcept;

    // Flat resource list (replaces libpe::Ilibpe::FlatResources)
    PERESFLAT_VEC const& GetFlatResources() const;

private:
    void BuildCaches();
    void BuildResources();

    // LIEF binary stored as opaque pointer to avoid pulling LIEF headers into
    // every TU that includes PEFile.h.
    struct LiefImpl;
    std::unique_ptr<LiefImpl> m_Impl;

    wil::unique_mapview_ptr<uint8_t> m_Raw;
    std::wstring         m_Path;

    PEFileInfo       m_Info{};
    PENtHeader       m_NtHeader{};
    IMAGE_DOS_HEADER m_DosHeader{};
    DWORD            m_FileSize;

    PESECHDR_VEC      m_Sections;
    PEDATADIR_VEC     m_DataDirs;
    PEIMPORT_VEC      m_Imports;
    PEExport          m_Exports{};
    PEDEBUG_VEC       m_Debug;
    PESECURITY_VEC    m_Security;
    PETls             m_Tls{};
    PELoadConfig      m_LoadConfig{};
    PERELOC_VEC       m_Relocations;
    PEEXCEPTION_VEC   m_Exceptions;
    PEDELAYIMPORT_VEC m_DelayImports;
    PERESFLAT_VEC     m_FlatResources;
    PERichHeader      m_RichHeader{};
    std::vector<std::byte> m_ResRawData; // backing store for PEResFlat::Data spans
};
