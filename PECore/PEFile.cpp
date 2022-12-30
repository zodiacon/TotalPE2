#include "pch.h"
#include "PEFile.h"
#include "libpe.h"

bool PEFile::Open(std::wstring_view path) {
	auto ok = m_pe->LoadPe(path.data()) == libpe::PEOK;
	if (ok) {
		wil::unique_hfile hFile(::CreateFile(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
		if (!hFile)
			return false;
		m_hMap.reset(::CreateFileMapping(hFile.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
		if (!m_hMap)
			return false;

		m_Path = path;
	}
	return ok;
}

std::wstring const& PEFile::GetPath() const {
	return m_Path;
}


bool PEFile::Read(uint32_t offset, uint32_t size, void* buffer) const {
	auto p = Map<>(offset, size);
	if (!p)
		return false;
	memcpy(buffer, p.get(), size);
	return true;
}

libpe::Ilibpe* PEFile::operator->() const {
	return m_pe.get();
}

