#include "pch.h"
#include "BinaryFile.h"

BinaryFile::BinaryFile()  = default;
BinaryFile::~BinaryFile() = default;
BinaryFile::BinaryFile(BinaryFile&&) noexcept            = default;
BinaryFile& BinaryFile::operator=(BinaryFile&&) noexcept = default;

void BinaryFile::Close() {
    m_Raw.reset();
    m_Path.clear();
    m_FileSize   = 0;
    m_Format     = BinaryFormat::Unknown;
    m_EntryPoint = 0;
    m_ImageBase  = 0;
    m_Is32Bit    = false;
    m_GenSections.clear();
    m_GenSymbols.clear();
    m_GenImports.clear();
    m_GenExports.clear();
}

BinaryFile::operator bool()          const { return !m_Path.empty(); }
BinaryFormat BinaryFile::GetFormat() const noexcept { return m_Format; }
std::wstring const& BinaryFile::GetPath()      const { return m_Path;      }
uint32_t     BinaryFile::GetFileSize()         const { return m_FileSize;  }
uint64_t     BinaryFile::GetEntryPoint()       const noexcept { return m_EntryPoint; }
uint64_t     BinaryFile::GetImageBase()        const noexcept { return m_ImageBase;  }
bool         BinaryFile::Is32Bit()             const noexcept { return m_Is32Bit;    }

bool BinaryFile::Read(uint32_t offset, uint32_t size, void* buffer) const {
    if (offset + (size_t)size > m_FileSize) return false;
    memcpy(buffer, m_Raw.get() + offset, size);
    return true;
}

const BYTE* BinaryFile::GetData() const {
    return m_Raw.get();
}

std::span<const std::byte> BinaryFile::GetSpan(uint32_t offset, uint32_t size) const {
    return { reinterpret_cast<const std::byte*>(m_Raw.get()) + offset, size };
}

std::vector<GenericSection> const& BinaryFile::GetSections() const { return m_GenSections; }
std::vector<GenericSymbol>  const& BinaryFile::GetSymbols()  const { return m_GenSymbols;  }
std::vector<GenericImport>  const& BinaryFile::GetImports()  const { return m_GenImports;  }
std::vector<GenericExport>  const& BinaryFile::GetExports()  const { return m_GenExports;  }

bool BinaryFile::OpenRaw(std::wstring_view path) {
    wil::unique_hfile hFile(::CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ,
                                          nullptr, OPEN_EXISTING, 0, nullptr));
    if (!hFile) return false;

    LARGE_INTEGER sz{};
    ::GetFileSizeEx(hFile.get(), &sz);
    if (sz.QuadPart > 0x100000000LL) return false;
    m_FileSize = sz.LowPart;

    HANDLE hMap = ::CreateFileMapping(hFile.get(), nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) { m_FileSize = 0; return false; }
    m_Raw.reset(static_cast<uint8_t*>(::MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0)));
    ::CloseHandle(hMap);
    if (!m_Raw) { m_FileSize = 0; return false; }

    m_Path = path;
    return true;
}

BinaryFormat DetectBinaryFormat(std::wstring_view path) {
    wil::unique_hfile h(::CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ,
                                      nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h) return BinaryFormat::Unknown;
    uint8_t magic[4]{};
    DWORD nRead = 0;
    ::ReadFile(h.get(), magic, 4, &nRead, nullptr);
    if (nRead >= 2 && magic[0] == 'M' && magic[1] == 'Z')
        return BinaryFormat::PE;
    if (nRead >= 4 && magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
        return BinaryFormat::ELF;
    return BinaryFormat::Unknown;
}
