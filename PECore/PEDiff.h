#pragma once

#include <cstdint>
#include <string>
#include <vector>

class PEFile;

// The differences between two PE files: "this file" (the one that is open) and "the other file", as an older and a newer
// version of a binary. Added items are only in the other file, removed items only in this one.

enum class DiffStatus : uint8_t {
	Same,
	Changed,
	Added,		// only in the other file
	Removed,	// only in this file
};

enum class DiffCategory : uint8_t {
	File,
	Header,
	Directory,
	Section,
	Import,
	Export,
	Resource,
	Version,
	Debug,
};

// Where an item of this file is, for showing it
enum class DiffPlace : uint8_t {
	None,			// nowhere (an item of the other file, a header field...)
	Section,		// Index: the section
	ImportModule,	// Name: the module, Index: its position in the imports
	Import,			// Name: the function ("#ordinal" for an import by ordinal, Value: the ordinal), Owner: the module
	Export,			// Name: the name (or "#ordinal"), Value: the ordinal
	Resource,		// Index: the resource in PEFile::GetFlatResources
};

struct DiffLocation {
	DiffPlace Place{ DiffPlace::None };
	std::wstring Name;
	uint64_t Value{};
	int Index{ -1 };
	std::wstring Owner;
};

struct DiffItem {
	DiffCategory Category{};
	std::wstring Name;
	DiffStatus Status{};
	std::wstring Left, Right;	// the values in this file and in the other file (empty where the item is not)
	std::wstring Details;		// what is different
	DiffLocation Where;			// in this file
};

struct PEDiff {
	std::vector<DiffItem> Items;

	size_t Count(DiffStatus status) const;
	// no differences at all (not even in the bytes)
	bool Identical{ false };

	static const wchar_t* StatusName(DiffStatus status);
	static const wchar_t* CategoryName(DiffCategory category);
};

PEDiff ComparePEFiles(PEFile const& left, PEFile const& right);
