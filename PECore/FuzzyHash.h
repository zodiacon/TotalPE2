#pragma once

#include <span>
#include <string>
#include <string_view>

//
// Fuzzy hashes: hashes that are close for files that are alike, so that variants of a program (another build, a patched
// copy, a repacked sample) can be found. The two that tools share:
//
//   ssdeep  context-triggered piecewise hashes ("96:jZ4o...:jZ4o..."); compared as a score from 0 (nothing alike) to 100.
//           The same digests and scores as ssdeep 2.14.
//   TLSH    a locality-sensitive hash of 72 characters ("T1..."); compared as a distance from 0 (alike) up; under 30 or
//           so is very close, around 100 and more is unrelated. The same digests and distances as TLSH 4 (128 buckets,
//           1-byte checksum, the "T1" form used by VirusTotal and MalwareBazaar).
//

namespace FuzzyHash {
	// The ssdeep digest of the data ("3::" for no data)
	std::string Ssdeep(std::span<const std::byte> data);
	// How alike two ssdeep digests are: 0 to 100; -1 if a digest is not valid. Digests of block sizes that are not the
	// same or twice apart cannot be compared, and score 0.
	int CompareSsdeep(std::string_view a, std::string_view b);

	// The TLSH digest of the data, with the "T1" prefix; empty if the data is too short (less than 50 bytes) or too
	// uniform to hash
	std::string Tlsh(std::span<const std::byte> data);
	// The distance between two TLSH digests (with or without "T1"); -1 if a digest is not valid. Without
	// 'includeLength', files of very different sizes are not set apart by their size.
	int TlshDistance(std::string_view a, std::string_view b, bool includeLength = true);
}
