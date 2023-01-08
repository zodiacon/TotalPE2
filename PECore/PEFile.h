#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "libpe.h"
#include <cassert>

class PEFile {
public:
	bool Open(std::wstring_view path);
	void Close();

	std::wstring const& GetPath() const;
	uint32_t GetFileSize() const;

	bool Read(uint32_t offset, uint32_t size, void* buffer) const;
	template<typename T>
	T Read(uint32_t offset) const {
		T value;
		Read(offset, sizeof(T), &value);
		return value;
	}

	const BYTE* GetData() const;
	std::span<const std::byte> GetSpan(uint32_t offset, uint32_t size) const;

	libpe::Ilibpe* operator->() const;

	operator bool() const;

private:
	libpe::IlibpePtr m_pe{ libpe::Createlibpe() };
	std::wstring m_Path;
};

