#pragma once

#include <cstdint>
#include <string>
#include <vector>

class PEFile;

// The tables of Control Flow Guard (and of EH continuation guard) that the load configuration points to.
// Each entry is an RVA, followed by as many bytes of metadata as the GuardFlags say (the first of them are flags).

enum class GuardTableKind {
	Functions,			// the valid targets of indirect calls (GuardCFFunctionTable)
	AddressTakenIat,	// the import address table slots whose address is taken (GuardAddressTakenIatEntryTable)
	LongJumpTargets,	// the valid targets of longjmp (GuardLongJumpTargetTable)
	EhContinuations,	// the valid targets of an exception handler's continuation (GuardEHContinuationTable)
};

struct GuardEntry {
	uint32_t Rva{};
	uint8_t Flags{};	// IMAGE_GUARD_FLAG_*: suppressed, export suppressed, language exception handler, XFG
};

struct GuardTable {
	GuardTableKind Kind{};
	uint64_t Va{};			// as the load configuration has it
	uint64_t Count{};		// as the load configuration has it
	std::vector<GuardEntry> Entries;
	std::wstring Error;		// why the entries could not be read (or not all of them)
};

const wchar_t* GuardTableName(GuardTableKind kind);
// "Suppressed, XFG" for the flags of an entry
std::wstring GuardEntryFlagsToString(uint8_t flags);

// The tables that the file has (the ones with a count that is not zero), in the order of GuardTableKind
std::vector<GuardTable> ReadGuardTables(PEFile const& pe);
