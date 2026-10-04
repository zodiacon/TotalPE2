#include "pch.h"
#include "ApiSetMap.h"
#include <PEFile.h>

namespace {
	uint32_t Read32(std::span<const std::byte> d, size_t pos) {
		uint32_t v = 0;
		for (size_t i = 0; i < 4; i++)
			v |= (uint32_t)std::to_integer<uint8_t>(d[pos + i]) << (8 * i);
		return v;
	}

	// a UTF-16 string at byte offset 'offset' of 'length' bytes, as lowercase ASCII (module names are ASCII)
	bool ReadName(std::span<const std::byte> d, uint64_t offset, uint64_t length, std::string& name) {
		if (length % 2 || offset + length > d.size())
			return false;
		name.clear();
		for (uint64_t i = 0; i < length; i += 2) {
			auto c = (uint16_t)(std::to_integer<uint8_t>(d[offset + i]) | (std::to_integer<uint8_t>(d[offset + i + 1]) << 8));
			name += c < 0x80 ? (char)tolower(c) : '?';
		}
		return true;
	}
}

bool ApiSetMap::IsApiSetName(std::string_view module) {
	auto starts = [&](std::string_view prefix) {
		return module.size() > prefix.size() && _strnicmp(module.data(), prefix.data(), prefix.size()) == 0;
	};
	return starts("api-") || starts("ext-");
}

std::string ApiSetMap::ContractName(std::string_view module) {
	std::string name(module);
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)tolower(c); });
	if (name.ends_with(".dll"))
		name.resize(name.size() - 4);
	// the version is the last number after a dash
	auto dash = name.rfind('-');
	if (dash != std::string::npos && dash + 1 < name.size() &&
		std::all_of(name.begin() + dash + 1, name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
		name.resize(dash);
	return name;
}

bool ApiSetMap::Parse(std::span<const std::byte> schema) {
	m_Hosts.clear();
	// header: Version, Size, Flags, Count, EntryOffset, HashOffset, HashFactor
	if (schema.size() < 28 || Read32(schema, 0) != 6)
		return false;
	uint64_t count = Read32(schema, 12), entryOffset = Read32(schema, 16);
	if (count > 100000 || entryOffset + count * 24 > schema.size())
		return false;

	std::string name, host;
	for (uint64_t i = 0; i < count; i++) {
		// entry: Flags, NameOffset, NameLength, HashedLength, ValueOffset, ValueCount
		auto e = (size_t)(entryOffset + i * 24);
		uint64_t valueOffset = Read32(schema, e + 16), valueCount = Read32(schema, e + 20);
		if (!ReadName(schema, Read32(schema, e + 4), Read32(schema, e + 8), name))
			continue;
		if (valueCount == 0 || valueOffset + valueCount * 20 > schema.size())
			continue;

		// value: Flags, NameOffset, NameLength, ValueOffset, ValueLength. The one without a name is the default host.
		size_t chosen = SIZE_MAX;
		for (uint64_t v = 0; v < valueCount; v++)
			if (Read32(schema, (size_t)(valueOffset + v * 20) + 8) == 0) {
				chosen = (size_t)(valueOffset + v * 20);
				break;
			}
		if (chosen == SIZE_MAX)
			chosen = (size_t)valueOffset;
		if (!ReadName(schema, Read32(schema, chosen + 12), Read32(schema, chosen + 16), host) || host.empty())
			continue;	// no host: the API set does not exist on this system

		m_Hosts[ContractName(name)] = host;
	}
	return !m_Hosts.empty();
}

std::optional<std::string> ApiSetMap::Resolve(std::string_view module) const {
	if (!IsApiSetName(module))
		return std::nullopt;
	auto it = m_Hosts.find(ContractName(module));
	if (it == m_Hosts.end())
		return std::nullopt;
	return it->second;
}

ApiSetMap const& ApiSetMap::System() {
	static const ApiSetMap map = [] {
		ApiSetMap m;
		WCHAR dir[MAX_PATH];
		if (::GetSystemDirectoryW(dir, MAX_PATH)) {
			PEFile pe;
			if (pe.Open(std::wstring(dir) + L"\\apisetschema.dll")) {
				for (auto& s : *pe.GetSecHeaders())
					if (s.SectionName == ".apiset" && (uint64_t)s.SecHdr.PointerToRawData + s.SecHdr.SizeOfRawData <= pe.GetFileSize()) {
						m.Parse(pe.GetSpan(s.SecHdr.PointerToRawData, s.SecHdr.SizeOfRawData));
						break;
					}
			}
		}
		return m;
	}();
	return map;
}

std::wstring DescribeApiSet(std::string_view module) {
	if (!ApiSetMap::IsApiSetName(module))
		return {};
	auto host = ApiSetMap::System().Resolve(module);
	return host ? std::wstring(host->begin(), host->end()) : std::wstring(L"(not present on this system)");
}
