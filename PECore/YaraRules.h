#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

//
// YARA rules (libyara): compiled from rule files (.yar, .yara) or loaded compiled (.yarc, as yarac writes them), and matched
// against the bytes of a file. The modules of libyara are there (pe, dotnet, math, hash...), and the external variables that
// community rules use (filename, filepath, extension, filetype, owner) are defined: set from the path of the scanned file.
//
// One set of rules can be scanned with from several threads at once, by at most MaxThreads of them.
//

struct YaraStringMatch {
	std::string Identifier;		// "$a"
	uint64_t Offset{};			// in the data
	uint32_t Length{};
	std::vector<uint8_t> Data;	// the first bytes of the match (at most MaxDataBytes)

	static constexpr size_t MaxDataBytes = 64;
};

struct YaraMatch {
	std::string Rule;
	std::string Namespace;
	std::vector<std::string> Tags;
	std::vector<std::pair<std::string, std::string>> Meta;	// values as text (integers in decimal, booleans "true"/"false")
	std::vector<YaraStringMatch> Strings;	// at most MaxStringMatches
	size_t StringMatchCount{};				// all of them
	bool TooManyMatches{ false };			// a string matched too often: libyara stopped looking for it

	static constexpr size_t MaxStringMatches = 100;
};

struct YaraDiagnostic {
	bool Error{ true };			// else a warning
	std::wstring File;			// empty for rules from text
	int Line{};
	std::string Message;
};

class YaraRules {
public:
	static constexpr unsigned MaxThreads = 32;	// (YR_MAX_THREADS)

	YaraRules();
	~YaraRules();
	YaraRules(YaraRules const&) = delete;
	YaraRules& operator=(YaraRules const&) = delete;
	YaraRules(YaraRules&&) noexcept;
	YaraRules& operator=(YaraRules&&) noexcept;

	// Rules from a file (.yar, .yara: compiled now; .yarc: loaded as they are), or from the rule files of a directory (and of
	// its subdirectories). Rule files are compiled together, each in a namespace of its own name ("default" for one file);
	// compiled rules cannot be combined with others. A rule file with errors is left out, and the others are kept (collections
	// of rules often have a few that need modules or versions of libyara that are not here): Diagnostics() has the errors and
	// the warnings. False if no rules could be loaded.
	bool Load(std::wstring const& path);
	// Rules from text, in a namespace
	bool LoadText(std::string const& text, std::string const& ns = "default");

	bool IsLoaded() const;
	size_t RuleCount() const;
	std::vector<YaraDiagnostic> const& Diagnostics() const { return m_Diagnostics; }
	std::wstring const& Source() const { return m_Source; }

	// The rules that match the data. 'path' is the file the data is of (for the external variables; may be empty).
	// 'timeoutSeconds' 0 means no limit. False (with the reason) on an error of libyara (a timeout, too many threads...).
	bool Scan(std::span<const std::byte> data, std::wstring const& path, std::vector<YaraMatch>& matches, std::string* error = nullptr,
		int timeoutSeconds = 60) const;

	// A message for an error code of libyara
	static std::string ErrorMessage(int code);

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
	std::vector<YaraDiagnostic> m_Diagnostics;
	std::wstring m_Source;
};
