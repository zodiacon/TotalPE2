#include "pch.h"
#include "PEFile.h"
#include "libpe.h"

bool PEFile::Open(std::wstring_view path) {
	auto ok = m_pe->LoadPe(path.data()) == libpe::PEOK;
	if (ok) {
		wil::unique_hfile hFile(::CreateFile(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
		if (!hFile)
			return false;
		m_FileSize = ::GetFileSize(hFile.get(), nullptr);
		m_hMap.reset(::CreateFileMapping(hFile.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
		if (!m_hMap)
			return false;

		m_Path = path;
	}
	return ok;
}

void PEFile::Close() {
	m_pe->Clear();
	m_Path = L"";
}

std::wstring const& PEFile::GetPath() const {
	return m_Path;
}

uint32_t PEFile::GetFileSize() const {
	return m_FileSize;
}

PEFile::operator bool() const {
	return !m_Path.empty();
}

bool PEFile::Read(uint32_t offset, uint32_t size, void* buffer) const {
	uint32_t bias;
	auto p = Map<BYTE>(offset, size, bias);
	if (!p)
		return false;
	memcpy(buffer, p.get() + bias, size);
	return true;
}

libpe::Ilibpe* PEFile::operator->() const {
	return m_pe.get();
}


