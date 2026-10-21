#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class PEFile;
class JsonValue;

//
// Capabilities: what a program can do, told from what it imports, the strings it has and the shape of the file, with rules in
// the manner of capa (Mandiant). A rule names a capability ("create a process"), puts it in a namespace
// ("host-interaction/process"), may name ATT&CK techniques, and has a condition on the features of the file. The scope is the
// whole file: a rule matches if the file has the features, wherever they are.
//
// Rules are JSON: an array of rules, or an object with a "rules" array. A rule:
//
//   { "name": "create a process", "namespace": "host-interaction/process", "description": "...", "attack": ["T1106"],
//     "condition": <condition> }
//
// A condition is an object with one key:
//   "api": "CreateProcess" or [ ... ]	an imported function (by name, case-insensitive; "CreateProcess" matches CreateProcessA,
//										CreateProcessW, CreateProcessExA... too; "kernel32.CreateFileW" names the module: the
//										one imported from, or for an API set the DLL that implements it on this system); a
//										list matches if any does
//   "string": "text" or [ ... ]		a string of the file (ASCII or UTF-16) that contains the text (case-insensitive)
//   "regex": "pattern"					a string of the file that matches (ECMAScript, case-insensitive)
//   "section": ".upx0" or [ ... ]		a section of that name (case-insensitive)
//   "import-module": "ws2_32.dll"		a module the file imports from
//   "export": "ServiceMain"			a function the file exports
//   "characteristic": "..."			one of: tls-callbacks, overlay, high-entropy-section, writable-code, dotnet, driver,
//										dll, few-imports (fewer than 10 imported functions), no-imports, signed
//   "all": [ ... ]						all of them
//   "any": [ ... ]						at least one of them
//   "n-of": { "n": 2, "of": [ ... ] }	at least n of them
//   "not": <condition>					it does not match
//

struct CapabilityMatch {
	std::string Name;
	std::string Namespace;
	std::string Description;
	std::vector<std::string> Attack;		// ATT&CK technique IDs ("T1543.003")
	std::vector<std::string> Evidence;		// what matched: "api: kernel32.dll!CreateProcessW", "string: \"...\"" (at most 10)
};

class CapabilityRules {
public:
	CapabilityRules();
	~CapabilityRules();
	CapabilityRules(CapabilityRules&&) noexcept;
	CapabilityRules& operator=(CapabilityRules&&) noexcept;

	// The rules that come with TotalPE
	static CapabilityRules const& BuiltIn();

	// Adds the rules of JSON text; false (with the reasons, one for each bad rule) if some rules are wrong: the good ones are
	// still added. 'source' names the text in the messages.
	bool AddJson(std::string_view json, std::vector<std::string>& errors, std::string const& source = {});
	// Adds the rules of a file, or of the .json files of a directory (and of its subdirectories)
	bool AddPath(std::wstring const& path, std::vector<std::string>& errors);
	// Adds the rules of another set
	void Add(CapabilityRules const& other);

	size_t Size() const;

	// The capabilities of the file, in the order of the rules
	std::vector<CapabilityMatch> Match(PEFile const& pe) const;

	struct Rule;
	struct Impl;

private:
	std::unique_ptr<Impl> m_Impl;
};
