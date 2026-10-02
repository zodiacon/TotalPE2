#include "pch.h"
#include "PEAnomalies.h"
#include <cmath>
#include <ctime>
#include <cwctype>

namespace {
	struct Optional {
		uint32_t EntryPoint{}, SectionAlignment{}, FileAlignment{}, SizeOfImage{}, SizeOfHeaders{}, CheckSum{}, NumberOfDirectories{};
	};

	Optional GetOptional(PEFile const& pe) {
		auto nt = pe.GetNTHeader();
		Optional o;
		if (pe.GetFileInfo()->IsPE64) {
			auto& h = nt->NTHdr64.OptionalHeader;
			o = { h.AddressOfEntryPoint, h.SectionAlignment, h.FileAlignment, h.SizeOfImage, h.SizeOfHeaders, h.CheckSum, h.NumberOfRvaAndSizes };
		}
		else {
			auto& h = nt->NTHdr32.OptionalHeader;
			o = { h.AddressOfEntryPoint, h.SectionAlignment, h.FileAlignment, h.SizeOfImage, h.SizeOfHeaders, h.CheckSum, h.NumberOfRvaAndSizes };
		}
		return o;
	}

	uint64_t AlignUp(uint64_t value, uint64_t alignment) {
		return alignment ? (value + alignment - 1) / alignment * alignment : value;
	}

	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	// names that packers, protectors and installers give to their sections
	PCWSTR PackerForSection(std::string name) {
		static const std::unordered_map<std::string, PCWSTR> packers{
			{ "upx0", L"UPX" }, { "upx1", L"UPX" }, { "upx2", L"UPX" }, { "upx!", L"UPX" },
			{ ".aspack", L"ASPack" }, { ".adata", L"ASPack" }, { ".petite", L"Petite" },
			{ ".themida", L"Themida" }, { ".winlice", L"Themida" },
			{ ".vmp0", L"VMProtect" }, { ".vmp1", L"VMProtect" }, { ".vmp2", L"VMProtect" },
			{ ".enigma1", L"Enigma Protector" }, { ".enigma2", L"Enigma Protector" },
			{ ".mpress1", L"MPRESS" }, { ".mpress2", L"MPRESS" },
			{ ".nsp0", L"NsPack" }, { ".nsp1", L"NsPack" }, { ".nsp2", L"NsPack" },
			{ ".perplex", L"PE-Armor" }, { ".yp", L"Y0da Protector" }, { ".y0da", L"Y0da Protector" },
			{ "pec1", L"PECompact" }, { "pec2", L"PECompact" }, { "pecompact2", L"PECompact" },
			{ ".packed", L"a packer" }, { ".rmnet", L"RMNet" },
		};
		for (auto& c : name)
			c = (char)tolower((unsigned char)c);
		auto it = packers.find(name);
		return it == packers.end() ? nullptr : it->second;
	}

	uint32_t Rol(uint32_t value, unsigned bits) {
		bits &= 31;
		return bits ? (value << bits) | (value >> (32 - bits)) : value;
	}
}

PCWSTR AnomalySeverityToString(AnomalySeverity severity) {
	switch (severity) {
		case AnomalySeverity::Info: return L"Info";
		case AnomalySeverity::Warning: return L"Warning";
		case AnomalySeverity::Error: return L"Error";
	}
	return L"";
}

double ComputeEntropy(const uint8_t* data, size_t size) {
	if (size == 0)
		return 0;
	uint64_t counts[256]{};
	for (size_t i = 0; i < size; i++)
		counts[data[i]]++;
	double entropy = 0;
	for (auto c : counts) {
		if (c == 0)
			continue;
		double p = (double)c / (double)size;
		entropy -= p * std::log2(p);
	}
	return entropy;
}

uint32_t ComputePEChecksum(const uint8_t* data, size_t size, size_t checksumOffset) {
	uint64_t sum = 0;
	for (size_t i = 0; i < size; i += 2) {
		uint32_t word;
		if (i >= checksumOffset && i < checksumOffset + 4)
			word = 0;	// the checksum field itself counts as zero
		else
			word = data[i] | (i + 1 < size ? data[i + 1] << 8 : 0);
		sum += word;
		if (sum > 0xFFFFFFFFULL)
			sum = (sum & 0xFFFFFFFFULL) + (sum >> 32);
	}
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum += sum >> 16;
	sum &= 0xFFFF;
	return (uint32_t)(sum + size);
}

uint32_t ComputeRichKey(const uint8_t* data, PERichHeader const& rich) {
	uint32_t key = rich.Offset;
	for (uint32_t i = 0; i < rich.Offset; i++) {
		if (i >= 0x3C && i < 0x40)
			continue;	// e_lfanew is not part of the checksum
		key += Rol(data[i], i);
	}
	for (auto& e : rich.Entries)
		key += Rol(((uint32_t)e.ProductId << 16) | e.BuildId, e.Count);
	return key;
}

std::vector<Anomaly> FindAnomalies(PEFile const& pe) {
	std::vector<Anomaly> result;
	auto info = pe.GetFileInfo();
	auto nt = pe.GetNTHeader();
	auto dos = pe.GetMSDOSHeader();
	auto data = pe.GetData();
	if (!pe || !info || !nt || !data)
		return result;

	auto add = [&](AnomalySeverity severity, PCWSTR category, std::wstring message, int64_t offset = -1) {
		result.push_back({ severity, category, std::move(message), offset });
	};
	auto hex = [](uint64_t v) { return std::format(L"0x{:X}", v); };

	const uint64_t fileSize = pe.GetFileSize();
	const auto opt = GetOptional(pe);
	auto const& fh = nt->NTHdr32.FileHeader;
	const bool isDll = fh.Characteristics & IMAGE_FILE_DLL;
	const uint64_t lfanew = dos->e_lfanew;
	auto sections = pe.GetSecHeaders();
	auto dirs = pe.GetDataDirs();

	//
	// headers
	//
	if (lfanew < sizeof(IMAGE_DOS_HEADER))
		add(AnomalySeverity::Warning, L"Header", L"The PE header starts at " + hex(lfanew) + L", inside the DOS header", 0x3C);
	if (sections->empty())
		add(AnomalySeverity::Warning, L"Header", L"The file has no sections");
	if (fh.NumberOfSections > 96)
		add(AnomalySeverity::Warning, L"Header", std::format(L"The file claims {} sections; the Windows loader accepts at most 96", fh.NumberOfSections));

	const uint64_t optStart = lfanew + 4 + sizeof(IMAGE_FILE_HEADER);
	const uint64_t optFixed = pe.GetFileInfo()->IsPE64 ? sizeof(IMAGE_OPTIONAL_HEADER64) - sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_NUMBEROF_DIRECTORY_ENTRIES
		: sizeof(IMAGE_OPTIONAL_HEADER32) - sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
	if (opt.NumberOfDirectories != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
		add(AnomalySeverity::Info, L"Header", std::format(L"The optional header declares {} data directories (normally 16)", opt.NumberOfDirectories));
	if (fh.SizeOfOptionalHeader != optFixed + (uint64_t)opt.NumberOfDirectories * sizeof(IMAGE_DATA_DIRECTORY))
		add(AnomalySeverity::Info, L"Header", std::format(L"SizeOfOptionalHeader ({}) does not match the number of data directories", fh.SizeOfOptionalHeader));

	const uint64_t headersEnd = optStart + fh.SizeOfOptionalHeader + (uint64_t)fh.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
	if (opt.SizeOfHeaders < headersEnd)
		add(AnomalySeverity::Error, L"Header", std::format(L"SizeOfHeaders ({}) is smaller than the headers ({})", hex(opt.SizeOfHeaders), hex(headersEnd)));

	//
	// checksum and time stamp
	//
	const size_t checksumOffset = (size_t)optStart + 64;	// CheckSum is at the same place in PE32 and PE32+
	if (opt.CheckSum != 0 && checksumOffset + 4 <= fileSize) {
		auto computed = ComputePEChecksum(data, (size_t)fileSize, checksumOffset);
		if (computed != opt.CheckSum)
			add(AnomalySeverity::Warning, L"Checksum", std::format(L"The checksum in the header is {} but the file's checksum is {}; the file was modified after it was linked",
				hex(opt.CheckSum), hex(computed)), (int64_t)checksumOffset);
	}

	bool reproducible = false;
	for (auto& d : *pe.GetDebug())
		if (d.Directory.Type == 16)	// IMAGE_DEBUG_TYPE_REPRO: the time stamp is a hash, not a time
			reproducible = true;
	auto stamp = fh.TimeDateStamp;
	if (!reproducible && stamp != 0) {
		if ((int64_t)stamp > (int64_t)time(nullptr) + 24 * 3600)
			add(AnomalySeverity::Warning, L"Time stamp", L"The link time stamp " + hex(stamp) + L" is in the future", (int64_t)optStart - 16);
		else if (stamp < 0x2A000000)	// before 1995
			add(AnomalySeverity::Info, L"Time stamp", L"The link time stamp " + hex(stamp) + L" is earlier than 1995", (int64_t)optStart - 16);
	}

	//
	// sections
	//
	struct Range { uint64_t Start, End; std::wstring Name; };
	std::vector<Range> virtualRanges, rawRanges;
	uint64_t imageEnd = 0, rawEnd = 0;
	uint32_t previousVa = 0;
	size_t index = 0;
	for (auto& s : *sections) {
		auto& h = s.SecHdr;
		auto name = Widen(s.SectionName);
		if (name.empty())
			name = L"(no name)";
		std::wstring label = L"Section " + name;
		int64_t headerOffset = s.Offset ? s.Offset : (int64_t)(optStart + fh.SizeOfOptionalHeader + index * sizeof(IMAGE_SECTION_HEADER));
		const bool executable = h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE);
		const uint64_t virtualSize = std::max<uint64_t>(h.Misc.VirtualSize, h.SizeOfRawData);

		if ((h.Characteristics & IMAGE_SCN_MEM_EXECUTE) && (h.Characteristics & IMAGE_SCN_MEM_WRITE))
			add(AnomalySeverity::Warning, L"Section", label + L" is both writable and executable", headerOffset);
		if (auto packer = PackerForSection(s.SectionName))
			add(AnomalySeverity::Warning, L"Section", label + L" has a name used by " + packer, headerOffset);
		if (s.SectionName.empty() || !std::all_of(s.SectionName.begin(), s.SectionName.end(), [](unsigned char c) { return c >= 0x20 && c < 0x7F; }))
			add(AnomalySeverity::Info, L"Section", label + L" has an empty or unprintable name", headerOffset);

		if (h.SizeOfRawData && (uint64_t)h.PointerToRawData + h.SizeOfRawData > fileSize)
			add(AnomalySeverity::Error, L"Section", label + L" has raw data beyond the end of the file (" + hex((uint64_t)h.PointerToRawData + h.SizeOfRawData) + L" > " + hex(fileSize) + L")", headerOffset);
		if (executable && h.SizeOfRawData == 0 && h.Misc.VirtualSize)
			add(AnomalySeverity::Warning, L"Section", label + L" is executable but has no data in the file; code is created at run time", headerOffset);

		if (opt.SectionAlignment && h.VirtualAddress % opt.SectionAlignment)
			add(AnomalySeverity::Warning, L"Section", label + L" is at RVA " + hex(h.VirtualAddress) + L", which is not a multiple of the section alignment " + hex(opt.SectionAlignment), headerOffset);
		if (opt.FileAlignment >= 0x200 && h.PointerToRawData % opt.FileAlignment)
			add(AnomalySeverity::Warning, L"Section", label + L" has raw data at " + hex(h.PointerToRawData) + L", which is not a multiple of the file alignment " + hex(opt.FileAlignment), headerOffset);
		if (index > 0 && h.VirtualAddress < previousVa)
			add(AnomalySeverity::Warning, L"Section", label + L" is not in ascending RVA order", headerOffset);
		previousVa = h.VirtualAddress;

		virtualRanges.push_back({ h.VirtualAddress, (uint64_t)h.VirtualAddress + virtualSize, name });
		imageEnd = std::max<uint64_t>(imageEnd, (uint64_t)h.VirtualAddress + virtualSize);
		if (h.SizeOfRawData) {
			rawRanges.push_back({ h.PointerToRawData, (uint64_t)h.PointerToRawData + h.SizeOfRawData, name });
			rawEnd = std::max<uint64_t>(rawEnd, (uint64_t)h.PointerToRawData + h.SizeOfRawData);
		}

		// entropy: compressed or encrypted data looks like noise
		if (h.SizeOfRawData >= 512 && (uint64_t)h.PointerToRawData + h.SizeOfRawData <= fileSize) {
			auto entropy = ComputeEntropy(data + h.PointerToRawData, h.SizeOfRawData);
			if (executable && entropy >= 7.0)
				add(AnomalySeverity::Warning, L"Entropy", std::format(L"{} is executable and has an entropy of {:.2f} out of 8; the code is probably packed or encrypted", Widen(s.SectionName), entropy), h.PointerToRawData);
			else if (!executable && entropy >= 7.4)
				add(AnomalySeverity::Info, L"Entropy", std::format(L"{} has an entropy of {:.2f} out of 8; it holds compressed or encrypted data", Widen(s.SectionName), entropy), h.PointerToRawData);
		}
		index++;
	}

	auto reportOverlaps = [&](std::vector<Range> ranges, PCWSTR what) {
		std::sort(ranges.begin(), ranges.end(), [](auto& a, auto& b) { return a.Start < b.Start; });
		for (size_t i = 1; i < ranges.size(); i++)
			if (ranges[i].Start < ranges[i - 1].End)
				add(AnomalySeverity::Warning, L"Section", std::format(L"The {} of sections {} and {} overlap", what, ranges[i - 1].Name, ranges[i].Name));
	};
	reportOverlaps(virtualRanges, L"memory ranges");
	reportOverlaps(rawRanges, L"file ranges");

	if (!sections->empty() && opt.SizeOfImage != AlignUp(imageEnd, opt.SectionAlignment))
		add(AnomalySeverity::Warning, L"Header", std::format(L"SizeOfImage is {} but the sections end at {} (aligned: {})", hex(opt.SizeOfImage), hex(imageEnd), hex(AlignUp(imageEnd, opt.SectionAlignment))));

	//
	// entry point
	//
	if (opt.EntryPoint == 0) {
		if (!isDll && !(fh.Characteristics & IMAGE_FILE_SYSTEM))
			add(AnomalySeverity::Warning, L"Entry point", L"The executable has no entry point");
	}
	else {
		PESectionHeader const* host = nullptr;
		for (auto& s : *sections) {
			auto& h = s.SecHdr;
			if (opt.EntryPoint >= h.VirtualAddress && opt.EntryPoint < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData)) {
				host = &s;
				break;
			}
		}
		if (!host) {
			if (opt.EntryPoint < opt.SizeOfHeaders)
				add(AnomalySeverity::Warning, L"Entry point", L"The entry point " + hex(opt.EntryPoint) + L" is inside the headers");
			else
				add(AnomalySeverity::Error, L"Entry point", L"The entry point " + hex(opt.EntryPoint) + L" is outside every section");
		}
		else {
			auto name = Widen(host->SectionName);
			if (!(host->SecHdr.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)))
				add(AnomalySeverity::Warning, L"Entry point", L"The entry point " + hex(opt.EntryPoint) + L" is in section " + name + L", which is not executable");
			else if (sections->size() > 1 && host == &sections->back())
				add(AnomalySeverity::Info, L"Entry point", L"The entry point is in the last section (" + name + L"), a pattern of packers and file infectors");
		}
	}

	//
	// data directories
	//
	if (dirs) {
		for (int i = 0; i < (int)dirs->size() && i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; i++) {
			auto& d = (*dirs)[i].DataDir;
			if (d.VirtualAddress == 0 && d.Size == 0)
				continue;
			static const PCWSTR names[] = { L"Export", L"Import", L"Resource", L"Exception", L"Security", L"Base relocation", L"Debug", L"Architecture",
				L"Global pointer", L"TLS", L"Load config", L"Bound import", L"IAT", L"Delay import", L"CLR" };
			auto name = i < (int)_countof(names) ? names[i] : L"Reserved";
			if (i == IMAGE_DIRECTORY_ENTRY_SECURITY) {
				// the certificate table is addressed by file offset
				if ((uint64_t)d.VirtualAddress + d.Size > fileSize)
					add(AnomalySeverity::Error, L"Directory", L"The security directory extends beyond the end of the file", d.VirtualAddress);
			}
			else if (d.VirtualAddress && (uint64_t)d.VirtualAddress + d.Size > opt.SizeOfImage && i != IMAGE_DIRECTORY_ENTRY_GLOBALPTR)
				add(AnomalySeverity::Warning, L"Directory", std::wstring(name) + L" directory (" + hex(d.VirtualAddress) + L", size " + hex(d.Size) + L") extends beyond the image");
		}
	}

	//
	// overlay (data after the last section), not counting the certificate table
	//
	if (rawEnd < fileSize) {
		uint64_t overlayStart = rawEnd, overlayEnd = fileSize;
		if (dirs && dirs->size() > IMAGE_DIRECTORY_ENTRY_SECURITY) {
			auto& cert = (*dirs)[IMAGE_DIRECTORY_ENTRY_SECURITY].DataDir;
			if (cert.Size && cert.VirtualAddress >= overlayStart && (uint64_t)cert.VirtualAddress + cert.Size <= fileSize) {
				if (cert.VirtualAddress == overlayStart)
					overlayStart = (uint64_t)cert.VirtualAddress + cert.Size;
				else if ((uint64_t)cert.VirtualAddress + cert.Size == overlayEnd)
					overlayEnd = cert.VirtualAddress;
			}
		}
		if (overlayEnd > overlayStart) {
			auto size = overlayEnd - overlayStart;
			add(AnomalySeverity::Info, L"Overlay", std::format(L"{} bytes of data follow the last section, starting at {}", size, hex(overlayStart)), (int64_t)overlayStart);
			if (size >= 4096 && ComputeEntropy(data + overlayStart, (size_t)size) >= 7.5)
				add(AnomalySeverity::Warning, L"Overlay", L"The overlay has the entropy of compressed or encrypted data", (int64_t)overlayStart);
		}
	}

	//
	// imports
	//
	{
		size_t functions = 0;
		bool loader = false;
		for (auto& mod : *pe.GetImport())
			for (auto& fn : mod.ImportFunc) {
				functions++;
				if (fn.FuncName == "GetProcAddress" || fn.FuncName.starts_with("LoadLibrary") || fn.FuncName == "LdrGetProcedureAddress")
					loader = true;
			}
		if (functions == 0 && !isDll && !info->HasCOMDescr)
			add(AnomalySeverity::Info, L"Imports", L"The executable imports nothing");
		else if (loader && functions <= 6)
			add(AnomalySeverity::Warning, L"Imports", std::format(L"Only {} functions are imported and they include LoadLibrary or GetProcAddress; packed files resolve the rest at run time", functions));
	}

	//
	// TLS callbacks run before the entry point
	//
	if (auto tls = pe.GetTLS(); tls && !tls->TLSCallbacks.empty())
		add(AnomalySeverity::Info, L"TLS", std::format(L"The file has {} TLS callback(s), which run before the entry point", tls->TLSCallbacks.size()));

	//
	// Rich header
	//
	if (auto rich = pe.GetRichHeader(); rich && rich->Offset >= sizeof(IMAGE_DOS_HEADER) && rich->Offset <= fileSize) {
		if (ComputeRichKey(data, *rich) != rich->Key)
			add(AnomalySeverity::Warning, L"Rich header", L"The Rich header does not match its checksum; the DOS header or the Rich header was modified", rich->Offset);
	}

	// most severe first, keeping the order in which the checks ran within a severity
	std::stable_sort(result.begin(), result.end(), [](auto& a, auto& b) { return a.Severity > b.Severity; });
	return result;
}
