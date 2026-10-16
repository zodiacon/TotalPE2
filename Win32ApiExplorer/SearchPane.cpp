#include "pch.h"
#include "SearchPane.h"
#include "Images.h"
#include <SortHelper.h>

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

	constexpr int KindsWidth = 140, Gap = 4;
}

CSearchPane::CSearchPane(Win32Api& api) : m_Api(&api) {
}

LRESULT CSearchPane::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	auto font = AtlGetDefaultGuiFont();
	m_FilterEdit.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
	m_FilterEdit.SetFont(font);
	m_FilterEdit.SetWatermark(L"Filter by name (Ctrl+F; Esc clears)");

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
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 70);
	cm->AddColumn(L"DLL or Namespace", LVCFMT_LEFT, 180);

	SetApi(*m_Api);
	return 0;
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
	auto selected = SelectedEntry();
	auto name = selected >= 0 ? m_Api->Entries()[selected].Name : std::string();
	m_Api = &api;
	auto const& entries = api.Entries();
	m_LowerNames.clear();
	m_LowerNames.reserve(entries.size());
	for (auto const& e : entries)
		m_LowerNames.push_back(Lower(e.Name));
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	Fill();
	if (auto entry = name.empty() ? nullptr : api.Find(name)) {
		m_Selecting = true;
		SelectEntry(entry - entries.data());
		m_Selecting = false;
	}
}

bool CSearchPane::KindShown(Win32Api::Entry::Kinds kind) const {
	auto const& kinds = s_KindChoices[m_KindFilter].Shown;
	return kinds.size() == 0 || std::ranges::find(kinds, kind) != kinds.end();
}

// The one selected stays selected if it is still there; else none is
void CSearchPane::Fill() {
	auto selected = SelectedEntry();
	auto const& entries = m_Api->Entries();
	m_Items.clear();
	for (size_t i = 0; i < entries.size(); i++) {
		if (!KindShown(entries[i].Kind))
			continue;
		if (!m_Filter.empty() && m_LowerNames[i].find(m_Filter) == std::string::npos)
			continue;
		m_Items.push_back(i);
	}
	m_Selecting = true;
	m_List.SetItemCountEx((int)m_Items.size(), 0);
	Sort(m_List);
	if (auto it = std::ranges::find(m_Items, (size_t)selected); selected >= 0 && it != m_Items.end()) {
		auto row = (int)(it - m_Items.begin());
		m_List.SelectItem(row);
	}
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

CString CSearchPane::GetColumnText(HWND, int row, int col) const {
	auto const& e = m_Api->Entries()[m_Items[row]];
	switch (col) {
		case 0: return CString(e.Name.c_str());
		case 1: return Win32Api::KindName(e.Kind);
		case 2: return CString(e.Where.c_str());
	}
	return CString();
}

int CSearchPane::GetRowImage(HWND, int row, int) const {
	return Images::Of(m_Api->Entries()[m_Items[row]].Kind);
}

void CSearchPane::DoSort(SortInfo const* si) {
	auto const& entries = m_Api->Entries();
	if (si == nullptr) {
		std::ranges::sort(m_Items);	// (the entries are by name)
		return;
	}
	auto asc = si->SortAscending;
	std::stable_sort(m_Items.begin(), m_Items.end(), [&](size_t a, size_t b) {
		auto& x = entries[a];
		auto& y = entries[b];
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(x.Name, y.Name, asc);
			case 1: return SortHelper::Sort((int)x.Kind, (int)y.Kind, asc);
			case 2: return SortHelper::Sort(x.Where, y.Where, asc);
		}
		return false;
	});
}

int CSearchPane::SelectedEntry() const {
	auto row = m_List.GetSelectedIndex();
	return row >= 0 && row < (int)m_Items.size() ? (int)m_Items[row] : -1;
}

bool CSearchPane::SelectEntry(size_t entry) {
	auto it = std::ranges::find(m_Items, entry);
	if (it == m_Items.end()) {
		// hidden by the filter: all are shown again
		m_Filter.clear();
		m_KindFilter = 0;
		m_Kinds.SetCurSel(0);
		m_FilterEdit.SetWindowText(L"");
		Fill();
		it = std::ranges::find(m_Items, entry);
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

LRESULT CSearchPane::OnItemChanged(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	auto lv = (NMLISTVIEW*)hdr;
	if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !m_Selecting && OnOpen)
		if (auto entry = SelectedEntry(); entry >= 0)
			OnOpen(entry, false);
	return 0;
}

bool CSearchPane::OnDoubleClickList(HWND, int row, int, CPoint const&) {
	if (row < 0 || row >= (int)m_Items.size() || !OnOpen)
		return false;
	OnOpen(m_Items[row], true);
	return true;
}

LRESULT CSearchPane::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_FilterEdit) {
		handled = FALSE;
		return 0;
	}
	CString text;
	m_FilterEdit.GetWindowText(text);
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
		if (auto entry = SelectedEntry(); entry >= 0 && OnOpen)
			OnOpen(entry, true);
		return TRUE;
	}
	return FALSE;
}
