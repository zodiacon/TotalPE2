#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <PEFile.h>

enum class AnomalySeverity {
	Info,		// unusual, but common and usually harmless
	Warning,	// suspicious, or a sign of a packed or damaged file
	Error,		// the file is malformed
};

struct Anomaly {
	AnomalySeverity Severity{};
	std::wstring Category;
	std::wstring Message;
	int64_t FileOffset{ -1 };	// where to look in the file, -1 if not applicable
};

// Runs a set of heuristics over a PE file and returns what looks unusual, most severe first.
std::vector<Anomaly> FindAnomalies(PEFile const& pe);

PCWSTR AnomalySeverityToString(AnomalySeverity severity);

// Shannon entropy of the bytes, 0 (all the same) to 8 (random).
double ComputeEntropy(const uint8_t* data, size_t size);

// Whether a section's entropy means compressed or encrypted data: 7 for code (real code stays well below), 7.4 for the rest
// (resources often hold compressed images). Less than 512 bytes of data is too little to tell.
bool IsHighEntropySection(double entropy, uint32_t characteristics, size_t size);

// The checksum the Windows loader (CheckSumMappedFile) would compute. The 4 bytes at checksumOffset are ignored.
uint32_t ComputePEChecksum(const uint8_t* data, size_t size, size_t checksumOffset);

// The checksum of the whole file, the value the CheckSum field of the optional header should have (0 if the headers are not in the file)
uint32_t ComputeFileChecksum(PEFile const& pe);

// The key of a Rich header: a checksum of the DOS header and the entries.
uint32_t ComputeRichKey(const uint8_t* data, PERichHeader const& rich);
