#pragma once

class RecentFilesManager {
public:
	void AddFile(std::wstring const& file);
	std::vector<std::wstring> const& Files() const;
	bool IsEmpty() const;
	void Set(std::vector<std::wstring> files);
	void MaxFiles(size_t count);
	size_t MaxFiles() const;

private:
	size_t m_maxFiles{ 15 };
	std::vector<std::wstring> m_files;
};

