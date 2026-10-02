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

	// .data: a recognizable pattern (0, 1 ... 15, 0, 1 ...), with the low entropy of ordinary data
	for (uint32_t i = 0; i < SectionSize; i++)
		buf[DataOffset + i] = (uint8_t)(i % 16);

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
	// by name: the thunk is the RVA of the hint/name entry; by ordinal: the ordinal with the top bit set
	uint64_t thunk64 = ImportByOrdinal ? (IMAGE_ORDINAL_FLAG64 | ImportOrdinal) : 0x3100;
	DWORD thunk32 = ImportByOrdinal ? (IMAGE_ORDINAL_FLAG32 | ImportOrdinal) : 0x3100;
	if (Is64) {
		*At<uint64_t>(buf, rdata(0x30D0)) = thunk64;	// import name table
		*At<uint64_t>(buf, rdata(IatRva)) = thunk64;	// import address table
	}
	else {
		*At<DWORD>(buf, rdata(0x30D0)) = thunk32;
		*At<DWORD>(buf, rdata(IatRva)) = thunk32;
	}
	*At<WORD>(buf, rdata(0x3100)) = 0;	// hint
	PutString(buf, rdata(0x3102), "ExitProcess");
	PutString(buf, rdata(0x3120), ImportModule.c_str());	// up to 0xE0 bytes

	if (Clr)
		AddClr(buf, dirs);
	return buf;
}

void SyntheticPE::AddClr(std::vector<uint8_t>& buf, IMAGE_DATA_DIRECTORY* dirs) const {
	auto data = [&](uint32_t rva) { return (size_t)DataOffset + (rva - DataRva); };

	// the CLR header and metadata replace the pattern that .data otherwise holds
	memset(buf.data() + data(ClrHeaderRva), 0, ClrMetadataRva + ClrMetadataSize - ClrHeaderRva);

	auto cor = At<IMAGE_COR20_HEADER>(buf, data(ClrHeaderRva));
	cor->cb = sizeof(IMAGE_COR20_HEADER);
	cor->MajorRuntimeVersion = 2;
	cor->MinorRuntimeVersion = 5;
	cor->MetaData = { ClrMetadataRva, ClrMetadataSize };
	cor->Flags = COMIMAGE_FLAGS_ILONLY;
	cor->EntryPointToken = 0x06000001;
	dirs[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR] = { ClrHeaderRva, sizeof(IMAGE_COR20_HEADER) };

	// The buffer is zero filled, so skipping bytes writes zeros (padding and string terminators).
	size_t p = data(ClrMetadataRva);
	auto put16 = [&](uint16_t v) { *At<uint16_t>(buf, p) = v; p += 2; };
	auto put32 = [&](uint32_t v) { *At<uint32_t>(buf, p) = v; p += 4; };
	// a string followed by zero padding up to 'width' bytes
	auto putPadded = [&](char const* s, size_t width) { memcpy(buf.data() + p, s, strlen(s)); p += width; };
	// a zero terminated string
	auto putZ = [&](char const* s) { memcpy(buf.data() + p, s, strlen(s)); p += strlen(s) + 1; };

	// metadata root
	const uint32_t tablesOffset = 0x40, tablesSize = 108;
	const uint32_t stringsOffset = tablesOffset + tablesSize, stringsSize = 40;
	put32(0x424A5342);	// "BSJB"
	put16(1); put16(1);
	put32(0);
	put32(12);			// length of the version string
	putPadded("v4.0.30319", 12);
	put16(0);
	put16(2);			// streams
	put32(tablesOffset); put32(tablesSize); putPadded("#~", 4);
	put32(stringsOffset); put32(stringsSize); putPadded("#Strings", 12);

	// "#~" stream: Module (1 row), Assembly (1 row), AssemblyRef (2 rows); all heap indexes are 2 bytes wide
	p = data(ClrMetadataRva) + tablesOffset;
	put32(0);			// reserved
	buf[p++] = 2;		// major version
	buf[p++] = 0;		// minor version
	buf[p++] = 0;		// heap sizes
	buf[p++] = 1;		// reserved
	// "valid" is a 64 bit mask: tables 0x20 and 0x23 are bits 0 and 3 of the upper half
	put32(1u << 0);
	put32((1u << (0x20 - 32)) | (1u << (0x23 - 32)));
	put32(0); put32(0);	// sorted
	put32(1); put32(1); put32(2);	// row counts
	// Module: Generation, Name, Mvid, EncId, EncBaseId
	put16(0); put16(1); put16(0); put16(0); put16(0);
	// Assembly: HashAlgId, version, Flags, PublicKey, Name, Culture
	put32(0x8004); put16(1); put16(2); put16(3); put16(4); put32(0); put16(0); put16(10); put16(0);
	// AssemblyRef: version, Flags, PublicKeyOrToken, Name, Culture, HashValue
	put16(4); put16(0); put16(0); put16(0); put32(0); put16(0); put16(18); put16(0); put16(0);
	put16(3); put16(5); put16(0); put16(0); put32(0); put16(0); put16(27); put16(0); put16(0);

	// "#Strings" stream: index 0 is the empty string, then "Test.dll" (1), "TestAsm" (10), "mscorlib" (18), "System.Core" (27)
	p = data(ClrMetadataRva) + stringsOffset + 1;
	putZ("Test.dll");
	putZ("TestAsm");
	putZ("mscorlib");
	putZ("System.Core");
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
