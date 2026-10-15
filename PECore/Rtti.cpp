#include "pch.h"
#include "Rtti.h"
#include "PEFile.h"
#include <DbgHelp.h>
#include <algorithm>
#include <map>
#include <unordered_map>

#pragma comment(lib, "dbghelp.lib")

std::string RttiTypeName(std::string const& decorated) {
	if (decorated.size() < 5 || decorated[0] != '.' || decorated[1] != '?' || decorated[2] != 'A')
		return {};
	// (UNDNAME_NO_ARGUMENTS | UNDNAME_32_BIT_DECODE: a type, without "class ")
	char name[1024];
	if (!::UnDecorateSymbolName(decorated.c_str() + 1, name, sizeof(name), 0x2800) || !name[0] || name[0] == '?')
		return {};
	std::string text = name;
	for (auto prefix : { "class ", "struct ", "union " })
		if (text.starts_with(prefix))
			text = text.substr(strlen(prefix));
	return text;
}

namespace {
	struct Image {
		PEFile const& Pe;
		uint64_t Base;
		bool Pe64;

		template<typename T>
		bool Read(uint32_t rva, T& value) const {
			auto offset = Pe.GetOffsetFromRVA(rva);
			if (!offset || offset + sizeof(T) > Pe.GetFileSize())
				return false;
			auto span = Pe.GetSpan((uint32_t)offset, sizeof(T));
			if (span.size() != sizeof(T))
				return false;
			memcpy(&value, span.data(), sizeof(T));
			return true;
		}
		std::string String(uint32_t rva, size_t max = 1024) const {
			std::string text;
			char ch;
			while (text.size() < max && Read(rva++, ch) && ch)
				text += ch;
			return text;
		}
		// of x86: a VA to an RVA (0 if it is not in the image)
		uint32_t RvaOf(uint64_t va) const {
			return va >= Base && va - Base < 0x100000000ULL ? (uint32_t)(va - Base) : 0;
		}
		bool InCode(uint32_t rva) const {
			for (auto const& s : *Pe.GetSecHeaders()) {
				auto const& h = s.SecHdr;
				if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData))
					return (h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) != 0;
			}
			return false;
		}
	};

	// x64 and ARM64: { signature 1, offset, cdOffset, type descriptor, class hierarchy descriptor, itself } (RVAs)
	// x86: { signature 0, offset, cdOffset, type descriptor, class hierarchy descriptor } (VAs)
	struct Locator {
		uint32_t Offset, TypeDescriptor, Hierarchy;
	};
}

std::vector<RttiClass> ReadRtti(PEFile const& pe) {
	std::vector<RttiClass> classes;
	auto sections = pe.GetSecHeaders();
	if (!sections)
		return classes;
	Image image{ pe, pe.GetImageBase(), pe.GetFileInfo()->IsPE64 };
	uint32_t pointer = image.Pe64 ? 8 : 4;
	auto pointerAt = [&](uint32_t rva, uint64_t& value) {
		if (image.Pe64)
			return image.Read(rva, value);
		uint32_t v32;
		if (!image.Read(rva, v32))
			return false;
		value = v32;
		return true;
	};
	// a pointer (an RVA in x64's RTTI, a VA in x86's) as an RVA
	auto target = [&](uint32_t value) { return image.Pe64 ? value : image.RvaOf(value); };

	// the type descriptors (by RVA): their names
	std::unordered_map<uint32_t, std::string> names;
	auto typeName = [&](uint32_t rva) -> std::string const* {
		auto it = names.find(rva);
		if (it == names.end()) {
			// the vtable of type_info, a spare pointer, then the name
			auto name = image.String(rva + 2 * pointer, 512);
			if (!name.starts_with(".?A"))
				name.clear();
			it = names.emplace(rva, std::move(name)).first;
		}
		return it->second.empty() ? nullptr : &it->second;
	};

	// the complete object locators: in the data (not in code), aligned
	std::map<uint32_t, Locator> locators;
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if ((h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto size = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		auto offset = pe.GetOffsetFromRVA(h.VirtualAddress);
		if (!offset || offset + size > pe.GetFileSize())
			continue;
		auto data = pe.GetSpan((uint32_t)offset, size);
		auto words = (uint32_t const*)data.data();
		size_t count = data.size() / 4;
		for (size_t i = 0; i + 5 <= count; i++) {
			auto rva = h.VirtualAddress + (uint32_t)i * 4;
			if (image.Pe64) {
				if (words[i] != 1 || i + 6 > count || words[i + 5] != rva)
					continue;
			}
			else if (words[i] != 0)
				continue;
			auto descriptor = target(words[i + 3]), hierarchy = target(words[i + 4]);
			if (!descriptor || !hierarchy || !typeName(descriptor))
				continue;
			locators[rva] = { words[i + 1], descriptor, hierarchy };
		}
	}
	if (locators.empty())
		return classes;

	// the vtables: a pointer to a locator, then pointers to code
	std::map<uint32_t, RttiClass> byDescriptor;
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if ((h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto size = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		for (uint32_t at = 0; at + pointer <= size; at += pointer) {
			uint64_t value;
			if (!pointerAt(h.VirtualAddress + at, value) || value < image.Base)
				continue;
			auto locator = locators.find(image.RvaOf(value));
			if (locator == locators.end())
				continue;
			RttiClass::Vtable vtable{ h.VirtualAddress + at + pointer, locator->second.Offset, locator->first };
			for (uint32_t slot = vtable.Rva; slot + pointer <= h.VirtualAddress + size; slot += pointer) {
				uint64_t function;
				if (!pointerAt(slot, function))
					break;
				auto rva = image.RvaOf(function);
				if (!rva || !image.InCode(rva))
					break;
				vtable.Functions.push_back(rva);
			}
			if (vtable.Functions.empty())
				continue;
			auto& c = byDescriptor[locator->second.TypeDescriptor];
			c.Vtables.push_back(std::move(vtable));
		}
	}

	// the classes: names, and bases from the hierarchy (its first base is the class itself)
	for (auto& [descriptor, c] : byDescriptor) {
		c.TypeDescriptor = descriptor;
		c.Decorated = *typeName(descriptor);
		c.Name = RttiTypeName(c.Decorated);
		if (c.Name.empty())
			c.Name = c.Decorated;
		c.Struct = c.Decorated.starts_with(".?AU");
		std::sort(c.Vtables.begin(), c.Vtables.end(), [](auto const& a, auto const& b) { return a.Offset < b.Offset; });
		auto hierarchy = locators[c.Vtables.front().Locator].Hierarchy;
		// { signature, attributes, count, the array of base class descriptors }
		uint32_t count = 0, array = 0;
		if (image.Read(hierarchy + 8, count) && image.Read(hierarchy + 12, array) && count < 256) {
			array = target(array);
			for (uint32_t i = 1; i < count; i++) {
				uint32_t base = 0, baseDescriptor = 0;
				if (!image.Read(array + i * 4, base) || !image.Read(target(base), baseDescriptor))
					break;
				if (auto name = typeName(target(baseDescriptor))) {
					auto text = RttiTypeName(*name);
					c.Bases.push_back(text.empty() ? *name : text);
				}
			}
		}
		classes.push_back(std::move(c));
	}
	std::sort(classes.begin(), classes.end(), [](auto const& a, auto const& b) { return a.Name < b.Name; });
	return classes;
}
