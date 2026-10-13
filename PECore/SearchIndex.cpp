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

int SearchIndex::MatchRank(std::wstring_view name, std::wstring_view folded, std::wstring_view text) {
	if (text.empty())
		return -1;
	auto pos = folded.find(text);
	if (pos == std::wstring_view::npos)
		return -1;
	if (pos == 0)
		return folded.size() == text.size() ? 0 : 1;
	// the last part of a qualified name ("Ns.Type", "Class::Method", "Outer/Inner") is the name itself
	auto last = folded.find_last_of(L".:/\\");
	if (last != std::wstring_view::npos && last + 1 < folded.size() && folded.find(text, last + 1) == last + 1)
		return folded.size() - last - 1 == text.size() ? 0 : 1;
	// a word starts after a character that is not a letter or digit, or at a capital after a small letter (camelCase)
	for (; pos != std::wstring_view::npos; pos = folded.find(text, pos + 1)) {
		if (pos >= name.size())
			break;
		auto prev = name[pos - 1], ch = name[pos];
		if (!iswalnum(prev) || (iswupper(ch) && iswlower(prev)))
			return 2;
	}
	return 3;
}

std::vector<uint32_t> SearchIndex::FindBest(std::wstring_view text, uint32_t kinds, size_t limit) const {
	std::vector<uint32_t> found;
	// the words, the first of which ranks the match
	std::vector<std::wstring> words;
	auto folded = Fold(text);
	for (size_t pos = 0; pos < folded.size(); ) {
		auto end = folded.find(L' ', pos);
		if (end == std::wstring::npos)
			end = folded.size();
		if (end > pos)
			words.push_back(folded.substr(pos, end - pos));
		pos = end + 1;
	}
	if (words.empty())
		return found;

	// the kinds that lead to a place before the others; strings (many, and long) last
	auto order = [](SearchKind kind) {
		switch (kind) {
			case SearchKind::View: return 0;
			case SearchKind::Export: case SearchKind::Symbol: case SearchKind::ClrType: return 1;
			case SearchKind::Import: case SearchKind::ClrMember: return 2;
			case SearchKind::String: return 4;
		}
		return 3;
	};
	struct Match {
		uint32_t Index;
		int Rank, Order;
		size_t Length;
	};
	std::vector<Match> matches;
	for (uint32_t i = 0; i < (uint32_t)m_Items.size(); i++) {
		if ((kinds & SearchKindBit(m_Items[i].Kind)) == 0)
			continue;
		auto rank = MatchRank(m_Items[i].Name, m_Folded[i], words[0]);
		if (rank < 0)
			continue;
		bool all = true;
		for (size_t w = 1; w < words.size() && all; w++)
			all = m_Folded[i].find(words[w]) != std::wstring::npos;
		if (!all)
			continue;
		matches.push_back({ i, words.size() > 1 ? std::max(rank, 1) : rank, order(m_Items[i].Kind), m_Items[i].Name.size() });
	}
	auto better = [](Match const& a, Match const& b) {
		if (a.Rank != b.Rank)
			return a.Rank < b.Rank;
		if (a.Order != b.Order)
			return a.Order < b.Order;
		if (a.Length != b.Length)
			return a.Length < b.Length;
		return a.Index < b.Index;
	};
	if (matches.size() > limit) {
		std::partial_sort(matches.begin(), matches.begin() + limit, matches.end(), better);
		matches.resize(limit);
	}
	else
		std::sort(matches.begin(), matches.end(), better);
	for (auto& m : matches)
		found.push_back(m.Index);
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
		case SearchKind::View: return L"View";
		case SearchKind::ClrType: return L".NET Type";
		case SearchKind::ClrMember: return L".NET Member";
		case SearchKind::Address: return L"Address";
	}
	return L"";
}
