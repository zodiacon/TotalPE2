#include "pch.h"
#include "GuardTables.h"
#include <PEFile.h>

namespace {
	// the size of an entry's metadata is in the high bits of GuardFlags (IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK)
	constexpr uint32_t StrideMask = 0xF0000000, StrideShift = 28;
	// a table cannot be bigger than the image; this keeps a damaged count from asking for gigabytes
	constexpr uint64_t MaxEntries = 0x4000000;
}

const wchar_t* GuardTableName(GuardTableKind kind) {
	switch (kind) {
		case GuardTableKind::Functions: return L"CFG Functions";
		case GuardTableKind::AddressTakenIat: return L"CFG Address-Taken IAT Entries";
		case GuardTableKind::LongJumpTargets: return L"CFG Long Jump Targets";
		case GuardTableKind::EhContinuations: return L"EH Continuation Targets";
	}
	return L"";
}

std::wstring GuardEntryFlagsToString(uint8_t flags) {
	std::wstring text;
	auto add = [&](uint8_t flag, PCWSTR name) {
		if (flags & flag)
			text += (text.empty() ? L"" : L", ") + std::wstring(name);
	};
	add(0x01, L"Suppressed");					// IMAGE_GUARD_FLAG_FID_SUPPRESSED
	add(0x02, L"Export Suppressed");			// IMAGE_GUARD_FLAG_EXPORT_SUPPRESSED
	add(0x04, L"Language Exception Handler");	// IMAGE_GUARD_FLAG_FID_LANGEXCPTHANDLER
	add(0x08, L"XFG");							// IMAGE_GUARD_FLAG_FID_XFG
	if (flags & 0xF0)
		text += (text.empty() ? L"" : L", ") + std::format(L"0x{:02X}", flags & 0xF0);
	return text;
}

std::vector<GuardTable> ReadGuardTables(PEFile const& pe) {
	std::vector<GuardTable> tables;
	auto lc = pe.GetLoadConfig();
	if (!lc)
		return tables;
	bool is64 = pe.GetFileInfo()->IsPE64;
	auto const& lc32 = lc->LCD32;
	auto const& lc64 = lc->LCD64;
	// the fields that are past the size of the structure in the file are not there (they are zero)
	uint32_t size = is64 ? lc64.Size : lc32.Size;
	uint32_t guardFlags = is64 ? lc64.GuardFlags : lc32.GuardFlags;
	if (size < offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, GuardFlags) + 4)
		return tables;
	uint32_t stride = 4 + ((guardFlags & StrideMask) >> StrideShift);

	struct Field {
		GuardTableKind Kind;
		uint64_t Va, Count;
		size_t End;		// where the count ends in the structure: the field is there if the structure is that big
	};
	Field fields[] = {
		{ GuardTableKind::Functions, is64 ? lc64.GuardCFFunctionTable : lc32.GuardCFFunctionTable, is64 ? lc64.GuardCFFunctionCount : lc32.GuardCFFunctionCount,
			is64 ? offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFFunctionCount) + 8 : offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, GuardCFFunctionCount) + 4 },
		{ GuardTableKind::AddressTakenIat, is64 ? lc64.GuardAddressTakenIatEntryTable : lc32.GuardAddressTakenIatEntryTable,
			is64 ? lc64.GuardAddressTakenIatEntryCount : lc32.GuardAddressTakenIatEntryCount,
			is64 ? offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardAddressTakenIatEntryCount) + 8 : offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, GuardAddressTakenIatEntryCount) + 4 },
		{ GuardTableKind::LongJumpTargets, is64 ? lc64.GuardLongJumpTargetTable : lc32.GuardLongJumpTargetTable,
			is64 ? lc64.GuardLongJumpTargetCount : lc32.GuardLongJumpTargetCount,
			is64 ? offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardLongJumpTargetCount) + 8 : offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, GuardLongJumpTargetCount) + 4 },
		{ GuardTableKind::EhContinuations, is64 ? lc64.GuardEHContinuationTable : lc32.GuardEHContinuationTable,
			is64 ? lc64.GuardEHContinuationCount : lc32.GuardEHContinuationCount,
			is64 ? offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardEHContinuationCount) + 8 : offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, GuardEHContinuationCount) + 4 },
	};

	auto base = pe.GetImageBase();
	auto fileSize = pe.GetFileSize();
	for (auto const& f : fields) {
		if (size < f.End || f.Count == 0)
			continue;
		GuardTable t{ f.Kind, f.Va, f.Count };
		if (f.Va < base || f.Va - base > 0xFFFFFFFF) {
			t.Error = std::format(L"The table (0x{:X}) is not in the image", f.Va);
			tables.push_back(std::move(t));
			continue;
		}
		auto offset = pe.GetOffsetFromRVA(f.Va - base);
		if (offset == 0 || offset >= fileSize) {
			t.Error = std::format(L"The table (RVA 0x{:X}) is not in the file", f.Va - base);
			tables.push_back(std::move(t));
			continue;
		}
		auto count = std::min<uint64_t>(f.Count, MaxEntries);
		auto available = (fileSize - offset) / stride;
		if (count > available) {
			t.Error = std::format(L"The table has {} entries, but only {} fit in the file", f.Count, available);
			count = available;
		}
		auto data = pe.GetData() + offset;
		t.Entries.reserve((size_t)count);
		for (uint64_t i = 0; i < count; i++) {
			auto p = data + i * stride;
			GuardEntry e;
			memcpy(&e.Rva, p, 4);
			e.Flags = stride > 4 ? p[4] : 0;
			t.Entries.push_back(e);
		}
		tables.push_back(std::move(t));
	}
	return tables;
}
