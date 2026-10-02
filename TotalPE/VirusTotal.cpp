#include "pch.h"
#include "VirusTotal.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <random>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;

namespace vt {

namespace {
	std::wstring Widen(std::string const& s) {
		if (s.empty())
			return L"";
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	std::string Narrow(std::wstring const& s) {
		if (s.empty())
			return "";
		int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
		std::string r(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
		return r;
	}

	json Parse(std::string const& text) {
		return json::parse(text, nullptr, false);	// no exceptions: a damaged text gives a "discarded" value
	}

	// the number at obj[name], 0 if it is not there or not a number
	int Int(json const& obj, char const* name) {
		if (!obj.is_object())
			return 0;
		auto it = obj.find(name);
		return it != obj.end() && it->is_number() ? (int)it->get<int64_t>() : 0;
	}

	std::string Str(json const& obj, char const* name) {
		if (!obj.is_object())
			return "";
		auto it = obj.find(name);
		return it != obj.end() && it->is_string() ? it->get<std::string>() : "";
	}

	void ReadStats(json const& stats, Report& r) {
		r.Malicious = Int(stats, "malicious");
		r.Suspicious = Int(stats, "suspicious");
		r.Harmless = Int(stats, "harmless");
		r.Undetected = Int(stats, "undetected");
		r.TimedOut = Int(stats, "timeout") + Int(stats, "confirmed-timeout");
		r.Unsupported = Int(stats, "type-unsupported");
		r.Failed = Int(stats, "failure");
	}

	// { "Engine": { "category": "malicious", "result": "Trojan.X" }, ... }
	void ReadResults(json const& results, Report& r) {
		if (!results.is_object())
			return;
		for (auto it = results.begin(); it != results.end(); ++it) {
			auto category = Str(*it, "category");
			if (category != "malicious" && category != "suspicious")
				continue;
			r.Detections.push_back({ Widen(it.key()), Widen(category), Widen(Str(*it, "result")) });
		}
		std::sort(r.Detections.begin(), r.Detections.end(), [](Detection const& a, Detection const& b) {
			return _wcsicmp(a.Engine.c_str(), b.Engine.c_str()) < 0;
		});
	}

	json const* Attributes(json const& doc) {
		if (!doc.is_object())
			return nullptr;
		auto data = doc.find("data");
		if (data == doc.end() || !data->is_object())
			return nullptr;
		auto attrs = data->find("attributes");
		if (attrs == data->end() || !attrs->is_object())
			return nullptr;
		return &*attrs;
	}
}

bool ParseFileReport(std::string const& text, Report& report) {
	auto doc = Parse(text);
	auto attrs = Attributes(doc);
	if (!attrs)
		return false;

	Report r;
	if (auto it = attrs->find("last_analysis_stats"); it != attrs->end())
		ReadStats(*it, r);
	if (auto it = attrs->find("last_analysis_results"); it != attrs->end())
		ReadResults(*it, r);
	if (auto it = attrs->find("last_analysis_date"); it != attrs->end() && it->is_number())
		r.LastAnalysis = it->get<int64_t>();
	r.FileType = Widen(Str(*attrs, "type_description"));
	r.TimesSubmitted = Int(*attrs, "times_submitted");
	if (r.Empty())
		return false;	// the file is known, but has not been analyzed
	report = std::move(r);
	return true;
}

bool ParseAnalysis(std::string const& text, std::string& status, Report& report) {
	auto doc = Parse(text);
	auto attrs = Attributes(doc);
	if (!attrs)
		return false;
	status = Str(*attrs, "status");
	if (status.empty())
		return false;

	Report r;
	if (auto it = attrs->find("stats"); it != attrs->end())
		ReadStats(*it, r);
	if (auto it = attrs->find("results"); it != attrs->end())
		ReadResults(*it, r);
	if (auto it = attrs->find("date"); it != attrs->end() && it->is_number())
		r.LastAnalysis = it->get<int64_t>();
	// the type of the file is in the information about it
	if (auto meta = doc.find("meta"); meta != doc.end() && meta->is_object())
		if (auto info = meta->find("file_info"); info != meta->end())
			r.FileType = Widen(Str(*info, "type_description"));
	report = std::move(r);
	return true;
}

bool ParseAnalysisId(std::string const& text, std::string& id) {
	auto doc = Parse(text);
	if (!doc.is_object())
		return false;
	auto data = doc.find("data");
	if (data == doc.end())
		return false;
	id = Str(*data, "id");
	return !id.empty();
}

bool ParseUploadUrl(std::string const& text, std::string& url) {
	auto doc = Parse(text);
	if (!doc.is_object())
		return false;
	auto data = doc.find("data");
	if (data == doc.end() || !data->is_string())
		return false;
	url = data->get<std::string>();
	return url.starts_with("https://");
}

std::wstring ParseError(std::string const& text) {
	auto doc = Parse(text);
	if (!doc.is_object())
		return L"";
	auto err = doc.find("error");
	if (err == doc.end() || !err->is_object())
		return L"";
	auto code = Str(*err, "code"), message = Str(*err, "message");
	if (code.empty())
		return Widen(message);
	return Widen(code) + (message.empty() ? L"" : L": " + Widen(message));
}

std::string MultipartPrefix(std::string const& boundary, std::string const& fileName) {
	// the name goes in quotes: leave out what would end them
	std::string safe;
	for (auto c : fileName)
		if (c != '"' && c != '\r' && c != '\n' && c != '\\')
			safe += c;
	return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + safe + "\"\r\n" +
		"Content-Type: application/octet-stream\r\n\r\n";
}

std::string MultipartSuffix(std::string const& boundary) {
	return "\r\n--" + boundary + "--\r\n";
}

std::wstring Status::Link() const {
	return Sha256.empty() ? L"" : L"https://www.virustotal.com/gui/file/" + Sha256;
}

std::wstring Status::Summary() const {
	switch (Step) {
		case Phase::Idle: return L"";
		case Phase::Failed: return L"Failed: " + Message;
		case Phase::Done:
			if (Results.Judged() == 0)
				return L"No results";
			if (Results.Flagged() == 0)
				return std::format(L"No engine flagged this file (0 of {})", Results.Judged());
			return std::format(L"{} of {} engines flagged this file", Results.Flagged(), Results.Judged());
		default: return Message;
	}
}

//
// hashing and keys
//

std::string HashFile(std::wstring const& path, std::atomic_bool const* cancel, std::function<void(uint64_t, uint64_t)> progress) {
	wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	if (!file)
		return "";
	LARGE_INTEGER size{};
	::GetFileSizeEx(file.get(), &size);

	BCRYPT_ALG_HANDLE alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
		return "";
	BCRYPT_HASH_HANDLE hash = nullptr;
	std::string result;
	if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
		std::vector<uint8_t> buffer(1 << 20);
		uint64_t done = 0;
		bool ok = true;
		for (;;) {
			if (cancel && cancel->load()) {
				ok = false;
				break;
			}
			DWORD read = 0;
			if (!::ReadFile(file.get(), buffer.data(), (DWORD)buffer.size(), &read, nullptr)) {
				ok = false;
				break;
			}
			if (read == 0)
				break;
			if (BCryptHashData(hash, buffer.data(), read, 0) != 0) {
				ok = false;
				break;
			}
			done += read;
			if (progress)
				progress(done, (uint64_t)size.QuadPart);
		}
		UCHAR digest[32];
		if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
			for (auto b : digest)
				result += std::format("{:02x}", b);
		BCryptDestroyHash(hash);
	}
	BCryptCloseAlgorithmProvider(alg, 0);
	return result;
}

namespace {
	DATA_BLOB Blob(std::string const& s) {
		return { (DWORD)s.size(), (BYTE*)s.data() };
	}
	const std::string Entropy = "TotalPE.VirusTotal";
}

std::wstring ProtectKey(std::wstring const& key) {
	if (key.empty())
		return L"";
	auto utf8 = Narrow(key);
	DATA_BLOB in = Blob(utf8), entropy = Blob(Entropy), out{};
	if (!::CryptProtectData(&in, L"TotalPE VirusTotal API key", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return L"";
	std::wstring hex;
	for (DWORD i = 0; i < out.cbData; i++)
		hex += std::format(L"{:02x}", out.pbData[i]);
	::LocalFree(out.pbData);
	return hex;
}

std::wstring UnprotectKey(std::wstring const& protectedKey) {
	if (protectedKey.empty() || protectedKey.size() % 2)
		return L"";
	std::string bytes;
	for (size_t i = 0; i < protectedKey.size(); i += 2) {
		int v = 0;
		for (size_t k = 0; k < 2; k++) {
			auto c = protectedKey[i + k];
			int digit = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
			if (digit < 0)
				return L"";
			v = v * 16 + digit;
		}
		bytes += (char)v;
	}
	DATA_BLOB in = Blob(bytes), entropy = Blob(Entropy), out{};
	if (!::CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return L"";
	std::string utf8((const char*)out.pbData, out.cbData);
	::LocalFree(out.pbData);
	return Widen(utf8);
}

std::wstring KeyFromEnvironment() {
	for (auto name : { L"VT_API_KEY", L"VIRUSTOTAL_API_KEY" }) {
		WCHAR value[256]{};
		auto n = ::GetEnvironmentVariableW(name, value, _countof(value));
		if (n > 0 && n < _countof(value))
			return value;
	}
	return L"";
}

bool LooksLikeKey(std::wstring const& key) {
	return key.size() == 64 && std::all_of(key.begin(), key.end(), [](wchar_t c) { return iswxdigit(c) != 0; });
}

//
// HTTP
//

namespace {
	struct Response {
		DWORD Status{ 0 };
		std::string Body;
		std::wstring Error;		// the failure of the connection (not an error status of the server)
	};

	struct Internet {
		HINTERNET h{ nullptr };
		Internet() = default;
		explicit Internet(HINTERNET handle) : h(handle) {}
		Internet(Internet const&) = delete;
		Internet& operator=(Internet const&) = delete;
		~Internet() { if (h) WinHttpCloseHandle(h); }
		explicit operator bool() const { return h != nullptr; }
	};

	std::wstring SystemError(DWORD code) {
		WCHAR text[256]{};
		auto module = ::GetModuleHandleW(L"winhttp.dll");
		if (!::FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, module, code, 0, text, _countof(text), nullptr))
			return std::format(L"error {}", code);
		std::wstring t = text;
		while (!t.empty() && (t.back() == L'\r' || t.back() == L'\n' || t.back() == L' '))
			t.pop_back();
		return t;
	}

	// One request. The body is 'prefix', then the contents of 'filePath' (if any), then 'suffix'.
	Response Send(std::wstring const& method, std::wstring const& url, std::wstring const& headers, std::string const& prefix,
		std::wstring const& filePath, std::string const& suffix, std::atomic_bool const& cancel,
		std::function<void(uint64_t, uint64_t)> const& progress = nullptr) {
		Response response;

		URL_COMPONENTSW parts{ sizeof(parts) };
		WCHAR host[256]{}, path[2048]{};
		parts.lpszHostName = host;
		parts.dwHostNameLength = _countof(host);
		parts.lpszUrlPath = path;
		parts.dwUrlPathLength = _countof(path);
		if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
			response.Error = L"The address is not valid";
			return response;
		}

		Internet session(::WinHttpOpen(L"TotalPE", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		if (!session) {
			response.Error = SystemError(::GetLastError());
			return response;
		}
		::WinHttpSetTimeouts(session.h, 15000, 15000, 120000, 120000);
		Internet connection(::WinHttpConnect(session.h, host, parts.nPort, 0));
		if (!connection) {
			response.Error = SystemError(::GetLastError());
			return response;
		}
		bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
		Internet request(::WinHttpOpenRequest(connection.h, method.c_str(), path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
		if (!request) {
			response.Error = SystemError(::GetLastError());
			return response;
		}

		// the size of the body, which is sent in pieces
		wil::unique_hfile file;
		uint64_t fileSize = 0;
		if (!filePath.empty()) {
			file.reset(::CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
			LARGE_INTEGER size{};
			if (!file || !::GetFileSizeEx(file.get(), &size)) {
				response.Error = L"The file could not be read";
				return response;
			}
			fileSize = (uint64_t)size.QuadPart;
		}
		const uint64_t total = prefix.size() + fileSize + suffix.size();
		const bool hasBody = total > 0;

		if (!::WinHttpSendRequest(request.h, headers.c_str(), (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, hasBody ? (DWORD)std::min<uint64_t>(total, 0xFFFFFFFF) : 0, 0)) {
			response.Error = SystemError(::GetLastError());
			return response;
		}

		if (hasBody) {
			uint64_t sent = 0;
			auto write = [&](const void* data, DWORD size) {
				DWORD written = 0;
				if (!::WinHttpWriteData(request.h, data, size, &written))
					return false;
				sent += written;
				if (progress)
					progress(sent, total);
				return true;
			};
			bool ok = prefix.empty() || write(prefix.data(), (DWORD)prefix.size());
			if (ok && file) {
				std::vector<uint8_t> buffer(256 * 1024);
				for (;;) {
					if (cancel.load()) {
						response.Error = L"Cancelled";
						return response;
					}
					DWORD read = 0;
					if (!::ReadFile(file.get(), buffer.data(), (DWORD)buffer.size(), &read, nullptr)) {
						response.Error = L"The file could not be read";
						return response;
					}
					if (read == 0)
						break;
					if (!(ok = write(buffer.data(), read)))
						break;
				}
			}
			if (ok && !suffix.empty())
				ok = write(suffix.data(), (DWORD)suffix.size());
			if (!ok) {
				response.Error = SystemError(::GetLastError());
				return response;
			}
		}

		if (!::WinHttpReceiveResponse(request.h, nullptr)) {
			response.Error = SystemError(::GetLastError());
			return response;
		}
		DWORD status = 0, size = sizeof(status);
		::WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		response.Status = status;

		for (;;) {
			DWORD available = 0;
			if (!::WinHttpQueryDataAvailable(request.h, &available) || available == 0)
				break;
			std::string chunk(available, '\0');
			DWORD read = 0;
			if (!::WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0)
				break;
			response.Body.append(chunk.data(), read);
			if (response.Body.size() > 64 * 1024 * 1024)
				break;	// no reply of the API is that large
		}
		return response;
	}

	// waits, but returns early (false) when cancelled
	bool Wait(std::chrono::milliseconds time, std::atomic_bool const& cancel) {
		auto end = std::chrono::steady_clock::now() + time;
		while (std::chrono::steady_clock::now() < end) {
			if (cancel.load())
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return !cancel.load();
	}

	std::wstring ErrorText(Response const& r) {
		if (!r.Error.empty())
			return r.Error;
		auto message = ParseError(r.Body);
		switch (r.Status) {
			case 401: return L"VirusTotal rejected the API key" + (message.empty() ? L"" : L" (" + message + L")");
			case 403: return L"The API key is not allowed to do this" + (message.empty() ? L"" : L" (" + message + L")");
			case 429: return L"The quota of the API key is used up: try again later";
		}
		return message.empty() ? std::format(L"VirusTotal answered with status {}", r.Status) : message;
	}
}

//
// the scan
//

void Scan(std::wstring const& path, std::wstring const& apiKey, std::atomic_bool const& cancel, Notify notify) {
	Status status;
	auto publish = [&](Phase phase, std::wstring message, int percent = -1) {
		status.Step = phase;
		status.Message = std::move(message);
		status.Percent = percent;
		if (!cancel.load())
			notify(status);
	};
	auto fail = [&](std::wstring message) { publish(Phase::Failed, std::move(message)); };

	const std::wstring base = L"https://www.virustotal.com/api/v3/";
	const std::wstring headers = L"x-apikey: " + apiKey + L"\r\naccept: application/json\r\n";
	const std::string none;

	// who is the file?
	publish(Phase::Hashing, L"Computing the SHA-256 of the file...");
	auto hash = HashFile(path, &cancel);
	if (cancel.load())
		return;
	if (hash.empty())
		return fail(L"The file could not be read");
	status.Sha256 = Widen(hash);

	// has VirusTotal seen it?
	publish(Phase::Lookup, L"Looking the file up on VirusTotal...");
	auto lookup = Send(L"GET", base + L"files/" + status.Sha256, headers, none, L"", none, cancel);
	if (cancel.load())
		return;
	if (lookup.Status == 200) {
		if (ParseFileReport(lookup.Body, status.Results)) {
			status.Uploaded = false;
			return publish(Phase::Done, L"");
		}
		// known, but never analyzed: it is sent again below
	}
	else if (lookup.Status != 404) {
		return fail(ErrorText(lookup));
	}

	// no: upload it
	uint64_t size = 0;
	{
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
			size = (uint64_t)data.nFileSizeHigh << 32 | data.nFileSizeLow;
	}
	if (size > UploadLimit)
		return fail(L"The file is too large for VirusTotal (the limit is 650 MB)");

	std::wstring uploadUrl = base + L"files";
	if (size > DirectUploadLimit) {
		auto r = Send(L"GET", base + L"files/upload_url", headers, none, L"", none, cancel);
		std::string url;
		if (cancel.load())
			return;
		if (r.Status != 200 || !ParseUploadUrl(r.Body, url))
			return fail(ErrorText(r));
		uploadUrl = Widen(url);
	}

	std::string boundary = "----TotalPE";
	{
		std::random_device rd;
		for (int i = 0; i < 4; i++)
			boundary += std::format("{:08x}", rd());
	}
	auto name = path.substr(path.find_last_of(L"\\/") + 1);
	int lastPercent = -1;
	publish(Phase::Uploading, L"Uploading the file...", 0);
	auto upload = Send(L"POST", uploadUrl, headers + L"content-type: multipart/form-data; boundary=" + Widen(boundary) + L"\r\n",
		MultipartPrefix(boundary, Narrow(name)), path, MultipartSuffix(boundary), cancel, [&](uint64_t sent, uint64_t total) {
			int percent = total ? (int)(sent * 100 / total) : 0;
			if (percent != lastPercent) {
				lastPercent = percent;
				publish(Phase::Uploading, std::format(L"Uploading the file... {}%", percent), percent);
			}
		});
	if (cancel.load())
		return;
	std::string id;
	if (upload.Status != 200 || !ParseAnalysisId(upload.Body, id))
		return fail(ErrorText(upload));
	status.Uploaded = true;

	// wait for the analysis. The public API allows 4 requests a minute, so there is no hurry
	const auto start = std::chrono::steady_clock::now();
	publish(Phase::Queued, L"Uploaded: waiting for the analysis to start...");
	auto delay = std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() - start < std::chrono::minutes(15)) {
		if (!Wait(delay, cancel))
			return;
		delay = std::chrono::seconds(20);

		auto r = Send(L"GET", base + L"analyses/" + Widen(id), headers, none, L"", none, cancel);
		if (cancel.load())
			return;
		if (r.Status == 429) {
			delay = std::chrono::seconds(45);
			continue;
		}
		std::string state;
		Report report;
		if (r.Status != 200 || !ParseAnalysis(r.Body, state, report))
			return fail(ErrorText(r));

		auto seconds = (int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
		if (state == "completed") {
			status.Results = std::move(report);
			return publish(Phase::Done, L"");
		}
		publish(state == "queued" ? Phase::Queued : Phase::Analyzing,
			std::format(L"{} ({} s)...", state == "queued" ? L"Waiting for the analysis to start" : L"Analyzing the file", seconds));
	}
	fail(L"The analysis did not finish in 15 minutes: see the result on virustotal.com");
}

}	// namespace vt
