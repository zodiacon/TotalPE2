#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <wil\resource.h>

// A static library: an "ar" archive of COFF object files, or of the short import records of an import library.

enum class ArchiveMemberKind {
	Object,			// a COFF object file (regular or "bigobj")
	ImportRecord,	// a short import record of an import library
	LinkerMember,	// the symbol index that the linker reads (the first and the second ones)
	LongNames,		// the names of the members that are longer than 15 characters
	Other,			// anything else: a resource file, LTO bitcode...
};

struct ArchiveMember {
	std::string Name;
	uint64_t HeaderOffset{};
	uint64_t DataOffset{};
	uint64_t Size{};
	uint32_t Date{};			// seconds since 1970
	uint32_t Mode{};			// the permissions (octal in the header)
	ArchiveMemberKind Kind{ ArchiveMemberKind::Other };
	uint16_t Machine{};			// objects and import records
	uint32_t Sections{};		// objects
	uint32_t Symbols{};			// objects
	bool BigObj{ false };
};

struct ArchiveSymbol {
	std::string Name;
	int Member{ -1 };			// index in LibArchive::Members(); -1 if the index of the symbols is wrong
};

enum class ImportType { Code, Data, Const, Unknown };
enum class ImportNameType { Ordinal, Name, NameNoPrefix, Undecorate, Unknown };

struct ArchiveImport {
	int Member{ -1 };
	uint16_t Machine{};
	uint32_t TimeDateStamp{};
	uint16_t OrdinalOrHint{};
	ImportType Type{ ImportType::Unknown };
	ImportNameType NameType{ ImportNameType::Unknown };
	std::string Symbol;			// as it is in the library (the decorated name)
	std::string Dll;
};

class LibArchive {
public:
	// A file that starts with the signature of an archive
	static bool IsArchiveFile(std::wstring_view path);
	static bool IsArchive(std::span<const std::byte> data);

	bool Open(std::wstring_view path);
	// The archive is copied
	bool Open(std::span<const std::byte> data);
	void Close();
	explicit operator bool() const { return !m_Data.empty(); }

	std::wstring const& Path() const { return m_Path; }
	std::span<const std::byte> Data() const { return m_Data; }
	std::vector<ArchiveMember> const& Members() const { return m_Members; }
	std::vector<ArchiveSymbol> const& Symbols() const { return m_Symbols; }
	std::vector<ArchiveImport> const& Imports() const { return m_Imports; }
	// the contents of a member (without its header)
	std::span<const std::byte> MemberData(size_t index) const;

	static std::wstring_view MachineName(uint16_t machine);
	static std::wstring_view KindName(ArchiveMemberKind kind);

private:
	bool Parse();
	void ReadSymbolIndex(size_t first, int second);

	wil::unique_mapview_ptr<uint8_t> m_Map;
	std::vector<std::byte> m_Copy;
	std::span<const std::byte> m_Data;
	std::wstring m_Path;
	std::vector<ArchiveMember> m_Members;
	std::vector<ArchiveSymbol> m_Symbols;
	std::vector<ArchiveImport> m_Imports;
};
