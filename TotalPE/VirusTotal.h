#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Scanning a file with VirusTotal (https://www.virustotal.com), through its API version 3.
//
// The file is identified by its SHA-256 first: if VirusTotal has seen it, the existing report is used and nothing is uploaded.
// Otherwise the file is uploaded and the analysis is waited for. An API key is needed (it is free for personal use).
namespace vt {

struct Detection {
	std::wstring Engine;
	std::wstring Category;	// "malicious" or "suspicious"
	std::wstring Result;	// the name the engine gave to the threat
};

struct Report {
	int Malicious{ 0 }, Suspicious{ 0 }, Harmless{ 0 }, Undetected{ 0 }, TimedOut{ 0 }, Unsupported{ 0 }, Failed{ 0 };
	std::vector<Detection> Detections;	// the engines that flagged the file, by name
	int64_t LastAnalysis{ 0 };			// seconds since 1970, 0 if unknown
	std::wstring FileType;				// "Win32 DLL"
	int TimesSubmitted{ 0 };

	int Flagged() const { return Malicious + Suspicious; }
	// the engines that gave a verdict
	int Judged() const { return Malicious + Suspicious + Harmless + Undetected; }
	bool Empty() const { return Judged() == 0 && Detections.empty(); }
};

enum class Phase {
	Idle,		// nothing was started
	Hashing,
	Lookup,		// asking about the hash
	Uploading,
	Queued,		// uploaded, waiting for the analysis to start
	Analyzing,
	Done,
	Failed,
};

struct Status {
	vt::Phase Step{ Phase::Idle };
	int Percent{ -1 };			// of the upload, -1 if not known
	std::wstring Message;		// what is going on, or what went wrong
	std::wstring Sha256;		// hex, once it is known
	bool Uploaded{ false };		// the file was sent (otherwise VirusTotal knew it already)
	vt::Report Results;

	bool Running() const { return Step != Phase::Idle && Step != Phase::Done && Step != Phase::Failed; }
	std::wstring Link() const;	// the page of the file on virustotal.com, empty without a hash
	std::wstring Summary() const;	// one line: "3 of 70 engines flagged this file"
};

//
// JSON of the API (all of these return false if the JSON is not what is expected)
//

// The "file" object (GET /files/{sha256}). False if the JSON is damaged or has no analysis results (yet).
bool ParseFileReport(std::string const& json, Report& report);

// The "analysis" object (GET /analyses/{id}): its status ("queued", "in-progress" or "completed") and, when completed, the results.
bool ParseAnalysis(std::string const& json, std::string& status, Report& report);

// The id of the analysis that an upload started
bool ParseAnalysisId(std::string const& json, std::string& id);

// The address an upload of a larger file goes to (GET /files/upload_url)
bool ParseUploadUrl(std::string const& json, std::string& url);

// "NotFoundError: File not found", from the error object of a failed request
std::wstring ParseError(std::string const& json);

//
// helpers
//

// The body of an upload is multipart/form-data with one part, "file": the prefix goes in front of the file and the suffix behind it.
std::string MultipartPrefix(std::string const& boundary, std::string const& fileName);
std::string MultipartSuffix(std::string const& boundary);

// SHA-256 of a file, as lowercase hex. Empty if the file cannot be read or 'cancel' is set. 'progress' gets the bytes read and the size.
std::string HashFile(std::wstring const& path, std::atomic_bool const* cancel = nullptr, std::function<void(uint64_t, uint64_t)> progress = nullptr);

// The API key is stored encrypted for the current user (DPAPI): hexadecimal text that only this user on this machine can decrypt.
std::wstring ProtectKey(std::wstring const& key);
std::wstring UnprotectKey(std::wstring const& protectedKey);

// The key from the environment (VT_API_KEY or VIRUSTOTAL_API_KEY), empty if there is none.
std::wstring KeyFromEnvironment();

// A key is 64 hexadecimal digits
bool LooksLikeKey(std::wstring const& key);

// How much a file may be before the upload goes to the address that VirusTotal gives for large files
constexpr uint64_t DirectUploadLimit = 32ull * 1024 * 1024;
constexpr uint64_t UploadLimit = 650ull * 1024 * 1024;

//
// the scan
//

using Notify = std::function<void(Status const&)>;

// Scans a file and reports every change of the status through 'notify', which is called on the thread that runs this function.
// Returns when the scan is finished, failed, or 'cancel' was set. Use a thread for it.
void Scan(std::wstring const& path, std::wstring const& apiKey, std::atomic_bool const& cancel, Notify notify);

}	// namespace vt
