#include "pch.h"
#include "PEFile.h"
#include "libpe.h"

bool PEFile::Open(std::wstring_view path) {
	auto ok = m_pe->LoadPe(path.data()) == libpe::PEOK;
	if (ok) {
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
	return (uint32_t)m_pe->GetDataSize();
}

PEFile::operator bool() const {
	return !m_Path.empty();
}

bool PEFile::Read(uint32_t offset, uint32_t size, void* buffer) const {
	memcpy(buffer, (PBYTE)m_pe->GetBaseAddr() + offset, size);
	return true;
}

const BYTE* PEFile::GetData() const {
	return (const BYTE*)m_pe->GetBaseAddr();
}

std::span<const std::byte> PEFile::GetSpan(uint32_t offset, uint32_t size) const {
	return std::span((const std::byte*)GetData() + offset, size);
}

libpe::Ilibpe* PEFile::operator->() const {
	return m_pe.get();
}


