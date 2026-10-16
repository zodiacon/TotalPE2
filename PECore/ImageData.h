#pragma once

#include <PEFile.h>
#include <algorithm>
#include <cstring>

//
// The sections of an image as its tables of data are looked for in them: what an RVA is in (code, data, writable data),
// reading at an RVA, the RVA of a pointer's VA
//
struct ImageData {
	PEFile const& Pe;
	uint64_t Base;
	bool Pe64;
	uint32_t Pointer;

	explicit ImageData(PEFile const& pe) : Pe(pe), Base(pe.GetImageBase()), Pe64(pe.GetFileInfo()->IsPE64), Pointer(Pe64 ? 8 : 4) {}

	IMAGE_SECTION_HEADER const* SectionOf(DWORD rva) const {
		for (auto const& s : *Pe.GetSecHeaders()) {
			auto const& h = s.SecHdr;
			if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData))
				return &h;
		}
		return nullptr;
	}
	bool InCode(DWORD rva) const {
		auto h = SectionOf(rva);
		return h && (h->Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE));
	}
	bool Writable(DWORD rva) const {
		auto h = SectionOf(rva);
		return h && (h->Characteristics & IMAGE_SCN_MEM_WRITE);
	}
	bool InData(DWORD rva) const {
		auto h = SectionOf(rva);
		return h && !(h->Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) && (h->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA);
	}
	bool Read(DWORD rva, void* buffer, size_t size) const {
		auto offset = Pe.GetOffsetFromRVA(rva);
		if (!offset || offset + size > Pe.GetFileSize())
			return false;
		auto span = Pe.GetSpan((uint32_t)offset, (uint32_t)size);
		if (span.size() != size)
			return false;
		memcpy(buffer, span.data(), size);
		return true;
	}
	// an RVA of a VA in the image (0 if it is not)
	DWORD RvaOf(uint64_t va) const {
		return va > Base && va - Base < 0x80000000ULL ? (DWORD)(va - Base) : 0;
	}
};
