#include "pch.h"
#include "PERegions.h"

namespace {
	PCWSTR DirectoryName(int index) {
		static const PCWSTR names[] = {
			L"Export", L"Import", L"Resource", L"Exception", L"Security", L"Base Relocation",
			L"Debug", L"Architecture", L"Global Pointer", L"Thread Local Storage", L"Load Config",
			L"Bound Import", L"IAT", L"Delay Import", L"COM Descriptor (CLR)",
		};
		return index >= 0 && index < (int)_countof(names) ? names[index] : L"Reserved";
	}
}

bool FileOffsetToRva(PEFile const& pe, uint64_t offset, DWORD& rva) {
	auto sections = pe.GetSecHeaders();
	if (sections) {
		for (auto& s : *sections) {
			auto& h = s.SecHdr;
			if (h.SizeOfRawData && offset >= h.PointerToRawData && offset < (uint64_t)h.PointerToRawData + h.SizeOfRawData) {
				rva = (DWORD)(h.VirtualAddress + (offset - h.PointerToRawData));
				return true;
			}
		}
	}
	auto nt = pe.GetNTHeader();
	if (!nt)
		return false;
	auto sizeOfHeaders = pe.GetFileInfo()->IsPE64 ? nt->NTHdr64.OptionalHeader.SizeOfHeaders : nt->NTHdr32.OptionalHeader.SizeOfHeaders;
	if (offset < sizeOfHeaders) {
		rva = (DWORD)offset;	// headers are mapped at the image base
		return true;
	}
	return false;
}

std::vector<HexRegion> BuildPERegions(PEFile const& pe) {
	std::vector<HexRegion> regions;
	auto fileSize = (uint64_t)pe.GetFileSize();
	auto dos = pe.GetMSDOSHeader();
	auto nt = pe.GetNTHeader();
	auto info = pe.GetFileInfo();
	if (!dos || !nt || !info || fileSize == 0)
		return regions;

	auto add = [&](uint64_t offset, uint64_t length, std::wstring name, int color) {
		if (length == 0 || offset >= fileSize)
			return;
		length = std::min(length, fileSize - offset);
		regions.push_back({ (int64_t)offset, (int64_t)length, std::move(name), color });
	};

	add(0, sizeof(IMAGE_DOS_HEADER), L"DOS Header", 0);

	uint64_t lfanew = dos->e_lfanew;
	uint64_t stubEnd = lfanew;
	if (auto rich = pe.GetRichHeader(); rich && !rich->Entries.empty() && rich->Offset >= sizeof(IMAGE_DOS_HEADER)) {
		// "DanS" + 3 padding dwords, the entries, then "Rich" + key
		add(rich->Offset, 16 + rich->Entries.size() * 8 + 8, L"Rich Header", 2);
		stubEnd = std::min<uint64_t>(stubEnd, rich->Offset);
	}
	if (stubEnd > sizeof(IMAGE_DOS_HEADER))
		add(sizeof(IMAGE_DOS_HEADER), stubEnd - sizeof(IMAGE_DOS_HEADER), L"DOS Stub", 1);

	auto is64 = info->IsPE64;
	auto const& fh = nt->NTHdr32.FileHeader;
	auto const optSize = (uint64_t)fh.SizeOfOptionalHeader;
	auto const optFixed = is64 ? sizeof(IMAGE_OPTIONAL_HEADER64) - sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_NUMBEROF_DIRECTORY_ENTRIES
		: sizeof(IMAGE_OPTIONAL_HEADER32) - sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
	auto numDirs = is64 ? nt->NTHdr64.OptionalHeader.NumberOfRvaAndSizes : nt->NTHdr32.OptionalHeader.NumberOfRvaAndSizes;

	add(lfanew, 4, L"PE Signature", 3);
	add(lfanew + 4, sizeof(IMAGE_FILE_HEADER), L"File Header", 4);
	uint64_t optStart = lfanew + 4 + sizeof(IMAGE_FILE_HEADER);
	add(optStart, std::min<uint64_t>(optFixed, optSize), L"Optional Header", 5);
	if (optSize > optFixed)
		add(optStart + optFixed, std::min<uint64_t>(optSize - optFixed, std::min<uint32_t>(numDirs, IMAGE_NUMBEROF_DIRECTORY_ENTRIES) * 8ULL), L"Data Directories", 6);
	add(optStart + optSize, (uint64_t)fh.NumberOfSections * sizeof(IMAGE_SECTION_HEADER), L"Section Headers", 7);

	// the overlay starts after the headers and the last section's raw data
	uint64_t dataEnd = is64 ? nt->NTHdr64.OptionalHeader.SizeOfHeaders : nt->NTHdr32.OptionalHeader.SizeOfHeaders;
	auto sections = pe.GetSecHeaders();
	if (sections) {
		int i = 0;
		for (auto& s : *sections) {
			auto& h = s.SecHdr;
			if (h.SizeOfRawData) {
				add(h.PointerToRawData, h.SizeOfRawData, L"Section " + std::wstring(s.SectionName.begin(), s.SectionName.end()), 8 + i % 3);
				dataEnd = std::max<uint64_t>(dataEnd, (uint64_t)h.PointerToRawData + h.SizeOfRawData);
			}
			i++;
		}
	}

	// the raw end of the section that contains an offset: a directory never extends into the next section
	auto sectionEnd = [&](uint64_t offset) -> uint64_t {
		if (sections)
			for (auto& s : *sections) {
				auto& h = s.SecHdr;
				if (offset >= h.PointerToRawData && offset < (uint64_t)h.PointerToRawData + h.SizeOfRawData)
					return (uint64_t)h.PointerToRawData + h.SizeOfRawData;
			}
		return fileSize;
	};

	auto dirs = pe.GetDataDirs();
	if (dirs) {
		for (int i = 0; i < (int)dirs->size() && i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; i++) {
			auto& dd = (*dirs)[i].DataDir;
			if (dd.Size == 0 || dd.VirtualAddress == 0)
				continue;
			uint64_t offset;
			if (i == IMAGE_DIRECTORY_ENTRY_SECURITY) {
				// the certificate table address is a file offset
				add(dd.VirtualAddress, dd.Size, L"Security (Certificate Table)", 0);
				continue;
			}
			offset = pe.GetOffsetFromRVA(dd.VirtualAddress);
			if (offset == 0 || offset >= fileSize) {
				if (i != IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT)
					continue;
				offset = dd.VirtualAddress;	// bound imports are not mapped
			}
			auto end = std::min<uint64_t>(offset + dd.Size, sectionEnd(offset));
			if (end > offset)
				add(offset, end - offset, std::wstring(DirectoryName(i)) + L" Directory", 11);
		}
	}

	if (dataEnd < fileSize)
		add(dataEnd, fileSize - dataEnd, L"Overlay", 1);
	return regions;
}
