#include "pch.h"
#include "RecentFilesManager.h"
#include "AppSettings.h"

namespace {
	// (TotalPE's settings: see wWinMain)
	PCWSTR const SettingsKey = L"SOFTWARE\\ScorpioSoftware\\TotalPE";
	PCWSTR const ValueName = L"RecentFiles";
}

std::vector<std::wstring> RecentFilesManager::Files() {
	std::vector<std::wstring> files;
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, SettingsKey, KEY_READ) != ERROR_SUCCESS)
		return files;
	ULONG chars = 0;
	if (key.QueryMultiStringValue(ValueName, nullptr, &chars) != ERROR_SUCCESS || chars == 0)
		return files;
	std::wstring text(chars, L'\0');
	if (key.QueryMultiStringValue(ValueName, text.data(), &chars) != ERROR_SUCCESS)
		return files;

	for (auto p = text.c_str(); *p && files.size() < MaxFiles; p += wcslen(p) + 1)
		if (std::ranges::none_of(files, [&](auto const& file) { return SamePath(file, p); }))
			files.emplace_back(p);
	return files;
}

void RecentFilesManager::AddFile(PCWSTR path) {
	auto full = FullPath(path);
	auto files = Files();
	std::erase_if(files, [&](auto const& file) { return SamePath(file, full); });
	files.insert(files.begin(), std::move(full));
	if (files.size() > MaxFiles)
		files.resize(MaxFiles);
	Store(files);
}

void RecentFilesManager::RemoveFile(std::wstring const& path) {
	auto files = Files();
	if (std::erase_if(files, [&](auto const& file) { return SamePath(file, path); }))
		Store(files);
}

std::wstring RecentFilesManager::FullPath(PCWSTR path) {
	auto chars = ::GetFullPathName(path, 0, nullptr, nullptr);
	if (chars == 0)
		return path;
	std::wstring full(chars, L'\0');
	full.resize(::GetFullPathName(path, chars, full.data(), nullptr));
	return full;
}

bool RecentFilesManager::SamePath(std::wstring const& path1, std::wstring const& path2) {
	return ::CompareStringOrdinal(path1.c_str(), (int)path1.size(), path2.c_str(), (int)path2.size(), TRUE) == CSTR_EQUAL;
}

void RecentFilesManager::Store(std::vector<std::wstring> const& files) {
	// (also the setting, which is saved as TotalPE exits)
	AppSettings::Get().RecentFiles(files);

	std::wstring text;
	for (auto const& file : files)
		text += file + L'\0';
	text += L'\0';
	CRegKey key;
	if (key.Create(HKEY_CURRENT_USER, SettingsKey, nullptr, 0, KEY_WRITE) == ERROR_SUCCESS)
		key.SetMultiStringValue(ValueName, text.c_str());
}
