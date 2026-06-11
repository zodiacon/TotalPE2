#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <memory>
#include <cstdint>

enum class BinaryFormat { Unknown, PE, ELF };

BinaryFormat DetectBinaryFormat(std::wstring_view path);

struct GenericSection {
    std::wstring Name;
    uint64_t     VirtualAddress{};
    uint64_t     FileOffset{};
    uint64_t     Size{};
    uint32_t     Flags{};
};

struct GenericSymbol {
    std::wstring Name;
    uint64_t     Value{};
    uint64_t     Size{};
};

struct GenericImport {
    std::wstring              Library;
    std::vector<std::wstring> Functions;
};

struct GenericExport {
    std::wstring Name;
    uint64_t     Address{};
};

class BinaryFile {
public:
    BinaryFile();
    virtual ~BinaryFile();
    BinaryFile(const BinaryFile&)            = delete;
    BinaryFile& operator=(const BinaryFile&) = delete;
    BinaryFile(BinaryFile&&)                 noexcept;
    BinaryFile& operator=(BinaryFile&&)      noexcept;

    virtual bool Open(std::wstring_view path) = 0;
    virtual void Close();

    explicit operator bool() const;

    BinaryFormat        GetFormat()     const noexcept;
    std::wstring const& GetPath()       const;
    uint32_t            GetFileSize()   const;
    uint64_t            GetEntryPoint() const noexcept;
    uint64_t            GetImageBase()  const noexcept;
    bool                Is32Bit()       const noexcept;

    bool Read(uint32_t offset, uint32_t size, void* buffer) const;
    template<typename T>
    T Read(uint32_t offset) const {
        T v{};
        Read(offset, sizeof(T), &v);
        return v;
    }

    const BYTE*                GetData()                                const;
    std::span<const std::byte> GetSpan(uint32_t offset, uint32_t size)  const;

    std::vector<GenericSection> const& GetSections() const;
    std::vector<GenericSymbol>  const& GetSymbols()  const;
    std::vector<GenericImport>  const& GetImports()  const;
    std::vector<GenericExport>  const& GetExports()  const;

protected:
    bool OpenRaw(std::wstring_view path);

    BinaryFormat                     m_Format{ BinaryFormat::Unknown };
    std::wstring                     m_Path;
    uint32_t                         m_FileSize{};
    wil::unique_mapview_ptr<uint8_t> m_Raw;

    uint64_t m_EntryPoint{};
    uint64_t m_ImageBase{};
    bool     m_Is32Bit{};

    std::vector<GenericSection> m_GenSections;
    std::vector<GenericSymbol>  m_GenSymbols;
    std::vector<GenericImport>  m_GenImports;
    std::vector<GenericExport>  m_GenExports;
};
