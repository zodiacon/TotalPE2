#pragma once

#include <PEFile.h>
#include <span>
#include <string>
#include <vector>
#include <cstdint>

struct ClrStream {
	std::string Name;
	uint32_t Offset;	// relative to metadata root
	uint32_t Size;
};

struct ClrTable {
	int Id;
	uint32_t Rows;
};

struct ClrAssemblyInfo {
	std::wstring Name;
	std::wstring Culture;
	uint16_t Major{}, Minor{}, Build{}, Revision{};
	uint32_t Flags{};
};

// Parses the CLR (COM descriptor) header and the essentials of the metadata root:
// version, streams, table row counts, module name, assembly identity and assembly references.
class ClrMetadata {
public:
	bool Parse(PEFile const& pe);

	bool HasHeader() const { return m_HasHeader; }
	bool HasMetadata() const { return m_HasMetadata; }
	IMAGE_COR20_HEADER const& Header() const { return m_Header; }
	std::wstring const& Version() const { return m_Version; }
	std::vector<ClrStream> const& Streams() const { return m_Streams; }
	std::vector<ClrTable> const& Tables() const { return m_Tables; }
	bool HasAssembly() const { return m_HasAssembly; }
	ClrAssemblyInfo const& Assembly() const { return m_Assembly; }
	std::wstring const& ModuleName() const { return m_ModuleName; }
	std::vector<ClrAssemblyInfo> const& References() const { return m_Refs; }

	static PCWSTR TableName(int id);

private:
	bool ParseTables(std::span<const std::byte> md, ClrStream const& tables, ClrStream const* strings);

	IMAGE_COR20_HEADER m_Header{};
	bool m_HasHeader{ false }, m_HasMetadata{ false }, m_HasAssembly{ false };
	std::wstring m_Version, m_ModuleName;
	std::vector<ClrStream> m_Streams;
	std::vector<ClrTable> m_Tables;
	ClrAssemblyInfo m_Assembly;
	std::vector<ClrAssemblyInfo> m_Refs;
};
