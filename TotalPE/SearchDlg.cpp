#include "pch.h"
#include "SearchDlg.h"
#include <SortHelper.h>

void CSearchDlg::Show(HWND hOwner) {
	if (!IsWindow())
		Create(hOwner);
	ShowWindow(SW_SHOW);
	SetActiveWindow();
	auto edit = GetDlgItem(IDC_SEARCH_TEXT);
	edit.SetFocus();
	edit.SendMessage(EM_SETSEL, 0, -1);
}

void CSearchDlg::Invalidate() {
	m_Results.clear();
	m_Index = nullptr;
	if (!IsWindow())
		return;
	m_List.SetItemCount(0);
	if (IsWindowVisible())
		Search();
}

LRESULT CSearchDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	DlgResize_Init(true);
	m_List.Attach(GetDlgItem(IDC_SEARCH_RESULTS));
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	// the list shares the image list of the tree (LVS_SHAREIMAGELISTS)
	m_List.SetImageList(m_Host->GetImageList(), LVSIL_SMALL);
	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 100, Kind);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 380, Name);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 220, Details);
	cm->AddColumn(L"Location", LVCFMT_LEFT, 150, Location);
	for (int id = IDC_SEARCH_IMPORTS; id <= IDC_SEARCH_SECTIONS; id++)
		CheckDlgButton(id, BST_CHECKED);
	return TRUE;
}

LRESULT CSearchDlg::OnSearchAgain(UINT, WPARAM, LPARAM, BOOL&) {
	Search();
	return 0;
}

LRESULT CSearchDlg::OnShowWindow(UINT, WPARAM show, LPARAM, BOOL& handled) {
	// the file may have changed while the dialog was hidden: the last search is made again
	if (show && m_Index == nullptr)
		PostMessage(WM_SEARCH_AGAIN);
	handled = FALSE;
	return 0;
}

LRESULT CSearchDlg::OnOptionChanged(WORD, WORD, HWND, BOOL&) {
	Search();
	return 0;
}

// Search (the default button, so Enter too): Enter in the results shows the selected one instead
LRESULT CSearchDlg::OnOK(WORD, WORD, HWND, BOOL&) {
	if (::GetFocus() == m_List) {
		GoTo(m_List.GetNextItem(-1, LVNI_SELECTED));
		return 0;
	}
	Search();
	return 0;
}

// the dialog is hidden, not destroyed: the search is there when it is shown again
LRESULT CSearchDlg::OnCancel(WORD, WORD, HWND, BOOL&) {
	ShowWindow(SW_HIDE);
	return 0;
}

uint32_t CSearchDlg::SelectedKinds() const {
	uint32_t kinds = 0;
	if (IsDlgButtonChecked(IDC_SEARCH_IMPORTS))
		kinds |= SearchKindBit(SearchKind::Import) | SearchKindBit(SearchKind::ImportModule);
	if (IsDlgButtonChecked(IDC_SEARCH_EXPORTS))
		kinds |= SearchKindBit(SearchKind::Export);
	if (IsDlgButtonChecked(IDC_SEARCH_STRINGS))
		kinds |= SearchKindBit(SearchKind::String);
	if (IsDlgButtonChecked(IDC_SEARCH_RESOURCES))
		kinds |= SearchKindBit(SearchKind::Resource);
	if (IsDlgButtonChecked(IDC_SEARCH_SYMBOLS))
		kinds |= SearchKindBit(SearchKind::Symbol);
	if (IsDlgButtonChecked(IDC_SEARCH_SECTIONS))
		kinds |= SearchKindBit(SearchKind::Section);
	return kinds;
}

void CSearchDlg::Search() {
	CString text;
	GetDlgItemText(IDC_SEARCH_TEXT, text);
	m_Results.clear();
	m_List.SetItemCount(0);
	if (text.IsEmpty()) {
		SetDlgItemText(IDC_SEARCH_STATUS, L"Type the text to search for, then click Search (or press Enter)");
		return;
	}

	CWaitCursor wait;
	if (m_Index == nullptr) {
		SetDlgItemText(IDC_SEARCH_STATUS, L"Collecting the items of the file...");
		UpdateWindow();
	}
	m_Index = &m_Host->GetSearchIndex();
	bool truncated;
	m_Results = m_Index->Find((PCWSTR)text, IsDlgButtonChecked(IDC_SEARCH_MATCHCASE), SelectedKinds(), MaxResults, &truncated);
	if (auto si = GetSortInfo(m_List); si && si->SortColumn >= 0)
		DoSort(si);
	m_List.SetItemCount((int)m_Results.size());
	if (!m_Results.empty())
		m_List.SetItemState(0, LVIS_FOCUSED, LVIS_FOCUSED);

	std::wstring status = m_Results.empty() ? L"Nothing found" : std::format(L"{} found", m_Results.size());
	if (truncated)
		status = std::format(L"The first {} found are shown", MaxResults);
	status += std::format(L" (of {} items). Double-click a result to show it.", m_Index->Size());
	SetDlgItemText(IDC_SEARCH_STATUS, status.c_str());
}

CString CSearchDlg::GetColumnText(HWND, int row, int col) const {
	if (m_Index == nullptr || row < 0 || row >= (int)m_Results.size())
		return CString();
	auto const& item = (*m_Index)[m_Results[row]];
	switch (GetColumnManager(m_List)->GetColumnTag<ColumnType>(col)) {
		case Kind: return SearchIndex::KindName(item.Kind);
		case Name: return item.Name.c_str();
		case Details: return item.Details.c_str();
		case Location: return item.Location.c_str();
	}
	return CString();
}

int CSearchDlg::GetRowImage(HWND, int row, int) const {
	if (m_Index == nullptr || row < 0 || row >= (int)m_Results.size())
		return -1;
	return (*m_Index)[m_Results[row]].Image;
}

void CSearchDlg::DoSort(SortInfo const* si) {
	if (m_Index == nullptr || si == nullptr)
		return;
	auto const& index = *m_Index;
	auto tag = GetColumnManager(m_List)->GetColumnTag<ColumnType>(si->SortColumn);
	auto asc = si->SortAscending;
	std::ranges::stable_sort(m_Results, [&](uint32_t a, uint32_t b) {
		auto const& x = index[a], & y = index[b];
		switch (tag) {
			case Kind: return SortHelper::Sort(std::wstring(SearchIndex::KindName(x.Kind)), std::wstring(SearchIndex::KindName(y.Kind)), asc);
			case Name: return SortHelper::Sort(x.Name, y.Name, asc);
			case Details: return SortHelper::Sort(x.Details, y.Details, asc);
			// the locations of a kind are numbers of the same sort
			case Location: return x.Kind != y.Kind ? SortHelper::Sort((int)x.Kind, (int)y.Kind, asc) : SortHelper::Sort(x.Value, y.Value, asc);
		}
		return false;
	});
}

bool CSearchDlg::OnDoubleClickList(HWND, int row, int, CPoint const&) {
	return GoTo(row);
}

bool CSearchDlg::GoTo(int row) {
	if (m_Index == nullptr || row < 0 || row >= (int)m_Results.size())
		return false;
	auto const& item = (*m_Index)[m_Results[row]];
	if (!m_Host->GoToSearchItem(item)) {
		::MessageBeep(MB_ICONWARNING);
		SetDlgItemText(IDC_SEARCH_STATUS, std::format(L"{} \"{}\" has no place to show", SearchIndex::KindName(item.Kind), item.Name).c_str());
		return false;
	}
	return true;
}
