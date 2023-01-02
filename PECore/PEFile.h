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

	template<typename T=void>
	wil::unique_mapview_ptr<T> Map(uint32_t offset, uint32_t& size, uint32_t& bias) const {
		auto base = offset - (bias = offset % (64 << 10));
		auto mapSize = size + bias;
		if (base + mapSize > m_FileSize) {
			mapSize = 0;
			assert(m_FileSize > offset);
			size = m_FileSize - offset;
		}
		auto ptr = ::MapViewOfFile(m_hMap.get(), FILE_MAP_READ, 0, base, mapSize);
		return wil::unique_mapview_ptr<T>((T*)ptr);
	}
	bool Read(uint32_t offset, uint32_t size, void* buffer) const;
	
	libpe::Ilibpe* operator->() const;

	operator bool() const;

private:
	libpe::IlibpePtr m_pe{ libpe::Createlibpe() };
	std::wstring m_Path;
	wil::unique_handle m_hMap;
	uint32_t m_FileSize{ 0 };
};

