#include "TestCommon.h"
#include <Capabilities.h>
#include <PEReport.h>
#include <Json.h>
#include <filesystem>

namespace {
	// rules from JSON text, which must have no errors
	CapabilityRules Rules(std::string const& json) {
		CapabilityRules rules;
		std::vector<std::string> errors;
		bool ok = rules.AddJson(json, errors);
		INFO((errors.empty() ? std::string() : errors[0]));
		REQUIRE(ok);
		return rules;
	}

	std::vector<CapabilityMatch> Match(CapabilityRules const& rules, std::vector<uint8_t> const& bytes) {
		TempFile file(bytes, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(file.Path()));
		return rules.Match(pe);
	}

	CapabilityMatch const* Find(std::vector<CapabilityMatch> const& matches, std::string const& name) {
		for (auto const& m : matches)
			if (m.Name == name)
				return &m;
		return nullptr;
	}

	// the names of the rules that match, from a rule of each condition
	std::vector<std::string> Names(std::vector<CapabilityMatch> const& matches) {
		std::vector<std::string> names;
		for (auto const& m : matches)
			names.push_back(m.Name);
		return names;
	}
}

TEST_CASE("Capabilities: the conditions", "[capabilities]") {
	auto rules = Rules(R"([
		{ "name": "api", "namespace": "t/api", "attack": ["T1106"], "condition": { "api": "ExitProcess" } },
		{ "name": "api list", "namespace": "t", "condition": { "api": ["NoSuchThing", "exitprocess"] } },
		{ "name": "api of a module", "namespace": "t", "condition": { "api": "kernel32.ExitProcess" } },
		{ "name": "api of another module", "namespace": "t", "condition": { "api": "user32.ExitProcess" } },
		{ "name": "api prefix is not enough", "namespace": "t", "condition": { "api": "ExitProc" } },
		{ "name": "string", "namespace": "t", "condition": { "string": "EXITPROCESS" } },
		{ "name": "regex", "namespace": "t", "condition": { "regex": "^test\\.dll$" } },
		{ "name": "section", "namespace": "t", "condition": { "section": ".TEXT" } },
		{ "name": "module", "namespace": "t", "condition": { "import-module": "KERNEL32.DLL" } },
		{ "name": "export", "namespace": "t", "condition": { "export": "Beta" } },
		{ "name": "dll", "namespace": "t", "condition": { "characteristic": "dll" } },
		{ "name": "few imports", "namespace": "t", "condition": { "characteristic": "few-imports" } },
		{ "name": "tls", "namespace": "t", "condition": { "characteristic": "tls-callbacks" } },
		{ "name": "all", "namespace": "t", "condition": { "all": [ { "api": "ExitProcess" }, { "export": "Alpha" } ] } },
		{ "name": "all fails", "namespace": "t", "condition": { "all": [ { "api": "ExitProcess" }, { "export": "Gamma" } ] } },
		{ "name": "any", "namespace": "t", "condition": { "any": [ { "export": "Gamma" }, { "export": "Alpha" } ] } },
		{ "name": "2 of 3", "namespace": "t", "condition": { "n-of": { "n": 2, "of": [ { "export": "Alpha" }, { "export": "Beta" }, { "export": "Gamma" } ] } } },
		{ "name": "3 of 3", "namespace": "t", "condition": { "n-of": { "n": 3, "of": [ { "export": "Alpha" }, { "export": "Beta" }, { "export": "Gamma" } ] } } },
		{ "name": "not", "namespace": "t", "condition": { "not": { "section": ".upx0" } } },
		{ "name": "not fails", "namespace": "t", "condition": { "not": { "section": ".text" } } }
	])");
	CHECK(rules.Size() == 20);

	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	auto matches = Match(rules, spec.Build());
	CHECK(Names(matches) == std::vector<std::string>{ "api", "api list", "api of a module", "string", "regex", "section", "module",
		"export", "dll", "few imports", "all", "any", "2 of 3", "not" });

	auto api = Find(matches, "api");
	REQUIRE(api);
	CHECK(api->Namespace == "t/api");
	CHECK(api->Attack == std::vector<std::string>{ "T1106" });
	CHECK(api->Evidence == std::vector<std::string>{ "api: kernel32!ExitProcess" });

	// the evidence of what matched, not of what did not
	auto any = Find(matches, "any");
	REQUIRE(any);
	CHECK(any->Evidence == std::vector<std::string>{ "export: alpha" });
	CHECK(Find(matches, "not")->Evidence.empty());
}

TEST_CASE("Capabilities: names with A, W and Ex", "[capabilities][system]") {
	// sc.exe imports OpenSCManagerW and CreateServiceW (from advapi32, or from an API set that sechost implements)
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	PEFile pe;
	if (!pe.Open(std::wstring(dir) + L"\\sc.exe"))
		SKIP("sc.exe is not on this machine");
	auto rules = Rules(R"([
		{ "name": "base name", "namespace": "t", "condition": { "api": "OpenSCManager" } },
		{ "name": "exact name", "namespace": "t", "condition": { "api": "openscmanagerw" } },
		{ "name": "other suffix", "namespace": "t", "condition": { "api": "OpenSCManagerX" } },
		{ "name": "a longer name", "namespace": "t", "condition": { "api": "OpenSC" } },
		{ "name": "module that has it", "namespace": "t", "condition": { "any": [ { "api": "advapi32.CreateService" }, { "api": "sechost.CreateService" } ] } },
		{ "name": "module that does not", "namespace": "t", "condition": { "api": "user32.CreateService" } }
	])");
	auto matches = rules.Match(pe);
	CHECK(Names(matches) == std::vector<std::string>{ "base name", "exact name", "module that has it" });
	auto base = Find(matches, "base name");
	REQUIRE(base);
	CHECK_THAT(base->Evidence.at(0), Catch::Matchers::EndsWith("!OpenSCManagerW"));
}

TEST_CASE("Capabilities: rules with errors", "[capabilities]") {
	CapabilityRules rules;
	std::vector<std::string> errors;
	CHECK(!rules.AddJson(R"([
		{ "name": "good", "namespace": "t", "condition": { "api": "ExitProcess" } },
		{ "namespace": "t", "condition": { "api": "x" } },
		{ "name": "unknown key", "condition": { "apis": "x" } },
		{ "name": "two keys", "condition": { "api": "x", "string": "y" } },
		{ "name": "bad regex", "condition": { "regex": "(" } },
		{ "name": "bad characteristic", "condition": { "characteristic": "fast" } },
		{ "name": "bad n-of", "condition": { "n-of": { "n": 3, "of": [ { "api": "x" } ] } } },
		{ "name": "empty any", "condition": { "any": [] } }
	])", errors, "test.json"));
	CHECK(rules.Size() == 1);	// the good one is kept
	REQUIRE(errors.size() == 7);
	CHECK(errors[0] == "test.json: rule 2: no \"name\"");
	CHECK_THAT(errors[1], Catch::Matchers::ContainsSubstring("unknown condition \"apis\""));
	CHECK_THAT(errors[2], Catch::Matchers::ContainsSubstring("one key"));
	CHECK_THAT(errors[3], Catch::Matchers::ContainsSubstring("bad regex"));
	CHECK_THAT(errors[4], Catch::Matchers::ContainsSubstring("unknown characteristic \"fast\""));
	CHECK_THAT(errors[5], Catch::Matchers::ContainsSubstring("n-of"));
	CHECK_THAT(errors[6], Catch::Matchers::ContainsSubstring("\"any\" needs"));

	errors.clear();
	CHECK(!rules.AddJson("not json", errors));
	CHECK_THAT(errors.at(0), Catch::Matchers::StartsWith("not JSON"));
	errors.clear();
	CHECK(!rules.AddJson("{}", errors));
	CHECK_THAT(errors.at(0), Catch::Matchers::StartsWith("no rules"));
	errors.clear();
	CHECK(!rules.AddPath(L"C:/no/such/rules", errors));
	CHECK_THAT(errors.at(0), Catch::Matchers::EndsWith("not found"));
}

TEST_CASE("Capabilities: the built-in rules", "[capabilities]") {
	auto const& rules = CapabilityRules::BuiltIn();
	CHECK(rules.Size() >= 40);

	SyntheticPE spec;
	auto matches = Match(rules, spec.Build());
	// a DLL that only imports ExitProcess does nothing much
	for (auto const& m : matches)
		CHECK((m.Namespace.starts_with("executable/") || m.Namespace.starts_with("runtime/")));
}

TEST_CASE("Capabilities: a system program", "[capabilities][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	auto path = std::wstring(dir) + L"\\sc.exe";
	if (!std::filesystem::exists(path))
		SKIP("sc.exe is not on this machine");
	PEFile pe;
	REQUIRE(pe.Open(path));
	auto matches = CapabilityRules::BuiltIn().Match(pe);
	auto create = Find(matches, "create or change a service");
	REQUIRE(create);
	CHECK(create->Attack == std::vector<std::string>{ "T1543.003" });
	CHECK(!create->Evidence.empty());
	CHECK(Find(matches, "start a service"));
	CHECK(!Find(matches, "is a kernel driver"));
}

TEST_CASE("Capabilities in a report", "[capabilities][report]") {
	SyntheticPE spec;
	TempFile file(spec.Build(), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(file.Path()));

	auto rules = Rules(R"([ { "name": "exits", "namespace": "t/exit", "description": "ends the process", "attack": ["T1"],
		"condition": { "api": "ExitProcess" } } ])");
	ReportOptions options;
	options.Capabilities = &rules;
	auto report = BuildReport(pe, options);
	auto const& list = report["capabilities"].AsArray();
	REQUIRE(list.size() == 1);
	CHECK(list[0]["name"].AsString() == "exits");
	CHECK(list[0]["namespace"].AsString() == "t/exit");
	CHECK(list[0]["description"].AsString() == "ends the process");
	CHECK(list[0]["attack"].AsArray().at(0).AsString() == "T1");
	CHECK(list[0]["evidence"].AsArray().at(0).AsString() == "api: kernel32!ExitProcess");
	CHECK(SummarizeReport(report)["capabilities"].AsString() == "exits");

	// a default part
	CHECK(BuildReport(pe)["capabilities"].IsArray());
	ReportOptions none;
	none.Parts = ReportPart::Headers;
	CHECK(!BuildReport(pe, none).AsObject().contains("capabilities"));
}
