#include "pch.h"
#include "MetadataPackages.h"
#include <winhttp.h>
#include <shlobj.h>
#include <zlib.h>
#include <fstream>

#pragma comment(lib, "winhttp")

namespace {
	// a GET of api.nuget.org: the body (false if it fails, or is not 200)
	bool Get(std::wstring const& path, std::string& body, std::function<void(size_t, size_t)> const& progress = {}) {
		body.clear();
		bool ok = false;
		auto session = ::WinHttpOpen(L"Win32MetaCore", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
			return false;
		::WinHttpSetTimeouts(session, 10000, 10000, 30000, 60000);
		if (auto connection = ::WinHttpConnect(session, L"api.nuget.org", INTERNET_DEFAULT_HTTPS_PORT, 0)) {
			if (auto request = ::WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
				if (::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
					::WinHttpReceiveResponse(request, nullptr)) {
					DWORD status = 0, size = sizeof(status);
					::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
						&status, &size, WINHTTP_NO_HEADER_INDEX);
					DWORD length = 0;
					size = sizeof(length);
					if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
						&length, &size, WINHTTP_NO_HEADER_INDEX))
						length = 0;
					if (status == 200) {
						if (length)
							body.reserve(length);
						ok = true;
						DWORD available = 0;
						while (::WinHttpQueryDataAvailable(request, &available) && available) {
							auto at = body.size();
							body.resize(at + available);
							DWORD read = 0;
							if (!::WinHttpReadData(request, body.data() + at, available, &read)) {
								ok = false;
								break;
							}
							body.resize(at + read);
							if (progress)
								progress(body.size(), length);
							if (!read)
								break;
						}
					}
				}
				::WinHttpCloseHandle(request);
			}
			::WinHttpCloseHandle(connection);
		}
		::WinHttpCloseHandle(session);
		return ok;
	}

	template<typename T>
	T Read(std::vector<uint8_t> const& data, size_t offset) {
		T value{};
		if (offset + sizeof(T) <= data.size())
			memcpy(&value, data.data() + offset, sizeof(T));
		return value;
	}
}

std::vector<std::string> MetadataPackages::Versions(std::wstring const& package) {
	// {"versions": ["0.1.40-experimental", ...]}
	std::string body;
	if (!Get(L"/v3-flatcontainer/" + package + L"/index.json", body))
		return {};
	std::vector<std::string> versions;
	auto list = body.find("\"versions\"");
	if (list == std::string::npos)
		return {};
	auto end = body.find(']', list);
	for (auto at = body.find('"', body.find('[', list)); at != std::string::npos && at < end; ) {
		auto close = body.find('"', at + 1);
		if (close == std::string::npos || close > end)
			break;
		versions.push_back(body.substr(at + 1, close - at - 1));
		at = body.find('"', close + 1);
	}
	return versions;
}

bool MetadataPackages::Download(std::wstring const& package, std::string const& version, std::vector<uint8_t>& data,
	std::function<void(size_t, size_t)> const& progress) {
	std::wstring v(version.begin(), version.end());
	std::string body;
	if (!Get(L"/v3-flatcontainer/" + package + L"/" + v + L"/" + package + L"." + v + L".nupkg", body, progress))
		return false;
	data.assign(body.begin(), body.end());
	return true;
}

// The central directory (its end: the last record of a zip), and in it each file: where it is (its local header), how it
// is compressed (0: not, 8: deflated), how large
bool MetadataPackages::Extract(std::vector<uint8_t> const& zip, std::vector<std::pair<std::string, std::wstring>> const& files) {
	constexpr uint32_t EndSignature = 0x06054B50, EntrySignature = 0x02014B50, LocalSignature = 0x04034B50;
	if (zip.size() < 22)
		return false;
	size_t end = std::string::npos;
	for (size_t at = zip.size() - 22; ; at--) {
		if (Read<uint32_t>(zip, at) == EndSignature) {
			end = at;
			break;
		}
		if (at == 0 || zip.size() - at > 0x10000 + 22)
			break;
	}
	if (end == std::string::npos)
		return false;
	auto count = Read<uint16_t>(zip, end + 10);
	size_t entry = Read<uint32_t>(zip, end + 16);
	size_t found = 0;
	for (uint16_t i = 0; i < count && entry + 46 <= zip.size(); i++) {
		if (Read<uint32_t>(zip, entry) != EntrySignature)
			return false;
		auto method = Read<uint16_t>(zip, entry + 10);
		size_t compressed = Read<uint32_t>(zip, entry + 20), size = Read<uint32_t>(zip, entry + 24);
		size_t nameLength = Read<uint16_t>(zip, entry + 28), extra = Read<uint16_t>(zip, entry + 30), comment = Read<uint16_t>(zip, entry + 32);
		size_t local = Read<uint32_t>(zip, entry + 42);
		if (entry + 46 + nameLength > zip.size())
			return false;
		std::string name((char const*)zip.data() + entry + 46, nameLength);
		entry += 46 + nameLength + extra + comment;
		auto wanted = std::ranges::find(files, name, &std::pair<std::string, std::wstring>::first);
		if (wanted == files.end())
			continue;
		if (Read<uint32_t>(zip, local) != LocalSignature)
			return false;
		auto data = local + 30 + Read<uint16_t>(zip, local + 26) + Read<uint16_t>(zip, local + 28);
		if (data + compressed > zip.size())
			return false;
		std::vector<uint8_t> content(size);
		if (method == 0) {
			if (compressed != size)
				return false;
			memcpy(content.data(), zip.data() + data, size);
		}
		else if (method == 8) {
			// (raw deflate: no zlib header)
			z_stream z{};
			if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
				return false;
			z.next_in = const_cast<Bytef*>(zip.data() + data);
			z.avail_in = (uInt)compressed;
			z.next_out = content.data();
			z.avail_out = (uInt)size;
			auto result = inflate(&z, Z_FINISH);
			inflateEnd(&z);
			if (result != Z_STREAM_END || z.total_out != size)
				return false;
		}
		else
			return false;
		std::ofstream out(wanted->second, std::ios::binary | std::ios::trunc);
		if (!out.write((char const*)content.data(), content.size()))
			return false;
		found++;
	}
	return found == files.size();
}

bool MetadataPackages::Install(std::string const& version, std::string const& wdkVersion, std::wstring const& folder,
	std::function<void(std::wstring const&)> const& status) {
	::SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
	auto path = [&](wchar_t const* name) { return folder + L"\\" + name; };
	auto install = [&](wchar_t const* package, std::string const& v, wchar_t const* what,
		std::vector<std::pair<std::string, std::wstring>> const& files) {
		std::vector<uint8_t> data;
		auto ok = Download(package, v, data, [&](size_t read, size_t total) {
			if (status)
				status(total ? std::format(L"Downloading the {} metadata: {} of {} KB...", what, read / 1024, total / 1024) :
					std::format(L"Downloading the {} metadata: {} KB...", what, read / 1024));
		});
		if (!ok)
			return false;
		if (status)
			status(std::format(L"Extracting the {} metadata...", what));
		return Extract(data, files);
	};
	if (!install(Win32Package, version, L"Win32", { { "Windows.Win32.winmd", path(L"Windows.Win32.winmd") },
		{ "sdk_license.txt", path(L"sdk_license.txt") } }))
		return false;
	if (!wdkVersion.empty() && !install(WdkPackage, wdkVersion, L"WDK", { { "Windows.Wdk.winmd", path(L"Windows.Wdk.winmd") },
		{ "sdk_license.txt", path(L"wdk_license.txt") } }))
		return false;
	return true;
}
