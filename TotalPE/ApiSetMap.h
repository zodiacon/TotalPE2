#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

// The API set schema: the table that maps virtual module names such as "api-ms-win-core-heap-l1-1-0.dll" to the
// DLLs that implement them. Windows 10 and later use version 6 of the format.
class ApiSetMap {
public:
	// Parses the schema (the contents of the ".apiset" section of apisetschema.dll). False if the format is not understood.
	bool Parse(std::span<const std::byte> schema);

	bool Loaded() const { return !m_Hosts.empty(); }
	size_t Count() const { return m_Hosts.size(); }

	// "api-ms-win-core-heap-l1-1-0.dll" -> "kernelbase.dll". Empty if the name is not an API set or it has no host on this system.
	std::optional<std::string> Resolve(std::string_view module) const;

	static bool IsApiSetName(std::string_view module);

	// The part of a name that identifies the contract: lowercase, without ".dll" and without the trailing version number
	// ("api-ms-win-core-heap-l1-1-0.dll" -> "api-ms-win-core-heap-l1-1").
	static std::string ContractName(std::string_view module);

	// The schema of the running system (from apisetschema.dll); empty if it cannot be read.
	static ApiSetMap const& System();

private:
	std::unordered_map<std::string, std::string> m_Hosts;	// contract name -> host DLL
};

// For the views: the DLL that implements an API set ("kernelbase.dll"), "(not present on this system)" for an API set
// that has no host here, and an empty string for any other module.
std::wstring DescribeApiSet(std::string_view module);
