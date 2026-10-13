#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The items of a file that one search goes through: imports, exports, strings, resources, symbols and section names.
// What an item leads to is up to whoever built the index (the main frame): the fields below say where it is.

enum class SearchKind : uint8_t {
	Import,			// a function a module imports
	ImportModule,	// a module that is imported
	Export,
	String,
	Resource,
	Symbol,
	Section,
	View,			// a node of the tree (Value: what it is, as the main frame has it)
	ClrType,		// a type of a .NET file (Value: its token)
	ClrMember,		// a method or field of a .NET file (Value: its token)
	Address,		// not in the index: an address that Go Anywhere was given (Index: 0 VA, 1 RVA, 2 file offset)
	Count
};

// a set of kinds, a bit for each
constexpr uint32_t SearchKindBit(SearchKind kind) {
	return 1u << (int)kind;
}
constexpr uint32_t AllSearchKinds = (1u << (int)SearchKind::Count) - 1;

struct SearchItem {
	SearchKind Kind{};
	std::wstring Name;		// what the search matches
	std::wstring Details;
	std::wstring Location;	// "RVA 0x1000", "Offset 0x400"...
	// where the item is, as its kind (and the file) has it
	uint64_t Value{};		// an address, an offset...
	int Index{ -1 };		// a section, a resource, a module, a member...
	std::wstring Owner;		// the module of an import
	bool Code{ false };		// an address of code
	int Image{ -1 };		// the icon (in the image list of the tree)
};

class SearchIndex {
public:
	void Add(SearchItem item);
	void Clear();
	size_t Size() const { return m_Items.size(); }
	bool Empty() const { return m_Items.empty(); }
	SearchItem const& operator[](size_t index) const { return m_Items[index]; }

	// The items of the kinds whose name contains the text, in the order they were added; at most 'limit' of them.
	// 'truncated' says if there were more.
	std::vector<uint32_t> Find(std::wstring_view text, bool matchCase, uint32_t kinds, size_t limit, bool* truncated = nullptr) const;

	// The items that match the text best, best first (ignoring case): the name is the text, then names that start with it,
	// then those where a word starts with it ("Proc" in "GetProcAddress"), then those that contain it; the kinds that lead
	// somewhere most directly before strings, and shorter names first. Words separated by spaces must all be in the name.
	std::vector<uint32_t> FindBest(std::wstring_view text, uint32_t kinds, size_t limit) const;

	// How a name matches the text (both in lower case; the name as it is, for the starts of words): 0 the same, 1 a prefix,
	// 2 at the start of a word, 3 inside; -1 if it does not
	static int MatchRank(std::wstring_view name, std::wstring_view folded, std::wstring_view text);

	static const wchar_t* KindName(SearchKind kind);

private:
	std::vector<SearchItem> m_Items;
	std::vector<std::wstring> m_Folded;		// the names in lower case
};
