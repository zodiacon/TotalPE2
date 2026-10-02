#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Builds a small but valid PE file in memory, so tests do not depend on files that happen to exist on the machine.
//
// Layout (all values are fixed, tests refer to them by name):
//   headers   file 0x000..0x1FF
//   .text     RVA 0x1000, file 0x200, size 0x200   code (a single RET at the entry point)
//   .data     RVA 0x2000, file 0x400, size 0x200   data
//   .rdata    RVA 0x3000, file 0x600, size 0x200   export directory ("test.dll": Alpha, Beta),
//                                                  import directory (kernel32.dll: ExitProcess) and the IAT
struct SyntheticPE {
	bool Is64{ true };
	bool Dll{ true };
	bool Clr{ false };	// add a CLR header and metadata (see below)
	std::string ImportModule{ "kernel32.dll" };	// the module the single import comes from
	bool ImportByOrdinal{ false };	// import ImportOrdinal instead of ExitProcess
	uint16_t ImportOrdinal{ 12 };

	static constexpr uint32_t FileAlignment = 0x200;
	static constexpr uint32_t SectionAlignment = 0x1000;
	static constexpr uint32_t HeadersSize = 0x200;
	static constexpr uint32_t ELfanew = 0x80;
	static constexpr uint32_t TimeDateStamp = 0x5F000000;

	static constexpr uint32_t TextRva = 0x1000, TextOffset = 0x200;
	static constexpr uint32_t DataRva = 0x2000, DataOffset = 0x400;
	static constexpr uint32_t RdataRva = 0x3000, RdataOffset = 0x600;
	static constexpr uint32_t SectionSize = 0x200;
	static constexpr uint32_t ImageSize = 0x4000;
	static constexpr uint32_t EntryPointRva = 0x1000;
	static constexpr uint32_t FileSize = 0x800;

	// export directory contents
	static constexpr uint32_t ExportDirRva = 0x3000, ExportDirSize = 0xA0;
	static constexpr uint32_t AlphaRva = 0x1000, BetaRva = 0x1010;

	// import directory contents
	static constexpr uint32_t ImportDirRva = 0x30A0, ImportDirSize = 0x28;
	static constexpr uint32_t IatRva = 0x30F0;

	// .NET: with Clr set, .data also holds a CLR header at RVA 0x2080 and the metadata at 0x2100
	//   module "Test.dll", assembly "TestAsm" 1.2.3.4, references mscorlib 4.0.0.0 and System.Core 3.5.0.0
	static constexpr uint32_t ClrHeaderRva = 0x2080, ClrMetadataRva = 0x2100, ClrMetadataSize = 0xD4;

	size_t OptionalHeaderOffset() const { return ELfanew + 4 + 20; }
	size_t SectionHeaderOffset(int index) const { return OptionalHeaderOffset() + (Is64 ? 240 : 224) + (size_t)index * 40; }

	uint64_t ImageBase() const { return Is64 ? 0x140000000ULL : 0x400000ULL; }
	uint32_t PointerSize() const { return Is64 ? 8 : 4; }
	uint32_t IatSize() const { return PointerSize() * 2; }	// one import plus the terminator

	std::vector<uint8_t> Build() const;

private:
	void AddClr(std::vector<uint8_t>& buf, IMAGE_DATA_DIRECTORY* dirs) const;
};

// Writes bytes to a unique temporary file that is deleted when the object goes out of scope.
class TempFile {
public:
	explicit TempFile(std::vector<uint8_t> const& data, PCWSTR extension = L".bin");
	explicit TempFile(std::string const& text);
	~TempFile();
	TempFile(TempFile const&) = delete;
	TempFile& operator=(TempFile const&) = delete;

	std::wstring const& Path() const { return m_Path; }

private:
	void Write(const void* data, size_t size);
	std::wstring m_Path;
};
