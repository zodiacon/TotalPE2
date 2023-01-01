#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "libpe.h"

class PEFile {
public:
	bool Open(std::wstring_view path);
	std::wstring const& GetPath() const;
	template<typename T=void>
	wil::unique_mapview_ptr<T> Map(uint32_t offset, uint32_t size) const {
		return wil::unique_mapview_ptr<T>((T*)::MapViewOfFile(m_hMap.get(), FILE_MAP_READ, 0, offset, size));
	}
	bool Read(uint32_t offset, uint32_t size, void* buffer) const;
	
	libpe::Ilibpe* operator->() const;

	operator bool() const;

private:
	libpe::IlibpePtr m_pe{ libpe::Createlibpe() };
	std::wstring m_Path;
	wil::unique_handle m_hMap;
};

