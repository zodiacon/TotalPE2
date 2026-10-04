#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class PEFile;

// One operation of a function's prolog, as the x64 unwinder sees it
struct UnwindCode {
	uint8_t CodeOffset;			// the offset in the prolog of the end of the instruction
	uint8_t Op;					// UWOP_*
	std::wstring Operation;		// "PUSH_NONVOL", "ALLOC_SMALL"...
	std::wstring Details;		// "rbp", "0x28 bytes"...
};

// The UNWIND_INFO of an x64 function: how to undo its prolog, and its exception handler, if it has one
struct UnwindInfo {
	uint8_t Version{ 0 };
	uint8_t Flags{ 0 };			// UNW_FLAG_EHANDLER, UNW_FLAG_UHANDLER, UNW_FLAG_CHAININFO
	uint8_t SizeOfProlog{ 0 };
	uint8_t CountOfCodes{ 0 };	// in slots: some codes take more than one
	uint8_t FrameRegister{ 0 };	// 0 if the function has no frame pointer
	uint8_t FrameOffset{ 0 };	// scaled by 16
	std::vector<UnwindCode> Codes;
	uint32_t HandlerRva{ 0 };		// the exception handler, if Flags has UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER
	uint32_t HandlerDataRva{ 0 };	// the handler's data (the scope table of __C_specific_handler, for one)
	// the function whose unwind information continues this one's (UNW_FLAG_CHAININFO)
	std::optional<uint32_t> ChainedBegin, ChainedEnd, ChainedUnwindInfo;
	std::wstring Error;			// why the data could not be decoded (or was only partly decoded)

	bool Valid() const { return Error.empty(); }
	bool HasHandler() const { return HandlerRva != 0; }
};

// Decodes the unwind information at the start of 'data', which is at 'rva' in the image
UnwindInfo DecodeUnwindInfo(std::span<const std::byte> data, uint32_t rva);
// Reads it from the file
UnwindInfo DecodeUnwindInfo(PEFile const& pe, uint32_t rva);

// "EHANDLER, UHANDLER", "CHAININFO" or "" for the flags of an UNWIND_INFO
std::wstring UnwindFlagsToString(uint8_t flags);
// The name of an x64 general purpose register by its number (0 is rax, 15 is r15)
const wchar_t* X64RegisterName(uint8_t reg);
