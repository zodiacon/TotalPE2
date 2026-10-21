#include "TestCommon.h"
#include <YaraRules.h>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
	std::span<const std::byte> Bytes(std::vector<uint8_t> const& data) {
		return { (const std::byte*)data.data(), data.size() };
	}

	std::vector<YaraMatch> Scan(YaraRules const& rules, std::vector<uint8_t> const& data, std::wstring const& path = L"") {
		std::vector<YaraMatch> matches;
		std::string error;
		INFO(error);
		REQUIRE(rules.Scan(Bytes(data), path, matches, &error));
		return matches;
	}

	YaraMatch const* Find(std::vector<YaraMatch> const& matches, std::string const& rule) {
		for (auto const& m : matches)
			if (m.Rule == rule)
				return &m;
		return nullptr;
	}

	std::string Describe(YaraRules const& rules) {
		std::string text;
		for (auto const& d : rules.Diagnostics())
			text += std::format("{}{}({}): {}\n", d.Error ? "error " : "warning ", Narrow(d.File), d.Line, d.Message);
		return text;
	}

	// a directory of its own that is deleted with the object
	class TempDir {
	public:
		TempDir() {
			static std::atomic<int> s_Count;
			m_Path = std::filesystem::temp_directory_path() / std::format(L"yara-test-{}-{}-{}", ::GetCurrentProcessId(), ::GetTickCount64(), ++s_Count);
			std::filesystem::create_directories(m_Path);
		}
		~TempDir() {
			std::error_code ec;
			std::filesystem::remove_all(m_Path, ec);
		}
		std::filesystem::path const& Path() const { return m_Path; }
		void Write(std::wstring const& name, std::string const& text) const {
			std::filesystem::create_directories((m_Path / name).parent_path());
			std::ofstream(m_Path / name, std::ios::binary) << text;
		}

	private:
		std::filesystem::path m_Path;
	};
}

TEST_CASE("YARA: a rule with strings, tags and meta", "[yara]") {
	YaraRules rules;
	REQUIRE(rules.LoadText(R"(
rule mz_header : pe exe {
	meta:
		author = "test"
		score = 5
		strict = true
	strings:
		$mz = { 4D 5A }
	condition:
		$mz at 0
}
rule never { condition: false }
)"));
	CHECK(rules.RuleCount() == 2);
	CHECK(rules.Diagnostics().empty());

	SyntheticPE spec;
	auto matches = Scan(rules, spec.Build());
	REQUIRE(matches.size() == 1);
	auto const& m = matches[0];
	CHECK(m.Rule == "mz_header");
	CHECK(m.Namespace == "default");
	CHECK(m.Tags == std::vector<std::string>{ "pe", "exe" });
	CHECK(m.Meta == std::vector<std::pair<std::string, std::string>>{ { "author", "test" }, { "score", "5" }, { "strict", "true" } });
	REQUIRE(!m.Strings.empty());
	CHECK(m.Strings[0].Identifier == "$mz");
	CHECK(m.Strings[0].Offset == 0);
	CHECK(m.Strings[0].Length == 2);
	CHECK(m.Strings[0].Data == std::vector<uint8_t>{ 0x4D, 0x5A });
	CHECK(!m.TooManyMatches);
}

TEST_CASE("YARA: the pe module", "[yara]") {
	YaraRules rules;
	REQUIRE(rules.LoadText(R"(
import "pe"
rule three_sections { condition: pe.number_of_sections == 3 }
rule four_sections { condition: pe.number_of_sections == 4 }
rule is_dll { condition: pe.characteristics & pe.DLL }
rule exports_alpha { condition: pe.exports("Alpha") }
)"));
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	auto matches = Scan(rules, spec.Build());
	CHECK(Find(matches, "three_sections"));
	CHECK(!Find(matches, "four_sections"));
	CHECK(Find(matches, "is_dll"));
	CHECK(Find(matches, "exports_alpha"));
}

TEST_CASE("YARA: external variables of the file", "[yara]") {
	YaraRules rules;
	REQUIRE(rules.LoadText(R"(
rule by_name { condition: filename == "sample.dll" and extension == "dll" and filepath contains "sample" }
rule by_type { condition: filetype == "pe" }
)"));
	SyntheticPE spec;
	auto bytes = spec.Build();
	auto matches = Scan(rules, bytes, L"C:/samples/sample.dll");
	CHECK(Find(matches, "by_name"));
	CHECK(Find(matches, "by_type"));
	CHECK(!Find(Scan(rules, bytes, L"C:/samples/other.exe"), "by_name"));
	CHECK(!Find(Scan(rules, bytes), "by_name"));
}

TEST_CASE("YARA: errors", "[yara]") {
	YaraRules rules;
	SECTION("syntax") {
		CHECK(!rules.LoadText("rule bad {\n condition: no_such_thing\n}"));
		CHECK(!rules.IsLoaded());
		REQUIRE(!rules.Diagnostics().empty());
		CHECK(rules.Diagnostics()[0].Error);
		CHECK(rules.Diagnostics()[0].Line == 2);
		CHECK(!rules.Diagnostics()[0].Message.empty());
		std::vector<YaraMatch> matches;
		std::string error;
		CHECK(!rules.Scan({}, L"", matches, &error));
		CHECK(error == "no rules are loaded");
	}
	SECTION("a path that does not exist") {
		CHECK(!rules.Load(L"C:/no/such/rules.yar"));
		REQUIRE(rules.Diagnostics().size() == 1);
		CHECK(rules.Diagnostics()[0].Message == "not found");
	}
	SECTION("a module that is not there") {
		CHECK(!rules.LoadText("import \"cuckoo\"\nrule c { condition: true }"));
	}
}

TEST_CASE("YARA: a directory of rule files", "[yara]") {
	TempDir dir;
	dir.Write(L"good.yar", "rule good_rule { strings: $a = { 4D 5A } condition: $a }");
	dir.Write(L"sub/more.yara", "import \"pe\"\nrule more_rule { condition: pe.is_pe }");
	dir.Write(L"broken.yar", "rule broken_rule {\n condition: undefined_identifier\n}");
	dir.Write(L"notes.txt", "not a rule file");

	YaraRules rules;
	INFO(Describe(rules));
	REQUIRE(rules.Load(dir.Path().wstring()));
	CHECK(rules.RuleCount() == 2);

	// the broken file is left out, and said so
	auto const& diags = rules.Diagnostics();
	auto broken = std::find_if(diags.begin(), diags.end(), [](auto const& d) { return d.Error && d.File.ends_with(L"broken.yar"); });
	REQUIRE(broken != diags.end());
	CHECK(broken->Line == 2);

	SyntheticPE spec;
	auto matches = Scan(rules, spec.Build());
	auto good = Find(matches, "good_rule");
	REQUIRE(good);
	CHECK(good->Namespace == "good");
	auto more = Find(matches, "more_rule");
	REQUIRE(more);
	CHECK(more->Namespace == "more");
	CHECK(!Find(matches, "broken_rule"));

	SECTION("only broken files") {
		TempDir bad;
		bad.Write(L"x.yar", "rule x { condition: nope }");
		YaraRules none;
		CHECK(!none.Load(bad.Path().wstring()));
		CHECK(!none.Diagnostics().empty());
	}
	SECTION("no rule files") {
		TempDir empty;
		empty.Write(L"readme.md", "nothing");
		YaraRules none;
		CHECK(!none.Load(empty.Path().wstring()));
	}
}

TEST_CASE("YARA: the matches of a string are capped", "[yara]") {
	YaraRules rules;
	REQUIRE(rules.LoadText("rule zeros { strings: $z = { 00 00 } condition: #z > 10 }"));
	SyntheticPE spec;
	auto matches = Scan(rules, spec.Build());
	REQUIRE(matches.size() == 1);
	CHECK(matches[0].StringMatchCount > YaraMatch::MaxStringMatches);
	CHECK(matches[0].Strings.size() == YaraMatch::MaxStringMatches);
}

TEST_CASE("YARA: scanning from several threads", "[yara]") {
	YaraRules rules;
	REQUIRE(rules.LoadText("import \"pe\"\nrule p { strings: $mz = { 4D 5A } condition: $mz at 0 and pe.is_pe }"));
	SyntheticPE spec;
	auto bytes = spec.Build();
	std::atomic<int> ok{ 0 };
	{
		std::vector<std::jthread> threads;
		for (int t = 0; t < 8; t++)
			threads.emplace_back([&] {
				for (int i = 0; i < 50; i++) {
					std::vector<YaraMatch> matches;
					if (rules.Scan(Bytes(bytes), L"", matches) && matches.size() == 1)
						ok++;
				}
			});
	}
	CHECK(ok == 8 * 50);
}

#include <PEReport.h>

TEST_CASE("YARA in a report", "[yara][report]") {
	YaraRules rules;
	REQUIRE(rules.LoadText(R"(
rule entry_ret : code {
	meta:
		description = "a RET"
	strings:
		$ret = { C3 }
	condition:
		$ret at 0x200
}
rule nothing { condition: false }
)"));
	SyntheticPE spec;
	TempFile file(spec.Build(), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));

	SECTION("without rules there is no yara part") {
		auto report = BuildReport(pe);
		CHECK(!report.AsObject().contains("yara"));
		CHECK(SummarizeReport(report)["yara"].IsNull());
	}

	ReportOptions options;
	options.Yara = &rules;
	options.Parts = ReportPart::Headers;	// the yara part does not depend on the parts
	auto report = BuildReport(pe, options);
	auto const& yara = report["yara"];
	REQUIRE(yara.IsObject());
	CHECK(yara["ruleCount"].AsNumber() == 2);
	CHECK(!yara.AsObject().contains("error"));
	REQUIRE(yara["matches"].AsArray().size() == 1);
	auto const& m = yara["matches"].AsArray()[0];
	CHECK(m["rule"].AsString() == "entry_ret");
	CHECK(m["tags"].AsArray().at(0).AsString() == "code");
	CHECK(m["meta"]["description"].AsString() == "a RET");

	// the match at the entry point: in .text, at its RVA
	JsonValue const* entry = nullptr;
	for (auto const& s : m["strings"].AsArray())
		if (s["offset"].AsString() == "0x200")
			entry = &s;
	REQUIRE(entry);
	CHECK((*entry)["id"].AsString() == "$ret");
	CHECK((*entry)["section"].AsString() == ".text");
	CHECK((*entry)["rva"].AsString() == "0x1000");
	CHECK((*entry)["length"].AsNumber() == 1);
	CHECK((*entry)["data"].AsString() == "c3");

	CHECK(SummarizeReport(report)["yara"].AsString() == "entry_ret");
	CHECK(CheckReport(report, {}) == std::vector<std::string>{ "yara: entry_ret" });
}
