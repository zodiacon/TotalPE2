#include "pch.h"
#include "UnwindInfo.h"
#include <PEFile.h>

namespace {
	// the unwind operations (winnt.h does not define them)
	enum : uint8_t {
		UWOP_PUSH_NONVOL = 0,
		UWOP_ALLOC_LARGE,
		UWOP_ALLOC_SMALL,
		UWOP_SET_FPREG,
		UWOP_SAVE_NONVOL,
		UWOP_SAVE_NONVOL_FAR,
		UWOP_EPILOG,			// version 2; UWOP_SAVE_XMM in version 1
		UWOP_SPARE_CODE,		// version 2; UWOP_SAVE_XMM_FAR in version 1
		UWOP_SAVE_XMM128,
		UWOP_SAVE_XMM128_FAR,
		UWOP_PUSH_MACHFRAME,
	};

	constexpr uint8_t FlagEHandler = 1, FlagUHandler = 2, FlagChainInfo = 4;

	std::wstring Hex(uint32_t value) {
		return std::format(L"0x{:X}", value);
	}
}

const wchar_t* X64RegisterName(uint8_t reg) {
	static const wchar_t* const names[] = {
		L"rax", L"rcx", L"rdx", L"rbx", L"rsp", L"rbp", L"rsi", L"rdi",
		L"r8", L"r9", L"r10", L"r11", L"r12", L"r13", L"r14", L"r15",
	};
	return reg < _countof(names) ? names[reg] : L"?";
}

std::wstring UnwindFlagsToString(uint8_t flags) {
	std::wstring text;
	auto add = [&](uint8_t flag, PCWSTR name) {
		if (flags & flag) {
			if (!text.empty())
				text += L", ";
			text += name;
		}
	};
	add(FlagEHandler, L"EHANDLER");
	add(FlagUHandler, L"UHANDLER");
	add(FlagChainInfo, L"CHAININFO");
	return text;
}

UnwindInfo DecodeUnwindInfo(std::span<const std::byte> data, uint32_t rva) {
	UnwindInfo info;
	auto byteAt = [&](size_t i) { return std::to_integer<uint8_t>(data[i]); };
	auto readU16 = [&](size_t i) { return (uint16_t)(byteAt(i) | (byteAt(i + 1) << 8)); };
	auto readU32 = [&](size_t i) { return (uint32_t)readU16(i) | ((uint32_t)readU16(i + 2) << 16); };

	if (data.size() < 4) {
		info.Error = L"The unwind information is outside the file";
		return info;
	}
	info.Version = byteAt(0) & 7;
	info.Flags = byteAt(0) >> 3;
	info.SizeOfProlog = byteAt(1);
	info.CountOfCodes = byteAt(2);
	info.FrameRegister = byteAt(3) & 0xF;
	info.FrameOffset = byteAt(3) >> 4;
	if (info.Version != 1 && info.Version != 2) {
		info.Error = std::format(L"Unknown version ({})", info.Version);
		return info;
	}

	// the codes are an array of 16-bit slots; some codes use the slots that follow them for their operand
	const size_t codesStart = 4;
	const size_t codesEnd = codesStart + info.CountOfCodes * 2;
	if (codesEnd > data.size()) {
		info.Error = L"The unwind codes are outside the file";
		return info;
	}
	size_t slot = 0;
	bool firstEpilog = true;
	while (slot < info.CountOfCodes) {
		auto at = codesStart + slot * 2;
		UnwindCode code{ byteAt(at), (uint8_t)(byteAt(at + 1) & 0xF) };
		uint8_t opInfo = byteAt(at + 1) >> 4;
		size_t slots = 1;
		auto operand16 = [&](size_t index) { return readU16(at + index * 2); };	// the slot 'index' after this one
		auto need = [&](size_t count) {
			if (slot + count > info.CountOfCodes) {
				info.Error = std::format(L"The code at slot {} needs {} slots, but only {} are left", slot, count, info.CountOfCodes - slot);
				return false;
			}
			slots = count;
			return true;
		};

		switch (code.Op) {
			case UWOP_PUSH_NONVOL:
				code.Operation = L"PUSH_NONVOL";
				code.Details = X64RegisterName(opInfo);
				break;

			case UWOP_ALLOC_LARGE:
				code.Operation = L"ALLOC_LARGE";
				if (opInfo == 0) {
					if (!need(2))
						break;
					code.Details = std::format(L"{} bytes", Hex(operand16(1) * 8u));
				}
				else {
					if (!need(3))
						break;
					code.Details = std::format(L"{} bytes", Hex(operand16(1) | ((uint32_t)operand16(2) << 16)));
				}
				break;

			case UWOP_ALLOC_SMALL:
				code.Operation = L"ALLOC_SMALL";
				code.Details = std::format(L"{} bytes", Hex(opInfo * 8u + 8));
				break;

			case UWOP_SET_FPREG:
				code.Operation = L"SET_FPREG";
				code.Details = std::format(L"{} = rsp + {}", X64RegisterName(info.FrameRegister), Hex(info.FrameOffset * 16u));
				break;

			case UWOP_SAVE_NONVOL:
				code.Operation = L"SAVE_NONVOL";
				if (need(2))
					code.Details = std::format(L"{} at [rsp + {}]", X64RegisterName(opInfo), Hex(operand16(1) * 8u));
				break;

			case UWOP_SAVE_NONVOL_FAR:
				code.Operation = L"SAVE_NONVOL_FAR";
				if (need(3))
					code.Details = std::format(L"{} at [rsp + {}]", X64RegisterName(opInfo), Hex(operand16(1) | ((uint32_t)operand16(2) << 16)));
				break;

			case UWOP_EPILOG:
				if (info.Version == 1) {
					code.Operation = L"SAVE_XMM";
					if (need(2))
						code.Details = std::format(L"xmm{} at [rsp + {}]", opInfo, Hex(operand16(1) * 8u));
				}
				else {
					// the first describes the epilogs' size; the others where they are, counted back from the end of the function
					code.Operation = L"EPILOG";
					if (firstEpilog)
						code.Details = std::format(L"Size {}{}", Hex(code.CodeOffset), (opInfo & 1) ? L", at the end of the function" : L"");
					else if (code.CodeOffset || opInfo)
						code.Details = std::format(L"At end - {}", Hex(code.CodeOffset | (opInfo << 8)));
					else
						code.Details = L"(padding)";
					firstEpilog = false;
				}
				break;

			case UWOP_SPARE_CODE:
				code.Operation = info.Version == 1 ? L"SAVE_XMM_FAR" : L"SPARE_CODE";
				if (need(3) && info.Version == 1)
					code.Details = std::format(L"xmm{} at [rsp + {}]", opInfo, Hex(operand16(1) | ((uint32_t)operand16(2) << 16)));
				break;

			case UWOP_SAVE_XMM128:
				code.Operation = L"SAVE_XMM128";
				if (need(2))
					code.Details = std::format(L"xmm{} at [rsp + {}]", opInfo, Hex(operand16(1) * 16u));
				break;

			case UWOP_SAVE_XMM128_FAR:
				code.Operation = L"SAVE_XMM128_FAR";
				if (need(3))
					code.Details = std::format(L"xmm{} at [rsp + {}]", opInfo, Hex(operand16(1) | ((uint32_t)operand16(2) << 16)));
				break;

			case UWOP_PUSH_MACHFRAME:
				code.Operation = L"PUSH_MACHFRAME";
				code.Details = opInfo ? L"With an error code" : L"";
				break;

			default:
				code.Operation = std::format(L"Unknown ({})", code.Op);
				break;
		}
		if (!info.Error.empty())
			return info;
		info.Codes.push_back(std::move(code));
		slot += slots;
	}

	// what follows the codes is aligned to 4 bytes
	size_t next = codesStart + ((info.CountOfCodes + 1) & ~1) * 2;
	if (info.Flags & FlagChainInfo) {
		if (next + 12 > data.size()) {
			info.Error = L"The chained function entry is outside the file";
			return info;
		}
		info.ChainedBegin = readU32(next);
		info.ChainedEnd = readU32(next + 4);
		info.ChainedUnwindInfo = readU32(next + 8);
	}
	else if (info.Flags & (FlagEHandler | FlagUHandler)) {
		if (next + 4 > data.size()) {
			info.Error = L"The exception handler is outside the file";
			return info;
		}
		info.HandlerRva = readU32(next);
		info.HandlerDataRva = rva + (uint32_t)next + 4;
	}
	return info;
}

UnwindInfo DecodeUnwindInfo(PEFile const& pe, uint32_t rva) {
	auto offset = pe.GetOffsetFromRVA(rva);
	auto fileSize = pe.GetFileSize();
	if (rva == 0 || offset == 0 || offset >= fileSize) {
		UnwindInfo info;
		info.Error = std::format(L"The unwind information (RVA 0x{:X}) is not in the file", rva);
		return info;
	}
	// 4 bytes of header, at most 255 slots and then a handler or a function entry
	auto size = (uint32_t)std::min<uint64_t>(fileSize - offset, 4 + 256 * 2 + 12);
	return DecodeUnwindInfo(pe.GetSpan((uint32_t)offset, size), rva);
}
