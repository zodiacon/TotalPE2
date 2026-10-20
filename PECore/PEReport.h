#pragma once

#include <cstdint>
#include <string>
#include <Windows.h>
#include "Json.h"

class PEFile;

//
// A report of a PE file as JSON: what the analyzers of PECore find (headers, sections, imports, the signature, the security
// features, anomalies...), for tools that read it rather than people. The layout is versioned by "schema" ("totalpe.report/1").
//
// Addresses, RVAs and file offsets are strings in hex ("0x1A2B0"): JSON numbers are doubles, which do not hold every 64-bit
// value. Sizes and counts are numbers. Names (machine, subsystem, flags) are stable tokens, not the text the UI shows.
// headers.timestamp is null for a reproducible build (/Brepro), whose timestamp is a hash (headers.timestampRaw has it).
// A part that fails to be read leaves its key out and adds a message to "errors"; the rest of the report is still made.
//

enum class ReportPart : uint32_t {
	None = 0,
	File = 1,			// size and hashes (MD5, SHA-1, SHA-256, imphash)
	Headers = 2,
	Sections = 4,
	Imports = 8,		// and the delay imports
	Exports = 0x10,
	Debug = 0x20,		// the debug directory, with the PDB of a CodeView entry
	Rich = 0x40,
	Tls = 0x80,
	Version = 0x100,
	Signature = 0x200,
	Security = 0x400,
	Overlay = 0x800,
	Anomalies = 0x1000,
	Rtti = 0x2000,		// expensive: scans the data sections

	Default = 0x1FFF,
	All = 0x3FFF,
};
DEFINE_ENUM_FLAG_OPERATORS(ReportPart);

struct ReportOptions {
	ReportPart Parts{ ReportPart::Default };
	bool VerifyTrust{ false };		// ask WinVerifyTrust about the signature (of the file, or of its catalog)
	bool CheckCatalogs{ true };		// look for the catalog of the system that signs a file without a signature of its own
	bool ResolveOrdinals{ true };	// name the imports by ordinal from the DLLs of this system
	size_t MaxExports{ SIZE_MAX };	// list at most this many exports ("truncated" says if some were left out)
};

inline constexpr char ReportSchema[] = "totalpe.report/1";

JsonValue BuildReport(PEFile const& pe, ReportOptions const& options = {});

// The report of a file that could not be opened as a PE file: its path, and why
JsonValue BuildErrorReport(std::wstring const& path, std::string const& error);

// A flat row of a report for a table (a line of CSV): path, sha256, machine, subsystem, isDll, isDriver, timestamp,
// company, fileVersion, signed, signatureKind, signer, trusted, aslr, dep, cfg, cet, maxEntropy, overlaySize,
// anomalyErrors, anomalyWarnings, imphash, pdb, error. Values that the report does not have are null.
JsonValue SummarizeReport(JsonValue const& report);

// The names of the columns of SummarizeReport, in order
std::vector<std::string> const& ReportSummaryColumns();

// Parses a comma-separated list of part names ("headers,imports,rtti"; "all", "default"). False on an unknown name.
bool ParseReportParts(std::string_view text, ReportPart& parts, std::string* unknown = nullptr);

//
// Differences
//

inline constexpr char DiffSchema[] = "totalpe.diff/1";

// The differences between two files (ComparePEFiles) as JSON: "left" and "right" (path, size, sha256), "status" ("identical"
// or "changed"), the number of changed, added and removed items ("counts"), and the items that differ ("items": category,
// name, status, left, right, details). Added items are only in the right (newer) file. With 'includeSame', the items that
// are the same too.
JsonValue BuildDiffReport(PEFile const& left, PEFile const& right, bool includeSame = false);

// A file that is only on one side of a comparison of directories: "status" is "added" (only in the right) or "removed"
JsonValue BuildOneSidedDiffReport(std::wstring const& path, bool added);

//
// Requirements
//

// The security features (from the "security" part of a report) that can be required, by token ("aslr", "cfg"...), and
// "signed" (in the file or in a catalog) and "trusted" (the report must have been made with VerifyTrust)
std::vector<std::pair<std::string, std::string>> const& RequirementNames();	// token, the name of the feature (or a description)
inline constexpr char DefaultRequirements[] = "aslr,dep,cfg,gs,no-wx";

// Parses a comma-separated list of requirement tokens ("default" is DefaultRequirements). False on an unknown token.
bool ParseRequirements(std::string_view text, std::vector<std::string>& requirements, std::string* unknown = nullptr);

// Why a report fails the requirements (one message each), and the anomalies at or above 'failOn' ("info", "warning", "error";
// empty: anomalies do not fail). Empty if the file passes. A feature that does not apply to the file (SafeSEH in 64-bit code)
// passes; a partial one (ASLR without relocations) fails.
std::vector<std::string> CheckReport(JsonValue const& report, std::vector<std::string> const& requirements, std::string_view failOn = {});
