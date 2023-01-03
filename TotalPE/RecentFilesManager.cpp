#include "pch.h"
#include "RecentFilesManager.h"

void RecentFilesManager::AddFile(std::wstring const& file) {
	if (auto it = std::find(m_files.begin(), m_files.end(), file); it != m_files.end()) {
		m_files.erase(it);
	}
	m_files.insert(m_files.begin(), file);
	if (m_files.size() > m_maxFiles)
		m_files.resize(m_files.size() - 1);
}

std::vector<std::wstring> const& RecentFilesManager::Files() const {
	return m_files;
}

bool RecentFilesManager::IsEmpty() const {
	return m_files.empty();
}

void RecentFilesManager::Set(std::vector<std::wstring> files) {
	m_files = std::move(files);
}
