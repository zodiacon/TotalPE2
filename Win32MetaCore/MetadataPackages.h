#pragma once

#include <functional>
#include <string>
#include <vector>

//
// The metadata's NuGet packages on nuget.org (Microsoft.Windows.SDK.Win32Metadata, Microsoft.Windows.WDK.Win32Metadata): the
// versions there are, and a version's files (Windows.Win32.winmd, Windows.Wdk.winmd and the license, under the Windows
// SDK's) put in a folder. Blocking: for a thread of its own.
//
namespace MetadataPackages {
	inline constexpr wchar_t Win32Package[] = L"microsoft.windows.sdk.win32metadata";
	inline constexpr wchar_t WdkPackage[] = L"microsoft.windows.wdk.win32metadata";

	// the versions of a package, oldest first (the last: the newest); empty if they cannot be read
	std::vector<std::string> Versions(std::wstring const& package);
	// a version of a package (its .nupkg); progress: bytes read so far, and how many there are (0: not known)
	bool Download(std::wstring const& package, std::string const& version, std::vector<uint8_t>& data,
		std::function<void(size_t, size_t)> const& progress = {});
	// files of a zip (a .nupkg is one): each, by its name in it, to a path; false if one is not in it, or cannot be written
	bool Extract(std::vector<uint8_t> const& zip, std::vector<std::pair<std::string, std::wstring>> const& files);
	// The metadata of a version into a folder (made if it is not there): Windows.Win32.winmd, sdk_license.txt; and the
	// WDK's, if its version is given: Windows.Wdk.winmd, wdk_license.txt. What it does now, to status.
	bool Install(std::string const& version, std::string const& wdkVersion, std::wstring const& folder,
		std::function<void(std::wstring const&)> const& status = {});
}
