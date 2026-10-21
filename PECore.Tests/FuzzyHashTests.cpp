#include "TestCommon.h"
#include <FuzzyHash.h>
#include <algorithm>
#include <format>
#include <span>

// The expected digests and scores are those of ssdeep 2.14.1 (ssdeep.exe -s -b, -d) and of py-tlsh 4 (tlsh.hash,
// tlsh.diff, tlsh.diffxlen) for the same data.

namespace {
	std::vector<std::byte> Random(uint32_t seed, size_t size) {
		std::vector<std::byte> data(size);
		for (auto& b : data) {
			seed = seed * 1103515245 + 12345;
			b = (std::byte)((seed >> 16) & 0xFF);
		}
		return data;
	}

	// lines of text; each line 'every' says something else
	std::vector<std::byte> Text(int lines, int every = 0) {
		std::string text;
		for (int i = 0; i < lines; i++)
			text += every && i % every == 0
				? std::format("line {}: a slow red cat sleeps under the busy desk\n", i)
				: std::format("line {}: the quick brown fox jumps over the lazy dog\n", i);
		auto bytes = std::as_bytes(std::span(text));
		return { bytes.begin(), bytes.end() };
	}

	// the random data with 2000 bytes in the middle replaced
	std::vector<std::byte> Random2() {
		auto data = Random(1, 200000);
		auto patch = Random(2, 2000);
		std::copy(patch.begin(), patch.end(), data.begin() + 50000);
		return data;
	}
}

TEST_CASE("ssdeep digests are those of ssdeep", "[fuzzy]") {
	using FuzzyHash::Ssdeep;
	CHECK(Ssdeep(Random(1, 200000)) ==
		"3072:x0wTelZF2gviRe7unWljMZZo/tZa8lnt7yDQN+MNG7KElsGxxS1GN+2:Swaz2gv+suvZqFZ57yDO+MJqVp");
	CHECK(Ssdeep(Random2()) ==
		"3072:x0wTelZF2gviRrunWljMZZo/tZa8lnt7yDQN+MNG7KElsGxxS1GN+2:Swaz2gv+ruvZqFZ57yDO+MJqVp");
	CHECK(Ssdeep(Text(3000)) ==
		"3072:z73U5WDQlSvuRuPMdKbIZ2cf+hYratEnnEpGzAVCfcSlQDW5U3q9FyjwZ2X0dKAa:z73U5WDQlSvuRuPMdKbIZ2cf+hYratE6");
	CHECK(Ssdeep(Text(3000, 100)) ==
		"3072:6L3U5WDQlSvgOuPMdKbIZ2cf+hYratEnnEpGzAVCfcSlQDW5U3q9FyjwZ2X0dKA6:6L3U5WDQlSvgOuPMdKbIZ2cf+hYratEa");
	CHECK(Ssdeep(Random(5, 5000)) == "96:e2N4d/m4IAW/02I99rHcXrHDRK6hRhrkq4EGta6HZpJm45OOws:b3Ak02I99rHc7HDRKqhKF7lRGs");
	CHECK(Ssdeep(Random(3, 60)) == "3:SFVKcAZRBULJa5cHGF1:sZAalycHGj");
	CHECK(Ssdeep(Random(3, 49)) == "3:SFVKcAZRBULJan:sZAala");
	CHECK(Ssdeep(std::vector<std::byte>(1000)) == "3::");
	CHECK(Ssdeep({}) == "3::");
	auto abc = std::as_bytes(std::span("abc", 3));
	CHECK(Ssdeep(abc) == "3:uG:uG");
}

TEST_CASE("ssdeep scores are those of ssdeep", "[fuzzy]") {
	using FuzzyHash::CompareSsdeep;
	using FuzzyHash::Ssdeep;
	auto random = Ssdeep(Random(1, 200000)), random2 = Ssdeep(Random2());
	auto text = Ssdeep(Text(3000)), text2 = Ssdeep(Text(3000, 100));
	CHECK(CompareSsdeep(random, random2) == 99);
	CHECK(CompareSsdeep(random2, random) == 99);
	// (a replacement costs 2 in ssdeep: Levenshtein's distance would give 97)
	CHECK(CompareSsdeep(text, text2) == 93);
	CHECK(CompareSsdeep(random, text) == 0);
	CHECK(CompareSsdeep(random, random) == 100);
	// block sizes too far apart to compare
	CHECK(CompareSsdeep(Ssdeep(Random(5, 5000)), random) == 0);
	// the score of small block sizes is capped by the length of the match
	CHECK(CompareSsdeep(Ssdeep(Random(3, 49)), Ssdeep(Random(3, 60))) == 14);
	CHECK(CompareSsdeep("3::", "3::") == 100);
	// ssdeep's lists put the file name after the digest
	CHECK(CompareSsdeep(random + ",\"random\"", random2) == 99);

	CHECK(CompareSsdeep("", random) == -1);
	CHECK(CompareSsdeep("x:abc:def", random) == -1);
	CHECK(CompareSsdeep("3:abc", random) == -1);
	CHECK(CompareSsdeep("3:" + std::string(70, 'A') + std::string(70, 'B') + ":x", random) == 0);	// runs are cut to 3
	std::string tooLong;
	for (int i = 0; i < 70; i++)
		tooLong += "ABCDEFGHIJ"[i % 10];
	CHECK(CompareSsdeep("3:" + tooLong + ":x", random) == -1);
}

TEST_CASE("TLSH digests are those of TLSH", "[fuzzy]") {
	using FuzzyHash::Tlsh;
	CHECK(Tlsh(Random(1, 200000)) == "T1B014239E315F81AE2980DA8BC60811543883DF77F81A15AF6AD05F84B57468E38F7DD3");
	CHECK(Tlsh(Random2()) == "T19914229E316E84AD6A80DA8BC609114438C3CB77F81A15AF39E15F84B97498D38F7DD3");
	CHECK(Tlsh(Text(3000)) == "T1ADF3759A622913F4F5CF289953CEE8F6C7ECD537A2726425B831B0136918521ECFC8E5");
	CHECK(Tlsh(Text(3000, 100)) == "T1CAF3759A622913F4F5CF2899538EE8F6C7EDD537A2726425F831B0036918521ECFC8E5");
	CHECK(Tlsh(Random(5, 5000)) == "T134A17F08B317C999C194FC7A720D17A60592E7CFC49648C4B6AB4D46E70F7EDFA99380");
	CHECK(Tlsh(Random(3, 60)) == "T159A002E763165A18E5CD855162E56026A8C36B5CD668681D34C00953A308CD4B6A446A");
	// too short, and too uniform
	CHECK(Tlsh(Random(3, 49)).empty());
	CHECK(Tlsh(std::vector<std::byte>(1000)).empty());
	CHECK(Tlsh({}).empty());
}

TEST_CASE("TLSH distances are those of TLSH", "[fuzzy]") {
	using FuzzyHash::Tlsh;
	using FuzzyHash::TlshDistance;
	auto random = Tlsh(Random(1, 200000)), random2 = Tlsh(Random2()), random5k = Tlsh(Random(5, 5000));
	auto text = Tlsh(Text(3000)), text2 = Tlsh(Text(3000, 100));
	CHECK(TlshDistance(random, random2) == 23);
	CHECK(TlshDistance(text, text2) == 5);
	CHECK(TlshDistance(random, text) == 306);
	CHECK(TlshDistance(random, text, false) == 282);
	CHECK(TlshDistance(random5k, random) == 736);
	CHECK(TlshDistance(random5k, random, false) == 268);
	CHECK(TlshDistance(random, random) == 0);
	CHECK(TlshDistance(text2, text) == 5);
	// without the "T1" prefix, and in lowercase
	std::string lower = random2.substr(2);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return (char)tolower(c); });
	CHECK(TlshDistance(random, lower) == 23);

	CHECK(TlshDistance("", random) == -1);
	CHECK(TlshDistance(random.substr(0, 71), random) == -1);
	CHECK(TlshDistance(random + "0", random) == -1);
	CHECK(TlshDistance("T1" + std::string(70, 'G'), random) == -1);
}
