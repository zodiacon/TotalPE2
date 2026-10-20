#pragma once

//
// The files opened last, the last first. They are TotalPE's, not a window's: kept in the registry (the RecentFiles
// setting) and read from it again whenever they are used, so that each window and each process adds to the same list
//
class RecentFilesManager {
public:
	// (the menu's commands are ID_FILE_MRU_FIRST..ID_FILE_MRU_LAST: 16)
	static constexpr size_t MaxFiles = 15;

	static std::vector<std::wstring> Files();
	// path: first (once: by its full path, whatever its case)
	static void AddFile(PCWSTR path);
	static void RemoveFile(std::wstring const& path);

	static std::wstring FullPath(PCWSTR path);
	static bool SamePath(std::wstring const& path1, std::wstring const& path2);

private:
	static void Store(std::vector<std::wstring> const& files);
};
