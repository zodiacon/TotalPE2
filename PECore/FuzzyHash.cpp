#include "pch.h"
#include "FuzzyHash.h"
#include <charconv>

//
// ssdeep (context-triggered piecewise hashing, after spamsum by Andrew Tridgell and ssdeep by Jesse Kornblum)
//

namespace {
	constexpr uint32_t RollingWindow = 7;
	constexpr uint32_t MinBlockSize = 3;
	constexpr uint8_t HashInit = 0x27;
	constexpr size_t SpamSumLength = 64;
	constexpr char Base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	// The FNV hash of ssdeep 2.13 and later, of which only the low 6 bits are kept
	inline uint8_t SumHash(uint8_t c, uint8_t h) {
		return (uint8_t)(((h * 0x01000193u) ^ c) & 0x3F);
	}

	// The rolling hash of the last 7 bytes, which picks the points where the pieces of the data end
	struct RollingHash {
		uint8_t Window[RollingWindow]{};
		uint32_t H1{}, H2{}, H3{}, N{};

		void Add(uint8_t c) {
			H2 = H2 - H1 + RollingWindow * c;
			H1 = H1 + c - Window[N];
			Window[N] = c;
			N = (N + 1) % RollingWindow;
			H3 = (H3 << 5) ^ c;
		}
		uint32_t Sum() const {
			return H1 + H2 + H3;
		}
	};

	struct SsdeepPass {
		std::string First, Second;	// the digests for the block size and twice the block size
		size_t Pieces{};			// the characters of the first before the last one (which covers the rest)
	};

	// The digests of the data for one block size. A piece ends where the rolling hash is -1 modulo the block size; the hash
	// of each piece is a character. The first digest has at most 64 characters, the second (twice the block size) 32: the
	// last character covers all the rest.
	SsdeepPass SsdeepDigests(std::span<const std::byte> data, uint32_t blockSize) {
		SsdeepPass pass;
		RollingHash roll;
		uint8_t h1 = HashInit, h2 = HashInit;
		char last1 = 0, last2 = 0;
		for (auto b : data) {
			auto c = (uint8_t)b;
			h1 = SumHash(c, h1);
			h2 = SumHash(c, h2);
			roll.Add(c);
			auto sum = roll.Sum();
			if (sum % blockSize != blockSize - 1)
				continue;
			last1 = Base64[h1];
			if (pass.First.size() < SpamSumLength - 1) {
				pass.First += last1;
				h1 = HashInit;
				last1 = 0;
			}
			if (sum % (blockSize * 2) == blockSize * 2 - 1) {
				last2 = Base64[h2];
				if (pass.Second.size() < SpamSumLength / 2 - 1) {
					pass.Second += last2;
					h2 = HashInit;
					last2 = 0;
				}
			}
		}
		pass.Pieces = pass.First.size();
		if (roll.Sum() != 0) {
			pass.First += Base64[h1];
			pass.Second += Base64[h2];
		}
		else {
			if (last1)
				pass.First += last1;
			if (last2)
				pass.Second += last2;
		}
		return pass;
	}

	// A part of a digest with the runs of more than 3 of a character cut to 3 (they say little, and would make unrelated
	// digests look alike); false if it is longer than a digest can be
	bool EliminateSequences(std::string_view part, std::string& result) {
		result.clear();
		for (size_t i = 0; i < part.size(); i++) {
			if (i >= 3 && part[i] == part[i - 1] && part[i] == part[i - 2] && part[i] == part[i - 3])
				continue;
			result += part[i];
		}
		return result.size() <= SpamSumLength;
	}

	struct SsdeepDigest {
		uint64_t BlockSize{};
		std::string First, Second;
	};

	bool ParseSsdeep(std::string_view text, SsdeepDigest& digest) {
		auto colon = text.find(':');
		if (colon == std::string_view::npos || colon == 0)
			return false;
		auto [end, ec] = std::from_chars(text.data(), text.data() + colon, digest.BlockSize);
		if (ec != std::errc() || end != text.data() + colon || digest.BlockSize == 0)
			return false;
		auto rest = text.substr(colon + 1);
		auto colon2 = rest.find(':');
		if (colon2 == std::string_view::npos)
			return false;
		auto second = rest.substr(colon2 + 1);
		// ssdeep's lists add ,"file name" after a digest
		second = second.substr(0, second.find(','));
		return EliminateSequences(rest.substr(0, colon2), digest.First) && EliminateSequences(second, digest.Second);
	}

	// Whether the two strings have a substring of the length of the rolling window in common: if not, no piece of the
	// data is shared
	bool HaveCommonSubstring(std::string const& a, std::string const& b) {
		if (a.size() < RollingWindow || b.size() < RollingWindow)
			return false;
		for (size_t i = 0; i + RollingWindow <= a.size(); i++)
			if (b.find(std::string_view(a).substr(i, RollingWindow)) != std::string::npos)
				return true;
		return false;
	}

	// The edit distance of ssdeep: inserting or removing a character costs 1, replacing one 2
	uint32_t EditDistance(std::string const& a, std::string const& b) {
		std::vector<uint32_t> previous(b.size() + 1), current(b.size() + 1);
		for (size_t j = 0; j <= b.size(); j++)
			previous[j] = (uint32_t)j;
		for (size_t i = 0; i < a.size(); i++) {
			current[0] = (uint32_t)i + 1;
			for (size_t j = 0; j < b.size(); j++)
				current[j + 1] = std::min({ previous[j + 1] + 1, current[j] + 1, previous[j] + (a[i] == b[j] ? 0 : 2) });
			std::swap(previous, current);
		}
		return previous[b.size()];
	}

	uint32_t ScoreStrings(std::string const& a, std::string const& b, uint64_t blockSize) {
		if (!HaveCommonSubstring(a, b))
			return 0;
		// the edit distance as a part of the lengths, on a scale of 0 (nothing alike) to 100
		uint32_t score = EditDistance(a, b);
		score = score * (uint32_t)SpamSumLength / (uint32_t)(a.size() + b.size());
		score = 100 * score / (uint32_t)SpamSumLength;
		score = 100 - score;
		// small block sizes would make short matches look better than they are
		if (blockSize >= (99 + RollingWindow) / RollingWindow * MinBlockSize)
			return score;
		auto cap = (uint32_t)(blockSize / MinBlockSize * std::min(a.size(), b.size()));
		return std::min(score, cap);
	}
}

std::string FuzzyHash::Ssdeep(std::span<const std::byte> data) {
	// the smallest block size that makes a digest of at most 64 characters; then smaller ones while the digest is too
	// short to say much (less than half its length)
	uint64_t blockSize = MinBlockSize;
	while (blockSize * SpamSumLength < data.size())
		blockSize *= 2;
	for (;;) {
		auto pass = SsdeepDigests(data, (uint32_t)blockSize);
		if (blockSize > MinBlockSize && pass.Pieces < SpamSumLength / 2) {
			blockSize /= 2;
			continue;
		}
		return std::format("{}:{}:{}", blockSize, pass.First, pass.Second);
	}
}

int FuzzyHash::CompareSsdeep(std::string_view a, std::string_view b) {
	SsdeepDigest x, y;
	if (!ParseSsdeep(a, x) || !ParseSsdeep(b, y))
		return -1;
	if (x.BlockSize != y.BlockSize && x.BlockSize * 2 != y.BlockSize && y.BlockSize * 2 != x.BlockSize)
		return 0;
	if (x.BlockSize == y.BlockSize && x.First == y.First && x.Second == y.Second)
		return 100;
	if (x.BlockSize == y.BlockSize)
		return (int)std::max(ScoreStrings(x.First, y.First, x.BlockSize), ScoreStrings(x.Second, y.Second, x.BlockSize * 2));
	if (x.BlockSize * 2 == y.BlockSize)
		return (int)ScoreStrings(y.First, x.Second, y.BlockSize);
	return (int)ScoreStrings(x.First, y.Second, x.BlockSize);
}

//
// TLSH (the Trend Micro locality-sensitive hash). The tables are those of the TLSH sources (Copyright 2013 Trend Micro
// Incorporated, used under the BSD license).
//

namespace {
	constexpr size_t TlshMinLength = 50;
	constexpr int Buckets = 128;			// the buckets that make the digest (of the 256 that are counted)
	constexpr int CodeSize = Buckets / 4;	// 2 bits a bucket
	constexpr size_t TlshHexLength = 2 * (3 + CodeSize);	// checksum, length, quartile ratios, the code

	// Pearson's table, which maps triplets of bytes to buckets
	constexpr uint8_t VTable[256] = {
		1, 87, 49, 12, 176, 178, 102, 166, 121, 193, 6, 84, 249, 230, 44, 163,
		14, 197, 213, 181, 161, 85, 218, 80, 64, 239, 24, 226, 236, 142, 38, 200,
		110, 177, 104, 103, 141, 253, 255, 50, 77, 101, 81, 18, 45, 96, 31, 222,
		25, 107, 190, 70, 86, 237, 240, 34, 72, 242, 20, 214, 244, 227, 149, 235,
		97, 234, 57, 22, 60, 250, 82, 175, 208, 5, 127, 199, 111, 62, 135, 248,
		174, 169, 211, 58, 66, 154, 106, 195, 245, 171, 17, 187, 182, 179, 0, 243,
		132, 56, 148, 75, 128, 133, 158, 100, 130, 126, 91, 13, 153, 246, 216, 219,
		119, 68, 223, 78, 83, 88, 201, 99, 122, 11, 92, 32, 136, 114, 52, 10,
		138, 30, 48, 183, 156, 35, 61, 26, 143, 74, 251, 94, 129, 162, 63, 152,
		170, 7, 115, 167, 241, 206, 3, 150, 55, 59, 151, 220, 90, 53, 23, 131,
		125, 173, 15, 238, 79, 95, 89, 16, 105, 137, 225, 224, 217, 160, 37, 123,
		118, 73, 2, 157, 46, 116, 9, 145, 134, 228, 207, 212, 202, 215, 69, 229,
		27, 188, 67, 124, 168, 252, 42, 4, 29, 108, 21, 247, 19, 205, 39, 203,
		233, 40, 186, 147, 198, 192, 155, 33, 164, 191, 98, 204, 165, 180, 117, 76,
		140, 36, 210, 172, 41, 54, 159, 8, 185, 232, 113, 196, 231, 47, 146, 120,
		51, 65, 28, 144, 254, 221, 93, 189, 194, 139, 112, 43, 71, 109, 184, 209,
	};

	// The upper bounds of the lengths that each value of the length byte stands for (a logarithmic scale)
	constexpr uint32_t LengthBounds[170] = {
		1, 2, 3, 5, 7, 11, 17, 25, 38, 57, 86, 129, 194, 291, 437, 656, 854, 1110, 1443, 1876, 2439, 3171, 3475, 3823,
		4205, 4626, 5088, 5597, 6157, 6772, 7450, 8195, 9014, 9916, 10907, 11998, 13198, 14518, 15970, 17567, 19323, 21256,
		23382, 25720, 28292, 31121, 34233, 37656, 41422, 45564, 50121, 55133, 60646, 66711, 73382, 80721, 88793, 97672,
		107439, 118183, 130002, 143002, 157302, 173032, 190335, 209369, 230306, 253337, 278670, 306538, 337191, 370911,
		408002, 448802, 493682, 543050, 597356, 657091, 722800, 795081, 874589, 962048, 1058252, 1164078, 1280486, 1408534,
		1549388, 1704327, 1874759, 2062236, 2268459, 2495305, 2744836, 3019320, 3321252, 3653374, 4018711, 4420582, 4862641,
		5348905, 5883796, 6472176, 7119394, 7831333, 8614467, 9475909, 10423501, 11465851, 12612437, 13873681, 15261050,
		16787154, 18465870, 20312458, 22343706, 24578077, 27035886, 29739474, 32713425, 35984770, 39583245, 43541573,
		47895730, 52685306, 57953837, 63749221, 70124148, 77136564, 84850228, 93335252, 102668779, 112935659, 124229227,
		136652151, 150317384, 165349128, 181884040, 200072456, 220079703, 242087671, 266296456, 292926096, 322218735,
		354440623, 389884688, 428873168, 471760495, 518936559, 570830240, 627913311, 690704607, 759775136, 835752671,
		919327967, 1011260767, 1112386880, 1223623232, 1345985727, 1480584256, 1628642751, 1791507135, 1970657856,
		2167723648, 2384496256, 2622945920, 2885240448, 3173764736, 3491141248, 3840255616, 4224281216,
	};

	// Pearson's hash of a salt and three bytes (the salt already through the table)
	inline uint8_t Mapping(uint8_t salt, uint8_t i, uint8_t j, uint8_t k) {
		return VTable[VTable[VTable[salt ^ i] ^ j] ^ k];
	}

	uint8_t LengthCode(size_t length) {
		auto it = std::lower_bound(std::begin(LengthBounds), std::end(LengthBounds), (uint32_t)std::min<size_t>(length, UINT32_MAX));
		return (uint8_t)std::min<ptrdiff_t>(it - std::begin(LengthBounds), std::size(LengthBounds) - 1);
	}

	inline uint8_t SwapNibbles(uint8_t b) {
		return (uint8_t)((b << 4) | (b >> 4));
	}

	struct TlshDigest {
		uint8_t Checksum{}, Length{}, Q1Ratio{}, Q2Ratio{};
		uint8_t Code[CodeSize]{};
	};

	int HexDigit(char c) {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	}

	bool ParseTlsh(std::string_view text, TlshDigest& digest) {
		if (text.starts_with("T1"))
			text.remove_prefix(2);
		if (text.size() != TlshHexLength)
			return false;
		uint8_t bytes[TlshHexLength / 2];
		for (size_t i = 0; i < std::size(bytes); i++) {
			int high = HexDigit(text[2 * i]), low = HexDigit(text[2 * i + 1]);
			if (high < 0 || low < 0)
				return false;
			bytes[i] = (uint8_t)(high << 4 | low);
		}
		digest.Checksum = SwapNibbles(bytes[0]);
		digest.Length = SwapNibbles(bytes[1]);
		digest.Q1Ratio = bytes[2] >> 4;
		digest.Q2Ratio = bytes[2] & 0xF;
		for (int i = 0; i < CodeSize; i++)
			digest.Code[i] = bytes[3 + CodeSize - 1 - i];
		return true;
	}

	// The distance between two values on a circle of 'range' (the length and the ratios wrap)
	int ModDiff(int x, int y, int range) {
		int d = std::abs(x - y);
		return std::min(d, range - d);
	}
}

std::string FuzzyHash::Tlsh(std::span<const std::byte> data) {
	if (data.size() < TlshMinLength)
		return {};
	auto d = (const uint8_t*)data.data();
	// each byte with the 4 before it: 6 of the triplets that include the byte go to buckets, and a pair to the checksum
	uint32_t buckets[256]{};
	uint8_t checksum = 0;
	for (size_t i = 4; i < data.size(); i++) {
		uint8_t a0 = d[i], a1 = d[i - 1], a2 = d[i - 2], a3 = d[i - 3], a4 = d[i - 4];
		checksum = Mapping(1, a0, a1, checksum);
		buckets[Mapping(49, a0, a1, a2)]++;
		buckets[Mapping(12, a0, a1, a3)]++;
		buckets[Mapping(178, a0, a2, a3)]++;
		buckets[Mapping(166, a0, a2, a4)]++;
		buckets[Mapping(84, a0, a1, a4)]++;
		buckets[Mapping(230, a0, a3, a4)]++;
	}

	// each bucket becomes the quartile its count falls in
	uint32_t sorted[Buckets];
	std::copy_n(buckets, Buckets, sorted);
	auto quartile = [&](int n) {
		std::nth_element(sorted, sorted + n, sorted + Buckets);
		return sorted[n];
	};
	auto q1 = quartile(Buckets / 4 - 1), q2 = quartile(Buckets / 2 - 1), q3 = quartile(Buckets - Buckets / 4 - 1);
	if (q3 == 0)
		return {};
	// too uniform: no more than half the buckets are used
	if (std::count_if(buckets, buckets + Buckets, [](uint32_t n) { return n > 0; }) <= Buckets / 2)
		return {};

	uint8_t code[CodeSize];
	for (int i = 0; i < CodeSize; i++) {
		uint8_t h = 0;
		for (int j = 0; j < 4; j++) {
			auto k = buckets[4 * i + j];
			h |= (k > q3 ? 3 : k > q2 ? 2 : k > q1 ? 1 : 0) << (j * 2);
		}
		code[i] = h;
	}
	auto q1Ratio = (uint8_t)((uint64_t)q1 * 100 / q3 % 16), q2Ratio = (uint8_t)((uint64_t)q2 * 100 / q3 % 16);

	auto digest = std::format("T1{:02X}{:02X}{:X}{:X}", SwapNibbles(checksum), SwapNibbles(LengthCode(data.size())), q1Ratio, q2Ratio);
	for (int i = CodeSize - 1; i >= 0; i--)
		digest += std::format("{:02X}", code[i]);
	return digest;
}

int FuzzyHash::TlshDistance(std::string_view a, std::string_view b, bool includeLength) {
	TlshDigest x, y;
	if (!ParseTlsh(a, x) || !ParseTlsh(b, y))
		return -1;
	constexpr int LengthMult = 12, RatioMult = 12;
	int diff = 0;
	if (includeLength) {
		int d = ModDiff(x.Length, y.Length, 256);
		diff += d <= 1 ? d : d * LengthMult;
	}
	for (auto [p, q] : { std::pair{ x.Q1Ratio, y.Q1Ratio }, std::pair{ x.Q2Ratio, y.Q2Ratio } }) {
		int d = ModDiff(p, q, 16);
		diff += d <= 1 ? d : (d - 1) * RatioMult;
	}
	if (x.Checksum != y.Checksum)
		diff++;
	// the buckets: a quartile apart counts 1, two 2, and the farthest (3) counts 6
	for (int i = 0; i < CodeSize; i++)
		for (int j = 0; j < 4; j++) {
			int d = std::abs(((x.Code[i] >> (2 * j)) & 3) - ((y.Code[i] >> (2 * j)) & 3));
			diff += d == 3 ? 6 : d;
		}
	return diff;
}
