#include "pch.h"
#include "SearchPane.h"
#include "Images.h"
#include <SortHelper.h>
#include "Values.h"

namespace {
	using Kinds = Win32Api::Entry::Kinds;

	// the choices of kinds (the first is all of them)
	struct KindChoice {
		PCWSTR Text;
		std::vector<Kinds> Shown;	// (empty: all)
	};
	KindChoice const s_KindChoices[] = {
		{ L"All", {} },
		{ L"Functions", { Kinds::Function } },
		{ L"Types", { Kinds::Struct, Kinds::Union, Kinds::Enum, Kinds::Typedef, Kinds::Callback, Kinds::Interface } },
		{ L"Structs and Unions", { Kinds::Struct, Kinds::Union } },
		{ L"Enums", { Kinds::Enum } },
		{ L"Interfaces", { Kinds::Interface } },
		{ L"Callbacks", { Kinds::Callback } },
		{ L"Typedefs", { Kinds::Typedef } },
		{ L"Constants", { Kinds::Constant } },
	};

	std::string Lower(std::string text) {
		for (auto& c : text)
			c = (char)tolower((unsigned char)c);
		return text;
	}

	// a GUID, with its braces or without
	std::optional<GUID> ParseGuid(std::wstring const& text) {
		auto s = Values::Trimmed(text);
		if (s.starts_with(L'{') && s.ends_with(L'}'))
			s = s.substr(1, s.size() - 2);
		if (s.size() != 36 || s[8] != L'-' || s[13] != L'-' || s[18] != L'-' || s[23] != L'-')
			return std::nullopt;
		GUID guid;
		if (FAILED(::IIDFromString((L"{" + s + L"}").c_str(), &guid)))
			return std::nullopt;
		return guid;
	}

	constexpr int KindsWidth = 140, Gap = 4;
}

CSearchPane::CSearchPane(Win32Api& api) : m_Api(&api) {
}

namespace {
	// a name by a pattern: * any letters, ? one
	bool Like(std::string_view text, std::string_view pattern) {
		size_t t = 0, p = 0, star = std::string_view::npos, mark = 0;
		while (t < text.size()) {
			if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
				t++;
				p++;
			}
			else if (p < pattern.size() && pattern[p] == '*') {
				star = p++;
				mark = t;
			}
			else if (star != std::string_view::npos) {
				p = star + 1;
				t = ++mark;
			}
			else
				return false;
		}
		while (p < pattern.size() && pattern[p] == '*')
			p++;
		return p == pattern.size();
	}
}

CSearchPane::Query CSearchPane::ParseQuery(std::string const& filter) {
	Query query;
	size_t start = 0;
	while (start < filter.size()) {
		auto end = filter.find(' ', start);
		if (end == std::string::npos)
			end = filter.size();
		auto term = filter.substr(start, end - start);
		start = end + 1;
		if (term.empty())
			continue;
		if (term.starts_with("dll:")) {
			auto dll = term.substr(4);
			if (dll.ends_with(".dll"))
				dll.resize(dll.size() - 4);
			if (!dll.empty())
				query.Dlls.push_back(dll);
			continue;
		}
		if (term.starts_with("ns:")) {
			if (term.size() > 3)
				query.Namespaces.push_back(term.substr(3));
			continue;
		}
		bool first = term.starts_with('^'), last = term.size() > 1 && term.ends_with('$');
		if (first)
			term = term.substr(1);
		if (last)
			term.pop_back();
		if (term.empty())
			continue;
		using Kinds = Term::Kinds;
		auto kind = term.find_first_of("*?") != std::string::npos ? Kinds::Pattern :
			first && last ? Kinds::All : first ? Kinds::Start : last ? Kinds::End : Kinds::Part;
		query.Names.push_back({ kind, term });
	}
	return query;
}

bool CSearchPane::Matches(Query const& query, size_t entry) const {
	auto const& e = m_Api->Entries()[entry];
	for (auto const& dll : query.Dlls)
		if (e.Kind != Kinds::Function || m_LowerWhere[entry].find(dll) == std::string::npos)
			return false;
	for (auto const& ns : query.Namespaces)
		if (m_LowerNamespaces[entry].find(ns) == std::string::npos)
			return false;
	auto const& name = m_LowerNames[entry];
	for (auto const& term : query.Names) {
		bool ok = false;
		switch (term.Kind) {
			case Term::Kinds::Part: ok = name.find(term.Text) != std::string::npos; break;
			case Term::Kinds::Start: ok = name.starts_with(term.Text); break;
			case Term::Kinds::End: ok = name.ends_with(term.Text); break;
			case Term::Kinds::All: ok = name == term.Text; break;
			case Term::Kinds::Pattern: ok = Like(name, term.Text); break;
		}
		if (!ok)
			return false;
	}
	return true;
}

LRESULT CSearchPane::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	auto font = AtlGetDefaultGuiFont();
	m_FilterEdit.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
	m_FilterEdit.SetFont(font);
	UpdateWatermark();

	CRect rc(0, 0, KindsWidth, 300);
	m_Kinds.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
	m_Kinds.SetFont(font);
	for (auto const& choice : s_KindChoices)
		m_Kinds.AddString(choice.Text);
	m_Kinds.SetCurSel(0);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 250);
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 80);
	cm->AddColumn(L"DLL, Namespace or Enum", LVCFMT_LEFT, 180);

	SetApi(*m_Api);
	return 0;
}

void CSearchPane::UpdateWatermark() {
	if (m_Scope)
		m_FilterEdit.SetWatermark(std::format(L"Filter {} by name (Esc clears)", m_ScopeDescription).c_str());
	else
		m_FilterEdit.SetWatermark(L"Name (^start, end$, *), dll:, ns:, a value or a GUID (Ctrl+F)");
}

void CSearchPane::SetUIFont(HFONT font) {
	m_FilterEdit.SetFont(font);
	m_Kinds.SetFont(font);
	m_List.SetFont(font);
	UpdateLayout();	// (the list of kinds is as high as its font)
}

void CSearchPane::SetImageList(HIMAGELIST images) {
	m_List.SetImageList(images, LVSIL_SMALL);
}

void CSearchPane::UpdateLayout() {
	CRect rc;
	GetClientRect(&rc);
	CRect combo;
	m_Kinds.GetWindowRect(&combo);
	auto height = combo.Height();
	m_FilterEdit.SetWindowPos(nullptr, Gap, Gap, std::max(0, rc.Width() - KindsWidth - 3 * Gap), height, SWP_NOZORDER | SWP_NOACTIVATE);
	m_Kinds.SetWindowPos(nullptr, rc.Width() - KindsWidth - Gap, Gap, KindsWidth, 300, SWP_NOZORDER | SWP_NOACTIVATE);
	auto top = height + 2 * Gap;
	m_List.SetWindowPos(nullptr, 0, top, rc.Width(), std::max(0, rc.Height() - top), SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CSearchPane::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateLayout();
	return 0;
}

LRESULT CSearchPane::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_List.SetFocus();
	return 0;
}

void CSearchPane::FocusFilter() {
	m_FilterEdit.SetFocus();
	m_FilterEdit.SetSelAll();
}

void CSearchPane::SetApi(Win32Api& api) {
	auto selected = SelectedItem();
	auto name = selected ? m_Api->Entries()[selected->Entry].Name : std::string();
	m_Api = &api;
	auto const& entries = api.Entries();
	m_LowerNames.clear();
	m_LowerWhere.clear();
	m_LowerNamespaces.clear();
	m_LowerNames.reserve(entries.size());
	m_LowerWhere.reserve(entries.size());
	m_LowerNamespaces.reserve(entries.size());
	for (auto const& e : entries) {
		m_LowerNames.push_back(Lower(e.Name));
		m_LowerWhere.push_back(Lower(e.Where));
		m_LowerNamespaces.push_back(Lower(e.Namespace));
	}
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	Fill();
	if (auto entry = name.empty() ? nullptr : api.Find(name)) {
		m_Selecting = true;
		SelectEntry(entry - entries.data());
		m_Selecting = false;
	}
}

void CSearchPane::SetScope(std::vector<size_t> const* entries, std::wstring const& description) {
	m_Scope = entries;
	m_ScopeDescription = description;
	UpdateWatermark();
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	ClearFilter();
}

void CSearchPane::ClearFilter() {
	m_Filter.clear();
	m_FilterText.clear();
	m_KindFilter = 0;
	m_Kinds.SetCurSel(0);
	m_FilterEdit.SetWindowText(L"");
	Fill();
}

bool CSearchPane::KindShown(Win32Api::Entry::Kinds kind) const {
	auto const& kinds = s_KindChoices[m_KindFilter].Shown;
	return kinds.size() == 0 || std::ranges::find(kinds, kind) != kinds.end();
}

// The one selected stays selected if it is still there; else none is
void CSearchPane::Fill() {
	std::optional<Item> selected;
	if (auto item = SelectedItem())
		selected = *item;
	auto const& entries = m_Api->Entries();
	m_Items.clear();
	auto add = [&](size_t entry, std::string name = {}) {
		if (KindShown(entries[entry].Kind))
			m_Items.push_back({ entry, std::move(name) });
	};
	auto query = ParseQuery(m_Filter);
	bool all = query.Dlls.empty() && query.Namespaces.empty() && query.Names.empty();
	auto byName = [&](size_t entry) {
		return all || Matches(query, entry);
	};
	if (m_Scope) {
		for (auto entry : *m_Scope)
			if (byName(entry))
				add(entry);
	}
	else if (auto value = Values::Parse(m_FilterText)) {
		// (a constant by its own name; a value of an enum by its name)
		for (auto& v : m_Api->FindValue(*value))
			add(v.Entry, v.Name == entries[v.Entry].Name ? std::string() : std::move(v.Name));
	}
	else if (auto guid = ParseGuid(m_FilterText)) {
		if (auto named = m_Api->FindGuid(*guid))
			add(named->Entry, named->Name == entries[named->Entry].Name ? std::string() : named->Name);
	}
	else {
		for (size_t i = 0; i < entries.size(); i++)
			if (byName(i))
				add(i);
	}
	m_Selecting = true;
	m_List.SetItemCountEx((int)m_Items.size(), 0);
	Sort(m_List);
	if (auto it = selected ? std::ranges::find(m_Items, *selected) : m_Items.end(); it != m_Items.end())
		m_List.SelectItem((int)(it - m_Items.begin()));
	else
		m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_Selecting = false;
	m_List.RedrawItems(0, m_List.GetItemCount());
	if (OnCountChanged)
		OnCountChanged();
}

size_t CSearchPane::GetShownCount() const {
	return m_Items.size();
}

size_t CSearchPane::GetTotalCount() const {
	return m_Scope ? m_Scope->size() : m_Api->Entries().size();
}

std::string const& CSearchPane::NameOf(Item const& item) const {
	return item.Name.empty() ? m_Api->Entries()[item.Entry].Name : item.Name;
}

CString CSearchPane::GetColumnText(HWND, int row, int col) const {
	auto const& item = m_Items[row];
	auto const& e = m_Api->Entries()[item.Entry];
	switch (col) {
		case 0: return CString(NameOf(item).c_str());
		// (a value of an enum, a GUID's name: of the entry)
		case 1: return !item.Name.empty() && e.Kind == Kinds::Enum ? L"Enum value" : Win32Api::KindName(e.Kind);
		case 2: return CString((item.Name.empty() ? e.Where : e.Name).c_str());
	}
	return CString();
}

int CSearchPane::GetRowImage(HWND, int row, int) const {
	auto const& item = m_Items[row];
	auto kind = m_Api->Entries()[item.Entry].Kind;
	return !item.Name.empty() && kind == Kinds::Enum ? Images::Constant : Images::Of(kind);
}

void CSearchPane::DoSort(SortInfo const* si) {
	auto const& entries = m_Api->Entries();
	if (si == nullptr) {
		// (the entries are by name; the values and GUIDs are found by name)
		// (a scope's are in its order: of history, the last first)
		if (!m_Scope && (m_Items.empty() || m_Items[0].Name.empty()))
			std::ranges::sort(m_Items, {}, &Item::Entry);
		return;
	}
	auto asc = si->SortAscending;
	std::stable_sort(m_Items.begin(), m_Items.end(), [&](Item const& a, Item const& b) {
		auto& x = entries[a.Entry];
		auto& y = entries[b.Entry];
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(NameOf(a), NameOf(b), asc);
			case 1: return SortHelper::Sort((int)x.Kind, (int)y.Kind, asc);
			case 2: return SortHelper::Sort(a.Name.empty() ? x.Where : x.Name, b.Name.empty() ? y.Where : y.Name, asc);
		}
		return false;
	});
}

CSearchPane::Item const* CSearchPane::SelectedItem() const {
	auto row = m_List.m_hWnd ? m_List.GetSelectedIndex() : -1;
	return row >= 0 && row < (int)m_Items.size() ? &m_Items[row] : nullptr;
}

bool CSearchPane::SelectEntry(size_t entry) {
	auto find = [&] { return std::ranges::find(m_Items, entry, &Item::Entry); };
	auto it = find();
	if (it == m_Items.end()) {
		// hidden by the filter: all are shown again
		ClearFilter();
		it = find();
		if (it == m_Items.end())
			return false;
	}
	auto row = (int)(it - m_Items.begin());
	auto selecting = m_Selecting;
	m_Selecting = true;
	m_List.SelectItem(row);
	m_List.EnsureVisible(row, FALSE);
	m_Selecting = selecting;
	return true;
}

void CSearchPane::Open(Item const& item, bool pin) {
	if (OnOpen) {
		// (copies: what is opened may fill the list again)
		auto entry = item.Entry;
		auto name = item.Name;
		OnOpen(entry, pin, name);
	}
}

LRESULT CSearchPane::OnItemChanged(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	auto lv = (NMLISTVIEW*)hdr;
	if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !m_Selecting)
		if (auto item = SelectedItem())
			Open(*item, false);
	return 0;
}

bool CSearchPane::OnDoubleClickList(HWND, int row, int, CPoint const&) {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	Open(m_Items[row], true);
	return true;
}

LRESULT CSearchPane::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_FilterEdit) {
		handled = FALSE;
		return 0;
	}
	CString text;
	m_FilterEdit.GetWindowText(text);
	m_FilterText = (PCWSTR)text;
	m_Filter = Lower(std::string(CW2A(text)));
	Fill();
	return 0;
}

LRESULT CSearchPane::OnKindChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Kinds) {
		handled = FALSE;
		return 0;
	}
	m_KindFilter = std::max(0, m_Kinds.GetCurSel());
	Fill();
	return 0;
}

BOOL CSearchPane::PreTranslateMessage(MSG* pMsg) {
	if (pMsg->message != WM_KEYDOWN || !m_hWnd)
		return FALSE;
	bool control = ::GetKeyState(VK_CONTROL) < 0, alt = ::GetKeyState(VK_MENU) < 0;
	// from the filter to the list (its first row, if none is selected)
	if (pMsg->hwnd == m_FilterEdit && !control && !alt && (pMsg->wParam == VK_DOWN || pMsg->wParam == VK_RETURN) && !m_Items.empty()) {
		if (m_List.GetSelectedIndex() < 0)
			m_List.SelectItem(0);
		m_List.SetFocus();
		return TRUE;
	}
	// Enter in the list: in a tab that stays
	if (pMsg->hwnd == m_List && pMsg->wParam == VK_RETURN && !alt) {
		if (auto item = SelectedItem())
			Open(*item, true);
		return TRUE;
	}
	return FALSE;
}
