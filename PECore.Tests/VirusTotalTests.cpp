#include "TestCommon.h"
#include <VirusTotal.h>

namespace {
	// what GET /files/{sha256} answers (reduced to what is read)
	const char* FileJson = R"({
		"data": { "id": "abc", "type": "file", "attributes": {
			"type_description": "Win32 DLL",
			"times_submitted": 12,
			"last_analysis_date": 1700000000,
			"last_analysis_stats": { "malicious": 2, "suspicious": 1, "harmless": 0, "undetected": 60, "timeout": 1, "type-unsupported": 5, "failure": 0 },
			"last_analysis_results": {
				"Zeta": { "category": "malicious", "result": "Trojan.Gen" },
				"alpha": { "category": "suspicious", "result": "Heur.Suspect" },
				"Mid": { "category": "undetected", "result": null },
				"Beta": { "category": "malicious", "result": "W32.Evil" }
			}
		} }
	})";

	const char* AnalysisJson = R"({
		"data": { "id": "id1", "type": "analysis", "attributes": {
			"status": "completed", "date": 1700000500,
			"stats": { "malicious": 0, "suspicious": 0, "harmless": 3, "undetected": 67, "timeout": 0, "type-unsupported": 4 },
			"results": { "Engine": { "category": "undetected", "result": null } }
		} },
		"meta": { "file_info": { "sha256": "abc", "type_description": "Win32 EXE" } }
	})";
}

TEST_CASE("The report of a file is read", "[virustotal]") {
	vt::Report r;
	REQUIRE(vt::ParseFileReport(FileJson, r));
	CHECK(r.Malicious == 2);
	CHECK(r.Suspicious == 1);
	CHECK(r.Undetected == 60);
	CHECK(r.TimedOut == 1);
	CHECK(r.Unsupported == 5);
	CHECK(r.Flagged() == 3);
	CHECK(r.Judged() == 63);
	CHECK(r.LastAnalysis == 1700000000);
	CHECK(r.FileType == L"Win32 DLL");
	CHECK(r.TimesSubmitted == 12);

	// the engines that flagged it, by name regardless of case; the ones that found nothing are left out
	REQUIRE(r.Detections.size() == 3);
	CHECK(r.Detections[0].Engine == L"alpha");
	CHECK(r.Detections[0].Category == L"suspicious");
	CHECK(r.Detections[0].Result == L"Heur.Suspect");
	CHECK(r.Detections[1].Engine == L"Beta");
	CHECK(r.Detections[2].Engine == L"Zeta");
	CHECK(r.Detections[2].Result == L"Trojan.Gen");
}

TEST_CASE("A report without an analysis is not a result", "[virustotal]") {
	vt::Report r;
	CHECK_FALSE(vt::ParseFileReport(R"({"data":{"attributes":{"type_description":"Win32 DLL"}}})", r));
	CHECK_FALSE(vt::ParseFileReport(R"({"data":{"attributes":{"last_analysis_stats":{"malicious":0}}}})", r));
}

TEST_CASE("Damaged JSON is rejected without exceptions", "[virustotal]") {
	vt::Report r;
	std::string status, id, url;
	for (std::string text : { "", "{", "[]", "null", "42", "\"text\"", "{\"data\":5}", "{\"data\":{\"attributes\":5}}", "{\"data\":[]}", "\xFF\xFE" }) {
		CHECK_FALSE(vt::ParseFileReport(text, r));
		CHECK_FALSE(vt::ParseAnalysis(text, status, r));
		CHECK_FALSE(vt::ParseAnalysisId(text, id));
		CHECK_FALSE(vt::ParseUploadUrl(text, url));
		CHECK(vt::ParseError(text).empty());
	}

	SECTION("fields of the wrong type are read as missing") {
		REQUIRE(vt::ParseFileReport(R"({"data":{"attributes":{"last_analysis_stats":{"malicious":"many","undetected":3},
			"last_analysis_results":{"E":{"category":5,"result":7},"F":"text"},"last_analysis_date":"yesterday","type_description":4}}})", r));
		CHECK(r.Malicious == 0);
		CHECK(r.Undetected == 3);
		CHECK(r.Detections.empty());
		CHECK(r.LastAnalysis == 0);
		CHECK(r.FileType.empty());
	}
	SECTION("every truncation of a real answer") {
		std::string text = FileJson;
		for (size_t i = 0; i < text.size(); i++)
			vt::ParseFileReport(text.substr(0, i), r);
	}
}

TEST_CASE("An analysis is read", "[virustotal]") {
	std::string status;
	vt::Report r;
	REQUIRE(vt::ParseAnalysis(AnalysisJson, status, r));
	CHECK(status == "completed");
	CHECK(r.Harmless == 3);
	CHECK(r.Undetected == 67);
	CHECK(r.Judged() == 70);
	CHECK(r.Flagged() == 0);
	CHECK(r.LastAnalysis == 1700000500);
	CHECK(r.FileType == L"Win32 EXE");

	SECTION("one that has not finished") {
		REQUIRE(vt::ParseAnalysis(R"({"data":{"attributes":{"status":"queued","stats":{}}}})", status, r));
		CHECK(status == "queued");
		CHECK(r.Empty());
	}
	SECTION("one without a status") {
		CHECK_FALSE(vt::ParseAnalysis(R"({"data":{"attributes":{"stats":{}}}})", status, r));
	}
}

TEST_CASE("The answer to an upload", "[virustotal]") {
	std::string id;
	REQUIRE(vt::ParseAnalysisId(R"({"data":{"type":"analysis","id":"MzQ1Njc4OQ=="}})", id));
	CHECK(id == "MzQ1Njc4OQ==");
	CHECK_FALSE(vt::ParseAnalysisId(R"({"data":{"id":""}})", id));

	std::string url;
	REQUIRE(vt::ParseUploadUrl(R"({"data":"https://www.virustotal.com/_ah/upload/AMmf/"})", url));
	CHECK(url == "https://www.virustotal.com/_ah/upload/AMmf/");
	CHECK_FALSE(vt::ParseUploadUrl(R"({"data":"http://not.secure/x"})", url));	// the key is only sent over HTTPS
	CHECK_FALSE(vt::ParseUploadUrl(R"({"data":7})", url));
}

TEST_CASE("Errors of the API", "[virustotal]") {
	CHECK(vt::ParseError(R"({"error":{"code":"WrongCredentialsError","message":"Wrong API key"}})") == L"WrongCredentialsError: Wrong API key");
	CHECK(vt::ParseError(R"({"error":{"message":"Only a message"}})") == L"Only a message");
	CHECK(vt::ParseError(R"({"error":{"code":"QuotaExceededError"}})") == L"QuotaExceededError");
	CHECK(vt::ParseError(R"({"data":{}})").empty());
	CHECK(vt::ParseError(R"({"error":{"code":"X","message":"caf\u00e9"}})") == L"X: caf\u00e9");
}

TEST_CASE("The body of an upload", "[virustotal]") {
	auto prefix = vt::MultipartPrefix("BOUND", "evil.exe");
	CHECK(prefix == "--BOUND\r\nContent-Disposition: form-data; name=\"file\"; filename=\"evil.exe\"\r\nContent-Type: application/octet-stream\r\n\r\n");
	CHECK(vt::MultipartSuffix("BOUND") == "\r\n--BOUND--\r\n");

	// a name cannot break out of its quotes or add headers
	auto hostile = vt::MultipartPrefix("B", "a\"; x=\"y\r\nX-Evil: 1");
	CHECK(hostile.find("X-Evil") != std::string::npos);			// the text stays...
	CHECK(hostile.find("\r\nX-Evil") == std::string::npos);		// ...but not as a header
	CHECK(hostile.find("filename=\"a; x=y") != std::string::npos);
}

TEST_CASE("The SHA-256 of a file", "[virustotal]") {
	std::string abc = "abc";
	TempFile abcFile(abc);
	CHECK(vt::HashFile(abcFile.Path()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	TempFile empty(std::string{});
	CHECK(vt::HashFile(empty.Path()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

	CHECK(vt::HashFile(L"C:\\this\\does\\not\\exist.bin").empty());

	SECTION("a file larger than the buffer, with progress") {
		std::string big(3 * 1024 * 1024 + 17, 'a');
		TempFile bigFile(big);
		uint64_t last = 0, total = 0;
		int calls = 0;
		auto hash = vt::HashFile(bigFile.Path(), nullptr, [&](uint64_t done, uint64_t size) { last = done; total = size; calls++; });
		CHECK(hash.size() == 64);
		CHECK(last == big.size());
		CHECK(total == big.size());
		CHECK(calls >= 3);
	}
	SECTION("cancelled") {
		std::atomic_bool cancel{ true };
		CHECK(vt::HashFile(abcFile.Path(), &cancel).empty());
	}
}

TEST_CASE("The API key is stored encrypted", "[virustotal]") {
	std::wstring key = L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	auto stored = vt::ProtectKey(key);
	REQUIRE_FALSE(stored.empty());
	CHECK(stored.find(key) == std::wstring::npos);
	CHECK(stored.find(L"0123456789abcdef") == std::wstring::npos);
	CHECK(vt::UnprotectKey(stored) == key);

	// two encryptions of the same text differ
	CHECK(vt::ProtectKey(key) != stored);

	CHECK(vt::ProtectKey(L"").empty());
	CHECK(vt::UnprotectKey(L"").empty());
	CHECK(vt::UnprotectKey(L"zz").empty());			// not hexadecimal
	CHECK(vt::UnprotectKey(L"abc").empty());		// odd length
	CHECK(vt::UnprotectKey(L"00112233").empty());	// not something DPAPI made
	auto damaged = stored;
	damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == L'0' ? L'1' : L'0';
	CHECK(vt::UnprotectKey(damaged).empty());
}

TEST_CASE("What a key looks like", "[virustotal]") {
	CHECK(vt::LooksLikeKey(L"0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef"));
	CHECK_FALSE(vt::LooksLikeKey(L""));
	CHECK_FALSE(vt::LooksLikeKey(L"0123456789abcdef"));
	CHECK_FALSE(vt::LooksLikeKey(L"g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
	CHECK_FALSE(vt::LooksLikeKey(L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0"));
}

TEST_CASE("The key from the environment", "[virustotal]") {
	::SetEnvironmentVariableW(L"VT_API_KEY", nullptr);
	::SetEnvironmentVariableW(L"VIRUSTOTAL_API_KEY", nullptr);
	CHECK(vt::KeyFromEnvironment().empty());
	::SetEnvironmentVariableW(L"VIRUSTOTAL_API_KEY", L"second");
	CHECK(vt::KeyFromEnvironment() == L"second");
	::SetEnvironmentVariableW(L"VT_API_KEY", L"first");
	CHECK(vt::KeyFromEnvironment() == L"first");
	::SetEnvironmentVariableW(L"VT_API_KEY", nullptr);
	::SetEnvironmentVariableW(L"VIRUSTOTAL_API_KEY", nullptr);
}

TEST_CASE("The status as text", "[virustotal]") {
	vt::Status s;
	CHECK_FALSE(s.Running());
	CHECK(s.Summary().empty());
	CHECK(s.Link().empty());

	s.Step = vt::Phase::Uploading;
	s.Message = L"Uploading the file... 40%";
	CHECK(s.Running());
	CHECK(s.Summary() == L"Uploading the file... 40%");

	s.Sha256 = L"abc123";
	CHECK(s.Link() == L"https://www.virustotal.com/gui/file/abc123");

	s.Step = vt::Phase::Done;
	CHECK_FALSE(s.Running());
	CHECK(s.Summary() == L"No results");
	s.Results.Undetected = 68;
	s.Results.Harmless = 2;
	CHECK(s.Summary() == L"No engine flagged this file (0 of 70)");
	s.Results.Malicious = 3;
	CHECK(s.Summary() == L"3 of 73 engines flagged this file");

	s.Step = vt::Phase::Failed;
	s.Message = L"VirusTotal rejected the API key";
	CHECK_FALSE(s.Running());
	CHECK(s.Summary() == L"Failed: VirusTotal rejected the API key");
}
