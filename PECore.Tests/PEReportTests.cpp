#include "TestCommon.h"
#include <PEReport.h>
#include <FuzzyHash.h>
#include <filesystem>

namespace {
	JsonValue Report(std::vector<uint8_t> const& bytes, ReportOptions const& options = {}) {
		TempFile file(bytes, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(file.Path()));
		return BuildReport(pe, options);
	}

	// the report as text and back: what a reader of the output gets
	JsonValue RoundTrip(JsonValue const& report) {
		JsonValue parsed;
		std::string error;
		INFO(error);
		REQUIRE(JsonValue::Parse(report.Write(false), parsed, &error));
		return parsed;
	}

	bool Has(JsonValue const& object, std::string const& name) {
		return object.AsObject().contains(name);
	}

	JsonValue const* FindByName(JsonValue const& array, std::string const& key, std::string const& value) {
		for (auto const& item : array.AsArray())
			if (item[key].AsString() == value)
				return &item;
		return nullptr;
	}

	bool Contains(JsonValue const& array, std::string const& value) {
		for (auto const& item : array.AsArray())
			if (item.AsString() == value)
				return true;
		return false;
	}
}

TEST_CASE("A report of a synthetic DLL", "[report]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	CAPTURE(spec.Is64);
	auto report = RoundTrip(Report(spec.Build()));

	CHECK(report["schema"].AsString() == ReportSchema);
	CHECK(report["errors"].IsArray());
	CHECK(report["errors"].AsArray().empty());
	CHECK(!Has(report, "error"));

	SECTION("file") {
		auto const& file = report["file"];
		CHECK(file["size"].AsNumber() == SyntheticPE::FileSize);
		CHECK(file["md5"].AsString().size() == 32);
		CHECK(file["sha1"].AsString().size() == 40);
		auto sha256 = file["sha256"].AsString();
		CHECK(sha256.size() == 64);
		CHECK(std::all_of(sha256.begin(), sha256.end(), [](char c) { return isdigit(c) || (c >= 'a' && c <= 'f'); }));
		CHECK(file["imphash"].AsString().size() == 32);
	}
	SECTION("headers") {
		auto const& h = report["headers"];
		CHECK(h["format"].AsString() == (spec.Is64 ? "PE32+" : "PE32"));
		CHECK(h["machine"].AsString() == (spec.Is64 ? "AMD64" : "I386"));
		CHECK(h["imageBase"].AsString() == (spec.Is64 ? "0x140000000" : "0x400000"));
		CHECK(h["entryPoint"].AsString() == "0x1000");
		CHECK(h["sizeOfImage"].AsNumber() == SyntheticPE::ImageSize);
		CHECK(h["timestamp"].AsString() == "2020-07-04T04:05:20Z");
		CHECK(h["timestampRaw"].AsNumber() == SyntheticPE::TimeDateStamp);
		CHECK(h["reproducible"].AsBool() == false);
		CHECK(h["isDll"].AsBool());
		CHECK(!h["isDriver"].AsBool());
		CHECK(!h["isManaged"].AsBool());
		CHECK(Contains(h["characteristics"], "DLL"));
		CHECK(Contains(h["characteristics"], "EXECUTABLE_IMAGE"));
		CHECK(h["checksum"]["stored"].IsString());
		CHECK(h["checksum"]["valid"].IsBool());
	}
	SECTION("sections") {
		auto const& sections = report["sections"].AsArray();
		REQUIRE(sections.size() == 3);
		auto text = FindByName(report["sections"], "name", ".text");
		REQUIRE(text);
		CHECK((*text)["rva"].AsString() == "0x1000");
		CHECK((*text)["rawOffset"].AsString() == "0x200");
		CHECK((*text)["rawSize"].AsNumber() == SyntheticPE::SectionSize);
		CHECK(Contains((*text)["flags"], "CODE"));
		CHECK(Contains((*text)["flags"], "EXECUTE"));
		CHECK((*text)["entropy"].AsNumber() < 1);
		CHECK(!(*text)["highEntropy"].AsBool());
		CHECK((*text)["md5"].AsString().size() == 32);
		CHECK(Has(*text, "tlsh"));
	}
	SECTION("imports") {
		auto kernel32 = FindByName(report["imports"], "module", "kernel32.dll");
		REQUIRE(kernel32);
		CHECK(!(*kernel32)["delay"].AsBool());
		REQUIRE((*kernel32)["functions"].AsArray().size() == 1);
		CHECK((*kernel32)["functions"].AsArray()[0]["name"].AsString() == "ExitProcess");
	}
	SECTION("exports") {
		auto const& exports = report["exports"];
		CHECK(exports["dllName"].AsString() == "test.dll");
		CHECK(exports["count"].AsNumber() == 2);
		CHECK(!exports["truncated"].AsBool());
		auto alpha = FindByName(exports["functions"], "name", "Alpha");
		REQUIRE(alpha);
		CHECK((*alpha)["rva"].AsString() == "0x1000");
		auto beta = FindByName(exports["functions"], "name", "Beta");
		REQUIRE(beta);
		CHECK((*beta)["rva"].AsString() == "0x1010");
	}
	SECTION("the rest") {
		CHECK(report["signature"]["present"].AsBool() == false);
		CHECK(report["signature"]["kind"].AsString() == "none");
		CHECK(!Has(report["signature"], "trusted"));	// not asked
		CHECK(FindByName(report["security"], "name", "ASLR"));
		CHECK(report["overlay"]["size"].AsNumber() == 0);
		CHECK(report["anomalies"].IsArray());
		CHECK(!Has(report, "rtti"));	// not a default part
		CHECK(!Has(report, "tls"));
		CHECK(!Has(report, "version"));
	}
}

TEST_CASE("A report has the fuzzy hashes of the file", "[report][fuzzy]") {
	SyntheticPE spec;
	auto bytes = spec.Build();
	// some text in a section, so that the file is not too uniform for TLSH
	std::string text;
	for (int i = 0; text.size() < SyntheticPE::SectionSize; i++)
		text += std::format("{} the quick brown fox ", i);
	std::copy_n(text.begin(), SyntheticPE::SectionSize, bytes.begin() + 0x200);
	auto report = RoundTrip(Report(bytes));
	auto span = std::as_bytes(std::span(bytes));
	auto const& file = report["file"];
	CHECK(file["ssdeep"].AsString() == FuzzyHash::Ssdeep(span));
	REQUIRE(file["tlsh"].IsString());
	CHECK(file["tlsh"].AsString() == FuzzyHash::Tlsh(span));
	CHECK(file["tlsh"].AsString().size() == 72);

	auto row = SummarizeReport(report);
	CHECK(row["ssdeep"].AsString() == file["ssdeep"].AsString());
	CHECK(row["tlsh"].AsString() == file["tlsh"].AsString());

	auto section = FindByName(report["sections"], "name", ".text");
	REQUIRE(section);
	CHECK((*section)["tlsh"].AsString() == FuzzyHash::Tlsh(span.subspan(0x200, SyntheticPE::SectionSize)));
}

TEST_CASE("A report shows imports by ordinal", "[report]") {
	SyntheticPE spec;
	spec.ImportModule = "ws2_32.dll";
	spec.ImportByOrdinal = true;
	spec.ImportOrdinal = 10;
	ReportOptions options;
	options.ResolveOrdinals = false;
	auto report = Report(spec.Build(), options);
	auto ws2 = FindByName(report["imports"], "module", "ws2_32.dll");
	REQUIRE(ws2);
	auto const& fn = (*ws2)["functions"].AsArray().at(0);
	CHECK(fn["ordinal"].AsNumber() == 10);
	CHECK(!Has(fn, "name"));
	CHECK(!Has(fn, "resolvedName"));
}

TEST_CASE("A report has only the parts asked for", "[report]") {
	SyntheticPE spec;
	ReportOptions options;
	options.Parts = ReportPart::Headers | ReportPart::Exports;
	options.MaxExports = 1;
	auto report = Report(spec.Build(), options);

	CHECK(Has(report, "headers"));
	CHECK(Has(report, "exports"));
	for (auto name : { "sections", "imports", "debug", "signature", "security", "overlay", "anomalies" }) {
		INFO(name);
		CHECK(!Has(report, name));
	}
	// the path is always there, to tell the reports of a scan apart
	CHECK(report["file"]["path"].IsString());
	CHECK(!Has(report["file"], "sha256"));

	CHECK(report["exports"]["count"].AsNumber() == 2);
	CHECK(report["exports"]["functions"].AsArray().size() == 1);
	CHECK(report["exports"]["truncated"].AsBool());
}

TEST_CASE("A report of a damaged file is still a report", "[report]") {
	SyntheticPE spec;
	auto bytes = spec.Build();
	SECTION("names that are not UTF-8") {
		// the name of the first section
		memcpy(bytes.data() + spec.SectionHeaderOffset(0), "\xFF\xFEtxt\x80\0\0", 8);
		auto report = RoundTrip(Report(bytes, { ReportPart::All }));
		CHECK(report["sections"].AsArray()[0]["name"].AsString() == "\\xFF\\xFEtxt\\x80");
	}
	SECTION("cut short") {
		bytes.resize(SyntheticPE::RdataOffset + 0x40);
		TempFile file(bytes, L".dll");
		PEFile pe;
		if (pe.Open(file.Path())) {
			auto report = RoundTrip(BuildReport(pe, { ReportPart::All }));
			CHECK(report["schema"].AsString() == ReportSchema);
			CHECK(report["errors"].IsArray());
		}
	}
	SECTION("random bytes") {
		std::vector<uint8_t> junk(0x1000);
		for (size_t i = 0; i < junk.size(); i++)
			junk[i] = (uint8_t)(i * 2654435761u >> 13);
		junk[0] = 'M';
		junk[1] = 'Z';
		TempFile file(junk, L".dll");
		PEFile pe;
		if (pe.Open(file.Path())) {
			auto report = RoundTrip(BuildReport(pe, { ReportPart::All }));
			CHECK(report["schema"].AsString() == ReportSchema);
		}
	}
}

TEST_CASE("An error report", "[report]") {
	auto report = RoundTrip(BuildErrorReport(L"C:\\x\\notes.txt", "not a PE file"));
	CHECK(report["schema"].AsString() == ReportSchema);
	CHECK(report["file"]["path"].AsString() == "C:\\x\\notes.txt");
	CHECK(report["error"].AsString() == "not a PE file");

	auto row = SummarizeReport(report);
	CHECK(row["path"].AsString() == "C:\\x\\notes.txt");
	CHECK(row["error"].AsString() == "not a PE file");
	CHECK(row["machine"].IsNull());
}

TEST_CASE("The summary of a report", "[report]") {
	SyntheticPE spec;
	auto report = Report(spec.Build());
	auto row = SummarizeReport(report);

	auto const& columns = ReportSummaryColumns();
	CHECK(row.AsObject().size() == columns.size());
	for (auto const& c : columns) {
		INFO(c);
		CHECK(Has(row, c));
	}
	CHECK(row["machine"].AsString() == "AMD64");
	CHECK(row["isDll"].AsBool());
	CHECK(row["signed"].AsBool() == false);
	CHECK(row["trusted"].IsNull());		// not checked
	CHECK(row["aslr"].IsString());
	CHECK(row["sha256"].AsString() == report["file"]["sha256"].AsString());
	CHECK(row["maxEntropy"].IsNumber());
	CHECK(row["anomalyErrors"].IsNumber());
	CHECK(row["error"].IsNull());
}

TEST_CASE("Report part names", "[report]") {
	ReportPart parts{};
	CHECK(ParseReportParts("headers,imports", parts));
	CHECK(parts == (ReportPart::Headers | ReportPart::Imports));
	CHECK(ParseReportParts(" Default , rtti ", parts));
	CHECK(parts == (ReportPart::Default | ReportPart::Rtti));
	CHECK(ParseReportParts("all", parts));
	CHECK(parts == ReportPart::All);

	std::string unknown;
	parts = ReportPart::Headers;
	CHECK(!ParseReportParts("headers,bogus", parts, &unknown));
	CHECK(unknown == "bogus");
	CHECK(parts == ReportPart::Headers);	// unchanged
}

TEST_CASE("A report of kernel32.dll", "[report][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	auto path = std::wstring(dir) + L"\\kernel32.dll";
	if (!std::filesystem::exists(path))
		SKIP("kernel32.dll does not exist on this machine");
	PEFile pe;
	REQUIRE(pe.Open(path));
	auto report = RoundTrip(BuildReport(pe, { ReportPart::All }));

	CHECK(report["errors"].AsArray().empty());
	CHECK(report["headers"]["isDll"].AsBool());
	// Windows is built with /Brepro: the timestamp is a hash
	if (report["headers"]["reproducible"].AsBool())
		CHECK(report["headers"]["timestamp"].IsNull());
	CHECK(report["exports"]["count"].AsNumber() > 1000);
	// kernel32 forwards much of itself to kernelbase and ntdll
	bool forwarder = false;
	for (auto const& fn : report["exports"]["functions"].AsArray())
		forwarder |= fn["forwarder"].IsString();
	CHECK(forwarder);
	CHECK(FindByName(report["imports"], "module", "ntdll.dll"));
	CHECK(report["version"]["strings"]["CompanyName"].AsString() == "Microsoft Corporation");

	auto codeView = FindByName(report["debug"], "type", "CODEVIEW");
	REQUIRE(codeView);
	CHECK_THAT((*codeView)["pdb"].AsString(), Catch::Matchers::EndsWith("kernel32.pdb", Catch::CaseSensitive::No));
	CHECK((*codeView)["guid"].AsString().size() == 38);
	CHECK(Has(report, "rich") == pe.GetFileInfo()->HasRichHdr);

	auto row = SummarizeReport(report);
	CHECK(row["company"].AsString() == "Microsoft Corporation");
	CHECK(row["fileVersion"].AsString().starts_with("10."));
	CHECK_THAT(row["pdb"].AsString(), Catch::Matchers::EndsWith("kernel32.pdb", Catch::CaseSensitive::No));
}

TEST_CASE("A report of a file signed in a catalog", "[report][system]") {
	auto path = FindCatalogSignedFile();
	if (path.empty())
		SKIP("no system file signed in a catalog was found");
	INFO(Narrow(path));
	PEFile pe;
	REQUIRE(pe.Open(path));

	SECTION("trust verified") {
		ReportOptions options;
		options.VerifyTrust = true;
		auto report = RoundTrip(BuildReport(pe, options));
		auto const& sig = report["signature"];
		CHECK(sig["kind"].AsString() == "catalog");
		CHECK(sig["present"].AsBool());
		CHECK(sig["signatures"].AsArray().empty());
		CHECK_THAT(sig["catalog"]["path"].AsString(), Catch::Matchers::EndsWith(".cat", Catch::CaseSensitive::No));
		CHECK(sig["catalog"]["signature"]["signer"]["subject"].AsString().find("Microsoft") != std::string::npos);
		CHECK(sig["trusted"].AsBool());
		CHECK(sig["trustCode"].AsString() == "0x00000000");

		auto feature = FindByName(report["security"], "name", "Signature");
		REQUIRE(feature);
		CHECK((*feature)["state"].AsString() == "enabled");
		CHECK((*feature)["details"].AsString().find("catalog") != std::string::npos);

		auto row = SummarizeReport(report);
		CHECK(row["signed"].AsBool());
		CHECK(row["signatureKind"].AsString() == "catalog");
		CHECK(row["signer"].AsString().find("Microsoft") != std::string::npos);
		CHECK(row["trusted"].AsBool());
	}
	SECTION("trust not verified: the catalog is still found") {
		auto report = BuildReport(pe);
		CHECK(report["signature"]["kind"].AsString() == "catalog");
		CHECK(!Has(report["signature"], "trusted"));
		CHECK(FindByName(report["security"], "name", "Signature")->operator[]("state").AsString() == "enabled");
	}
	SECTION("catalogs not looked for") {
		ReportOptions options;
		options.CheckCatalogs = false;
		auto report = BuildReport(pe, options);
		CHECK(report["signature"]["kind"].AsString() == "none");
		CHECK(!report["signature"]["present"].AsBool());
		CHECK(!Has(report["signature"], "catalog"));
		CHECK(FindByName(report["security"], "name", "Signature")->operator[]("state").AsString() == "disabled");
	}
}

TEST_CASE("The trust of an unsigned file", "[report]") {
	SyntheticPE spec;
	ReportOptions options;
	options.VerifyTrust = true;
	auto report = Report(spec.Build(), options);
	auto const& sig = report["signature"];
	CHECK(sig["kind"].AsString() == "none");
	CHECK(!sig["present"].AsBool());
	CHECK(!sig["trusted"].AsBool());
	CHECK(sig["trustCode"].AsString() == "0x800B0100");
}

namespace {
	struct OpenedPE {
		TempFile File;
		PEFile PE;
		explicit OpenedPE(std::vector<uint8_t> const& bytes) : File(bytes, L".dll") {
			REQUIRE(PE.Open(File.Path()));
		}
	};

	JsonValue const* FindItem(JsonValue const& diff, std::string const& category, std::string const& name) {
		for (auto const& item : diff["items"].AsArray())
			if (item["category"].AsString() == category && item["name"].AsString() == name)
				return &item;
		return nullptr;
	}

	bool HasFailure(std::vector<std::string> const& failures, std::string const& prefix) {
		return std::any_of(failures.begin(), failures.end(), [&](auto const& f) { return f.starts_with(prefix); });
	}
}

TEST_CASE("A diff report of the same file", "[report][diff]") {
	SyntheticPE spec;
	OpenedPE a(spec.Build()), b(spec.Build());
	auto diff = RoundTrip(BuildDiffReport(a.PE, b.PE));
	CHECK(diff["schema"].AsString() == DiffSchema);
	CHECK(diff["status"].AsString() == "identical");
	CHECK(diff["items"].AsArray().empty());
	CHECK(diff["counts"]["changed"].AsNumber() == 0);
	CHECK(diff["left"]["sha256"].AsString() == diff["right"]["sha256"].AsString());
	CHECK(diff["left"]["ssdeep"].AsString() == diff["right"]["ssdeep"].AsString());
	CHECK(diff["similarity"]["ssdeep"].AsNumber() == 100);
	CHECK((diff["similarity"]["tlsh"].IsNull() || diff["similarity"]["tlsh"].AsNumber() == 0));

	auto all = BuildDiffReport(a.PE, b.PE, true);
	REQUIRE(!all["items"].AsArray().empty());
	for (auto const& item : all["items"].AsArray())
		CHECK(item["status"].AsString() == "same");
}

TEST_CASE("A diff report of two versions", "[report][diff]") {
	SyntheticPE older, newer;
	newer.ImportModule = "user32.dll";
	OpenedPE a(older.Build()), b(newer.Build());
	auto diff = RoundTrip(BuildDiffReport(a.PE, b.PE));
	CHECK(diff["status"].AsString() == "changed");
	CHECK(diff["left"]["path"].AsString() != diff["right"]["path"].AsString());

	auto removed = FindItem(diff, "import", "kernel32.dll");
	REQUIRE(removed);
	CHECK((*removed)["status"].AsString() == "removed");
	auto added = FindItem(diff, "import", "user32.dll");
	REQUIRE(added);
	CHECK((*added)["status"].AsString() == "added");
	CHECK(diff["counts"]["added"].AsNumber() >= 1);
	CHECK(diff["counts"]["removed"].AsNumber() >= 1);
	CHECK(diff["similarity"]["ssdeep"].IsNumber());
	CHECK(Has(diff["similarity"], "tlsh"));
	for (auto const& item : diff["items"].AsArray())
		CHECK(item["status"].AsString() != "same");
}

TEST_CASE("A diff report of a file on one side", "[report][diff]") {
	auto added = RoundTrip(BuildOneSidedDiffReport(L"C:/new/a.dll", true));
	CHECK(added["status"].AsString() == "added");
	CHECK(added["left"].IsNull());
	CHECK(added["right"]["path"].AsString() == "C:/new/a.dll");
	auto removed = BuildOneSidedDiffReport(L"C:/old/a.dll", false);
	CHECK(removed["status"].AsString() == "removed");
	CHECK(removed["right"].IsNull());
}

TEST_CASE("Requirement names", "[report][check]") {
	std::vector<std::string> requirements;
	CHECK(ParseRequirements("default", requirements));
	CHECK(requirements == std::vector<std::string>{ "aslr", "dep", "cfg", "gs", "no-wx" });
	CHECK(ParseRequirements("CET, signed,cet,default", requirements));
	CHECK(requirements == std::vector<std::string>{ "cet", "signed", "aslr", "dep", "cfg", "gs", "no-wx" });

	std::string unknown;
	CHECK(!ParseRequirements("aslr,bogus", requirements, &unknown));
	CHECK(unknown == "bogus");
	CHECK(requirements.size() == 7);	// unchanged
}

TEST_CASE("Checking a report", "[report][check]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	CAPTURE(spec.Is64);
	auto bytes = spec.Build();
	auto setDllCharacteristics = [&](uint16_t flags) { memcpy(bytes.data() + spec.OptionalHeaderOffset() + 70, &flags, 2); };

	SECTION("nothing set") {
		setDllCharacteristics(0);
		auto report = Report(bytes);
		auto failures = CheckReport(report, { "aslr", "cfg", "signed", "no-wx" });
		INFO(failures.size());
		CHECK(HasFailure(failures, "aslr (ASLR): disabled"));
		CHECK(HasFailure(failures, "cfg (Control Flow Guard): disabled"));
		CHECK(HasFailure(failures, "signed: the file is not signed"));
		CHECK(!HasFailure(failures, "no-wx"));
		CHECK(failures.size() == 3);
	}
	SECTION("ASLR without relocations is partial, which fails") {
		setDllCharacteristics(IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT);
		auto failures = CheckReport(Report(bytes), { "aslr", "dep" });
		REQUIRE(failures.size() == 1);
		CHECK(failures[0].starts_with("aslr (ASLR): partial - "));
	}
	SECTION("what does not apply passes") {
		auto failures = CheckReport(Report(bytes), { "safeseh" });
		CHECK(failures.empty() == spec.Is64);
	}
	SECTION("a feature shown only when it is set") {
		setDllCharacteristics(0);
		CHECK(HasFailure(CheckReport(Report(bytes), { "force-integrity" }), "force-integrity (Force Integrity): disabled"));
		setDllCharacteristics(IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY);
		CHECK(CheckReport(Report(bytes), { "force-integrity" }).empty());
	}
	SECTION("trust") {
		CHECK(HasFailure(CheckReport(Report(bytes), { "trusted" }), "trusted: the trust of the signature was not verified"));
		ReportOptions options;
		options.VerifyTrust = true;
		CHECK(HasFailure(CheckReport(Report(bytes, options), { "trusted" }), "trusted: The file is not signed"));
	}
	SECTION("parts that are missing") {
		ReportOptions options;
		options.Parts = ReportPart::Headers;
		auto failures = CheckReport(Report(bytes, options), { "aslr", "signed" });
		CHECK(HasFailure(failures, "aslr: the report has no security part"));
		CHECK(HasFailure(failures, "signed: the report has no signature part"));
	}
}

TEST_CASE("Checking the anomalies of a report", "[report][check]") {
	JsonValue::Array anomalies{
		JsonValue::Object{ { "severity", "info" } },
		JsonValue::Object{ { "severity", "warning" } },
		JsonValue::Object{ { "severity", "warning" } },
		JsonValue::Object{ { "severity", "error" } },
	};
	JsonValue report = JsonValue::Object{ { "anomalies", anomalies } };
	CHECK(CheckReport(report, {}).empty());
	CHECK(CheckReport(report, {}, "error") == std::vector<std::string>{ "anomalies: 1 error" });
	CHECK(CheckReport(report, {}, "warning") == std::vector<std::string>{ "anomalies: 1 error, 2 warnings" });
	CHECK(CheckReport(report, {}, "info") == std::vector<std::string>{ "anomalies: 1 error, 2 warnings, 1 info" });

	JsonValue clean = JsonValue::Object{ { "anomalies", JsonValue::Array{} } };
	CHECK(CheckReport(clean, {}, "info").empty());
}

TEST_CASE("Checking an error report", "[report][check]") {
	auto failures = CheckReport(BuildErrorReport(L"x.txt", "not a PE file"), { "aslr" });
	CHECK(failures == std::vector<std::string>{ "not analyzed: not a PE file" });
}
