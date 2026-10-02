#include "TestCommon.h"
#include <cstring>

namespace {
	template<typename T>
	T* At(std::vector<uint8_t>& buf, size_t offset) {
		return reinterpret_cast<T*>(buf.data() + offset);
	}

	void PutString(std::vector<uint8_t>& buf, size_t offset, char const* s) {
		memcpy(buf.data() + offset, s, strlen(s) + 1);
	}
}

std::vector<uint8_t> SyntheticPE::Build() const {
	std::vector<uint8_t> buf(FileSize, 0);

	// DOS header
	auto dos = At<IMAGE_DOS_HEADER>(buf, 0);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = ELfanew;

	// NT headers
	*At<DWORD>(buf, ELfanew) = IMAGE_NT_SIGNATURE;
	auto fh = At<IMAGE_FILE_HEADER>(buf, ELfanew + 4);
	fh->Machine = Is64 ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386;
	fh->NumberOfSections = 3;
	fh->TimeDateStamp = TimeDateStamp;
	fh->Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | (Dll ? IMAGE_FILE_DLL : 0) | (Is64 ? IMAGE_FILE_LARGE_ADDRESS_AWARE : IMAGE_FILE_32BIT_MACHINE);

	size_t optOffset = ELfanew + 4 + sizeof(IMAGE_FILE_HEADER);
	IMAGE_DATA_DIRECTORY* dirs;
	if (Is64) {
		fh->SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
		auto opt = At<IMAGE_OPTIONAL_HEADER64>(buf, optOffset);
		opt->Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		opt->MajorLinkerVersion = 14;
		opt->SizeOfCode = SectionSize;
		opt->SizeOfInitializedData = SectionSize * 2;
		opt->AddressOfEntryPoint = EntryPointRva;
		opt->BaseOfCode = TextRva;
		opt->ImageBase = ImageBase();
		opt->SectionAlignment = SectionAlignment;
		opt->FileAlignment = FileAlignment;
		opt->MajorOperatingSystemVersion = 6;
		opt->MajorSubsystemVersion = 6;
		opt->SizeOfImage = ImageSize;
		opt->SizeOfHeaders = HeadersSize;
		opt->Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
		opt->DllCharacteristics = IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
		opt->SizeOfStackReserve = 0x100000;
		opt->SizeOfStackCommit = 0x1000;
		opt->SizeOfHeapReserve = 0x100000;
		opt->SizeOfHeapCommit = 0x1000;
		opt->NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
		dirs = opt->DataDirectory;
	}
	else {
		fh->SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
		auto opt = At<IMAGE_OPTIONAL_HEADER32>(buf, optOffset);
		opt->Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
		opt->MajorLinkerVersion = 14;
		opt->SizeOfCode = SectionSize;
		opt->SizeOfInitializedData = SectionSize * 2;
		opt->AddressOfEntryPoint = EntryPointRva;
		opt->BaseOfCode = TextRva;
		opt->BaseOfData = DataRva;
		opt->ImageBase = (DWORD)ImageBase();
		opt->SectionAlignment = SectionAlignment;
		opt->FileAlignment = FileAlignment;
		opt->MajorOperatingSystemVersion = 6;
		opt->MajorSubsystemVersion = 6;
		opt->SizeOfImage = ImageSize;
		opt->SizeOfHeaders = HeadersSize;
		opt->Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
		opt->DllCharacteristics = IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
		opt->SizeOfStackReserve = 0x100000;
		opt->SizeOfStackCommit = 0x1000;
		opt->SizeOfHeapReserve = 0x100000;
		opt->SizeOfHeapCommit = 0x1000;
		opt->NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
		dirs = opt->DataDirectory;
	}
	dirs[IMAGE_DIRECTORY_ENTRY_EXPORT] = { ExportDirRva, ExportDirSize };
	dirs[IMAGE_DIRECTORY_ENTRY_IMPORT] = { ImportDirRva, ImportDirSize };
	dirs[IMAGE_DIRECTORY_ENTRY_IAT] = { IatRva, IatSize() };

	// section headers
	auto sec = At<IMAGE_SECTION_HEADER>(buf, optOffset + fh->SizeOfOptionalHeader);
	auto setSection = [&](IMAGE_SECTION_HEADER& s, char const* name, uint32_t rva, uint32_t offset, DWORD characteristics) {
		strncpy_s((char*)s.Name, sizeof(s.Name), name, _TRUNCATE);
		s.Misc.VirtualSize = SectionSize;
		s.VirtualAddress = rva;
		s.SizeOfRawData = SectionSize;
		s.PointerToRawData = offset;
		s.Characteristics = characteristics;
	};
	setSection(sec[0], ".text", TextRva, TextOffset, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
	setSection(sec[1], ".data", DataRva, DataOffset, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
	setSection(sec[2], ".rdata", RdataRva, RdataOffset, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);

	// .text: INT3 padding with a RET at the entry point
	memset(buf.data() + TextOffset, 0xCC, SectionSize);
	buf[TextOffset] = 0xC3;

	// .data: a recognizable pattern
	for (uint32_t i = 0; i < SectionSize; i++)
		buf[DataOffset + i] = (uint8_t)i;

	// .rdata, addressed by offsets relative to the section start (RVA = RdataRva + offset)
	auto rdata = [&](uint32_t rvaInSection) { return (size_t)RdataOffset + (rvaInSection - RdataRva); };

	// export directory at 0x3000
	auto exp = At<IMAGE_EXPORT_DIRECTORY>(buf, rdata(0x3000));
	exp->TimeDateStamp = TimeDateStamp;
	exp->Name = 0x3070;
	exp->Base = 1;
	exp->NumberOfFunctions = 2;
	exp->NumberOfNames = 2;
	exp->AddressOfFunctions = 0x3040;
	exp->AddressOfNames = 0x3050;
	exp->AddressOfNameOrdinals = 0x3060;
	*At<DWORD>(buf, rdata(0x3040)) = AlphaRva;
	*At<DWORD>(buf, rdata(0x3044)) = BetaRva;
	*At<DWORD>(buf, rdata(0x3050)) = 0x3080;	// names must be sorted
	*At<DWORD>(buf, rdata(0x3054)) = 0x3090;
	*At<WORD>(buf, rdata(0x3060)) = 0;
	*At<WORD>(buf, rdata(0x3062)) = 1;
	PutString(buf, rdata(0x3070), "test.dll");
	PutString(buf, rdata(0x3080), "Alpha");
	PutString(buf, rdata(0x3090), "Beta");

	// import directory at 0x30A0: kernel32.dll!ExitProcess (the second descriptor is the zero terminator)
	auto imp = At<IMAGE_IMPORT_DESCRIPTOR>(buf, rdata(ImportDirRva));
	imp->OriginalFirstThunk = 0x30D0;
	imp->Name = 0x3120;
	imp->FirstThunk = IatRva;
	if (Is64) {
		*At<uint64_t>(buf, rdata(0x30D0)) = 0x3100;	// import name table
		*At<uint64_t>(buf, rdata(IatRva)) = 0x3100;	// import address table
	}
	else {
		*At<DWORD>(buf, rdata(0x30D0)) = 0x3100;
		*At<DWORD>(buf, rdata(IatRva)) = 0x3100;
	}
	*At<WORD>(buf, rdata(0x3100)) = 0;	// hint
	PutString(buf, rdata(0x3102), "ExitProcess");
	PutString(buf, rdata(0x3120), "kernel32.dll");

	return buf;
}

TempFile::TempFile(std::vector<uint8_t> const& data, PCWSTR extension) {
	Write(data.data(), data.size());
	if (wcscmp(extension, L".bin") != 0) {
		auto renamed = m_Path.substr(0, m_Path.size() - 4) + extension;
		if (::MoveFileW(m_Path.c_str(), renamed.c_str()))
			m_Path = renamed;
	}
}

TempFile::TempFile(std::string const& text) {
	Write(text.data(), text.size());
}

void TempFile::Write(const void* data, size_t size) {
	WCHAR dir[MAX_PATH], path[MAX_PATH];
	::GetTempPathW(MAX_PATH, dir);
	::GetTempFileNameW(dir, L"pct", 0, path);	// creates the file
	m_Path = path;

	wil::unique_hfile file(::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	REQUIRE(file);
	if (size) {
		DWORD written = 0;
		REQUIRE(::WriteFile(file.get(), data, (DWORD)size, &written, nullptr));
		REQUIRE(written == size);
	}
}

TempFile::~TempFile() {
	::DeleteFileW(m_Path.c_str());
}
