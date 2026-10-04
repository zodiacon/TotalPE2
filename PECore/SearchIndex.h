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

	static const wchar_t* KindName(SearchKind kind);

private:
	std::vector<SearchItem> m_Items;
	std::vector<std::wstring> m_Folded;		// the names in lower case
};
