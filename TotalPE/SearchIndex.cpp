#include "pch.h"
#include "SearchIndex.h"

namespace {
	std::wstring Fold(std::wstring_view text) {
		std::wstring folded(text);
		if (!folded.empty())
			::CharLowerBuffW(folded.data(), (DWORD)folded.size());
		return folded;
	}
}

void SearchIndex::Add(SearchItem item) {
	m_Folded.push_back(Fold(item.Name));
	m_Items.push_back(std::move(item));
}

void SearchIndex::Clear() {
	m_Items.clear();
	m_Folded.clear();
}

std::vector<uint32_t> SearchIndex::Find(std::wstring_view text, bool matchCase, uint32_t kinds, size_t limit, bool* truncated) const {
	std::vector<uint32_t> found;
	if (truncated)
		*truncated = false;
	if (text.empty())
		return found;
	auto folded = matchCase ? std::wstring(text) : Fold(text);
	for (uint32_t i = 0; i < (uint32_t)m_Items.size(); i++) {
		if ((kinds & SearchKindBit(m_Items[i].Kind)) == 0)
			continue;
		auto const& name = matchCase ? m_Items[i].Name : m_Folded[i];
		if (name.find(folded) == std::wstring::npos)
			continue;
		if (found.size() == limit) {
			if (truncated)
				*truncated = true;
			break;
		}
		found.push_back(i);
	}
	return found;
}

const wchar_t* SearchIndex::KindName(SearchKind kind) {
	switch (kind) {
		case SearchKind::Import: return L"Import";
		case SearchKind::ImportModule: return L"Imported Module";
		case SearchKind::Export: return L"Export";
		case SearchKind::String: return L"String";
		case SearchKind::Resource: return L"Resource";
		case SearchKind::Symbol: return L"Symbol";
		case SearchKind::Section: return L"Section";
	}
	return L"";
}
