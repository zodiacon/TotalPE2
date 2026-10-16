#include "pch.h"
#include "FrameworkBinding.h"
#include "PEFile.h"
#include <algorithm>
#include <cwchar>
#include <vector>

FrameworkBinding FindFrameworkBinding(PEFile const& pe, FrameworkImageReader const& read) {
	// WDF_BIND_INFO: Size, Component ("KmdfLibrary"), Version (Major, Minor, Build), FuncCount, FuncTable, Module
	FrameworkBinding binding;
	auto sections = pe.GetSecHeaders();
	if (!sections || !read)
		return binding;
	bool pe64 = pe.GetFileInfo()->IsPE64;
	auto base = pe.GetImageBase();
	uint32_t pointer = pe64 ? 8 : 4, size = pe64 ? 0x30 : 0x20;
	static wchar_t const name[] = L"KmdfLibrary";
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if ((h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto length = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		for (uint32_t at = 0; at + size <= length; at += pointer) {
			auto va = base + h.VirtualAddress + at;
			// (newer versions have more after these: its size is more)
			std::vector<uint8_t> bytes(size);
			if (!read(va, bytes.data(), size) || *(uint32_t const*)bytes.data() < size || *(uint32_t const*)bytes.data() > 0x100 ||
				*(uint32_t const*)bytes.data() % 4)
				continue;
			uint64_t component = 0, table = 0;
			memcpy(&component, bytes.data() + pointer, pointer);
			wchar_t text[_countof(name)]{};
			if (component <= base || !read(component, (uint8_t*)text, sizeof(text)) || wmemcmp(text, name, _countof(name)) != 0)
				continue;
			auto version = (uint32_t const*)(bytes.data() + 2 * pointer);
			auto count = version[3];
			memcpy(&table, bytes.data() + 2 * pointer + 16, pointer);
			uint8_t probe;
			if (version[0] != 1 || count == 0 || count > 4096 || table <= base || !read(table, &probe, 1))
				continue;
			binding.Major = version[0], binding.Minor = version[1], binding.Build = version[2];
			binding.Count = count;
			binding.Table = table;
			return binding;
		}
	}
	return binding;
}
