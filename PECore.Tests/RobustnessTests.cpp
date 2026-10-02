#include "TestCommon.h"

// A PE viewer is routinely pointed at damaged or hostile files. None of these tests assert on what Open returns
// for a damaged file - only that opening it, and then using everything the parser produced, does not crash.

namespace {
	// reads everything a view would read
	size_t TouchEverything(PEFile const& pe) {
		size_t sum = pe.GetFileSize();
		sum += pe.GetSecHeaders()->size() + pe.GetDataDirs()->size();
		for (auto& mod : *pe.GetImport()) {
			sum += mod.ModuleName.size();
			for (auto& fn : mod.ImportFunc)
				sum += fn.FuncName.size();
		}
		if (auto exp = pe.GetExport())
			for (auto& f : exp->Funcs)
				sum += f.FuncName.size() + f.ForwarderName.size() + f.FuncRVA;
		for (auto& d : *pe.GetDebug())
			sum += d.PdbPath.size();
		for (auto& r : *pe.GetRelocations())
			sum += r.RelocData.size();
		sum += pe.GetExceptions()->size() + pe.GetDelayImport()->size() + pe.GetSecurity()->size();
		sum += pe.GetFlatResources().size();
		if (auto tls = pe.GetTLS())
			sum += tls->TLSCallbacks.size();
		if (auto rich = pe.GetRichHeader())
			sum += rich->Entries.size();
		for (auto& s : *pe.GetSecHeaders())
			sum += (size_t)pe.GetOffsetFromRVA(s.SecHdr.VirtualAddress);
		return sum;
	}

	// If a case crashes the process there is nothing left to inspect. Set PECORE_TEST_DUMP to a file name and every
	// case is written there before it is opened, so after a crash that file is the culprit.
	void DumpCase(std::vector<uint8_t> const& bytes) {
		char* path = nullptr;
		size_t length = 0;
		if (_dupenv_s(&path, &length, "PECORE_TEST_DUMP") != 0 || !path)
			return;
		if (FILE* f = nullptr; fopen_s(&f, path, "wb") == 0 && f) {
			fwrite(bytes.data(), 1, bytes.size(), f);
			fclose(f);
		}
		free(path);
	}

	// small deterministic generator, so a failure can be reproduced
	struct Lcg {
		uint32_t State;
		uint32_t Next() {
			State = State * 1664525u + 1013904223u;
			return State >> 8;
		}
	};
}

// Hidden by default. Opens the file named by PECORE_TEST_FILE, to reproduce a crash under a debugger:
//   set PECORE_TEST_FILE=crash_case.bin && PECore.Tests.exe "[.file]"
TEST_CASE("Open the file named by PECORE_TEST_FILE", "[.file]") {
	wchar_t* path = nullptr;
	size_t length = 0;
	REQUIRE((_wdupenv_s(&path, &length, L"PECORE_TEST_FILE") == 0 && path));
	PEFile pe;
	if (pe.Open(path))
		TouchEverything(pe);
	free(path);
}

TEST_CASE("Truncated files do not crash the parser", "[robustness]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	auto bytes = spec.Build();

	// every length up to the end of the headers, then every 8 bytes
	for (size_t length = 0; length < bytes.size(); length += (length < 0x300 ? 1 : 8)) {
		CAPTURE(spec.Is64, length);
		std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + length);
		TempFile file(truncated, L".dll");
		PEFile pe;
		if (pe.Open(file.Path())) {
			CHECK(pe.GetFileSize() == length);
			TouchEverything(pe);
		}
	}
}

TEST_CASE("Damaged headers do not crash the parser", "[robustness]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	auto const original = spec.Build();
	Lcg random{ spec.Is64 ? 1u : 2u };

	for (int i = 0; i < 600; i++) {
		auto bytes = original;
		auto changes = 1 + random.Next() % 4;
		for (uint32_t c = 0; c < changes; c++) {
			// damage the headers and the directories in .rdata
			auto offset = (random.Next() % 2) ? random.Next() % 0x200 : SyntheticPE::RdataOffset + random.Next() % 0x140;
			bytes[offset] = (uint8_t)random.Next();
		}
		CAPTURE(spec.Is64, i);
		DumpCase(bytes);
		TempFile file(bytes, L".dll");
		PEFile pe;
		if (pe.Open(file.Path()))
			TouchEverything(pe);
	}
}

TEST_CASE("Extreme header values do not crash the parser", "[robustness]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	auto original = spec.Build();

	auto nt = [&](std::vector<uint8_t>& b) { return reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data() + SyntheticPE::ELfanew); };
	auto sections = [&](std::vector<uint8_t>& b) {
		auto optSize = nt(b)->FileHeader.SizeOfOptionalHeader;
		return reinterpret_cast<IMAGE_SECTION_HEADER*>(b.data() + SyntheticPE::ELfanew + 4 + sizeof(IMAGE_FILE_HEADER) + optSize);
	};
	auto dirs = [&](std::vector<uint8_t>& b) {
		return spec.Is64 ? reinterpret_cast<IMAGE_NT_HEADERS64*>(b.data() + SyntheticPE::ELfanew)->OptionalHeader.DataDirectory
			: reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data() + SyntheticPE::ELfanew)->OptionalHeader.DataDirectory;
	};

	// each mutation is applied to a fresh copy
	using Mutation = std::pair<char const*, std::function<void(std::vector<uint8_t>&)>>;
	std::vector<Mutation> mutations{
		{ "65535 sections", [&](auto& b) { nt(b)->FileHeader.NumberOfSections = 0xFFFF; } },
		{ "zero sections", [&](auto& b) { nt(b)->FileHeader.NumberOfSections = 0; } },
		{ "optional header size 0", [&](auto& b) { nt(b)->FileHeader.SizeOfOptionalHeader = 0; } },
		{ "optional header size 0xFFFF", [&](auto& b) { nt(b)->FileHeader.SizeOfOptionalHeader = 0xFFFF; } },
		{ "section raw data beyond the file", [&](auto& b) { sections(b)[0].PointerToRawData = 0xFFFFF000; } },
		{ "section raw size huge", [&](auto& b) { sections(b)[1].SizeOfRawData = 0xFFFFFFFF; } },
		{ "section virtual size huge", [&](auto& b) { sections(b)[2].Misc.VirtualSize = 0xFFFFFFFF; } },
		{ "overlapping sections", [&](auto& b) { sections(b)[1].VirtualAddress = sections(b)[0].VirtualAddress; } },
		{ "export directory beyond the image", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0xFFFFFF00; } },
		{ "export directory size huge", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = 0xFFFFFFFF; } },
		{ "import directory beyond the image", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0xFFFFFF00; } },
		{ "import directory points at the headers", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0x10; } },
		{ "resource directory in the middle of nowhere", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_RESOURCE] = { 0x2000, 0x100 }; } },
		{ "security directory beyond the file", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_SECURITY] = { 0xFFFFFF00, 0x1000 }; } },
		{ "relocation directory over code", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_BASERELOC] = { 0x1000, 0x100 }; } },
		{ "exception directory over data", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = { 0x2000, 0x100 }; } },
		{ "debug directory over data", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_DEBUG] = { 0x2000, 0x100 }; } },
		{ "TLS directory over data", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_TLS] = { 0x2000, 0x40 }; } },
		{ "load config over data", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG] = { 0x2000, 0x100 }; } },
		{ "delay import over data", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT] = { 0x2000, 0x40 }; } },
		{ "CLR header over data", [&](auto& b) { dirs(b)[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR] = { 0x2000, 0x48 }; } },
		{ "all directories over data", [&](auto& b) { for (int i = 0; i < 16; i++) dirs(b)[i] = { 0x2000, 0x100 }; } },
		{ "all directories beyond the image", [&](auto& b) { for (int i = 0; i < 16; i++) dirs(b)[i] = { 0xFFFFF000, 0x1000 }; } },
		{ "e_lfanew inside the DOS header", [&](auto& b) { reinterpret_cast<IMAGE_DOS_HEADER*>(b.data())->e_lfanew = 4; } },
		{ "e_lfanew near 4 GB", [&](auto& b) { reinterpret_cast<IMAGE_DOS_HEADER*>(b.data())->e_lfanew = 0xFFFFFFF0; } },
		{ "zero alignment", [&](auto& b) {
			if (spec.Is64) { auto o = &reinterpret_cast<IMAGE_NT_HEADERS64*>(b.data() + SyntheticPE::ELfanew)->OptionalHeader; o->SectionAlignment = 0; o->FileAlignment = 0; }
			else { auto o = &reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data() + SyntheticPE::ELfanew)->OptionalHeader; o->SectionAlignment = 0; o->FileAlignment = 0; } } },
		{ "unknown optional header magic", [&](auto& b) { nt(b)->OptionalHeader.Magic = 0x1234; } },
	};

	for (auto& [name, mutate] : mutations) {
		DYNAMIC_SECTION(name << (spec.Is64 ? " (PE32+)" : " (PE32)")) {
			auto bytes = original;
			mutate(bytes);
			TempFile file(bytes, L".dll");
			PEFile pe;
			if (pe.Open(file.Path()))
				TouchEverything(pe);
			SUCCEED();
		}
	}
}
