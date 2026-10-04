#include "pch.h"
#include "CompareView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>
#include <ToolbarHelper.h>

CCompareView::CCompareView(IMainFrame* frame, ISearchHost* host, PEDiff diff, std::wstring otherPath)
	: CViewBase(frame), m_Host(host), m_Diff(std::move(diff)), m_OtherPath(std::move(otherPath)) {
}

CString CCompareView::GetTitle() const {
	return CString(L"Compare: ") + m_OtherPath.substr(m_OtherPath.rfind(L'\\') + 1).c_str();
}

CString CCompareView::GetColumnText(HWND, int row, int col) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return CString();
	auto const& item = m_Diff.Items[m_Rows[row]];
	switch (GetColumnManager(m_List)->GetColumnTag<Column>(col)) {
		case Category: return PEDiff::CategoryName(item.Category);
		case Name: return item.Name.c_str();
		case Status: return PEDiff::StatusName(item.Status);
		case Left: return item.Left.c_str();
		case Right: return item.Right.c_str();
		case Details: return item.Details.c_str();
	}
	return CString();
}

// the icons the items have in the tree
int CCompareView::GetRowImage(HWND, int row, int) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return -1;
	auto const& item = m_Diff.Items[m_Rows[row]];
	UINT icon = 0;
	switch (item.Category) {
		case DiffCategory::File: return 0;
		case DiffCategory::Header: icon = IDI_HEADERS; break;
		case DiffCategory::Directory: icon = IDI_DIRS; break;
		case DiffCategory::Section: icon = IDI_SECTION; break;
		case DiffCategory::Import: icon = item.Name.find(L'!') == std::wstring::npos ? IDI_DLL_IMPORT : IDI_IMPORTS; break;
		case DiffCategory::Export: icon = IDI_EXPORTS; break;
		case DiffCategory::Resource: icon = IDI_RESOURCE; break;
		case DiffCategory::Version: icon = IDI_VERSION; break;
		case DiffCategory::Debug: icon = IDI_DEBUG; break;
	}
	return Frame()->GetIconIndex(icon);
}

void CCompareView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;
	auto tag = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto asc = si->SortAscending;
	std::ranges::stable_sort(m_Rows, [&](int a, int b) {
		auto const& x = m_Diff.Items[a], & y = m_Diff.Items[b];
		switch (tag) {
			case Category: return SortHelper::Sort((int)x.Category, (int)y.Category, asc);
			case Name: return SortHelper::Sort(x.Name, y.Name, asc);
			case Status: return SortHelper::Sort((int)x.Status, (int)y.Status, asc);
			case Left: return SortHelper::Sort(x.Left, y.Left, asc);
			case Right: return SortHelper::Sort(x.Right, y.Right, asc);
			case Details: return SortHelper::Sort(x.Details, y.Details, asc);
		}
		return false;
	});
}

// the places of the items are those Search All goes to
bool CCompareView::GoTo(DiffLocation const& where) const {
	SearchKind kind;
	switch (where.Place) {
		case DiffPlace::Section: kind = SearchKind::Section; break;
		case DiffPlace::ImportModule: kind = SearchKind::ImportModule; break;
		case DiffPlace::Import: kind = SearchKind::Import; break;
		case DiffPlace::Export: kind = SearchKind::Export; break;
		case DiffPlace::Resource: kind = SearchKind::Resource; break;
		default: return false;
	}
	return m_Host->GoToSearchItem({ .Kind = kind, .Name = where.Name, .Value = where.Value, .Index = where.Index, .Owner = where.Owner });
}

bool CCompareView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	auto const& item = m_Diff.Items[m_Rows[row]];
	if (!GoTo(item.Where)) {
		::MessageBeep(MB_ICONWARNING);
		Frame()->SetStatusText(1, item.Status == DiffStatus::Added ? L"The item is only in the other file" : L"The item has no place to show");
		return false;
	}
	return true;
}

void CCompareView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CCompareView::UpdateUI(bool) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	auto name = m_OtherPath.substr(m_OtherPath.rfind(L'\\') + 1);
	Frame()->SetStatusText(1, m_Diff.Identical ? std::format(L"The files are identical").c_str() :
		std::format(L"{} changed, {} added (only in {}), {} removed (only in this file), {} the same", m_Diff.Count(DiffStatus::Changed),
			m_Diff.Count(DiffStatus::Added), name, m_Diff.Count(DiffStatus::Removed), m_Diff.Count(DiffStatus::Same)).c_str());
}

void CCompareView::ApplyFilter() {
	CString filter(m_FilterText);
	filter.MakeLower();
	m_Rows.clear();
	for (int i = 0; i < (int)m_Diff.Items.size(); i++) {
		auto const& item = m_Diff.Items[i];
		if (!m_ShowSame && item.Status == DiffStatus::Same)
			continue;
		if (!filter.IsEmpty()) {
			CString text((item.Name + L"\t" + item.Left + L"\t" + item.Right + L"\t" + item.Details).c_str());
			text.MakeLower();
			if (text.Find(filter) < 0)
				continue;
		}
		m_Rows.push_back(i);
	}
	if (auto si = GetSortInfo(m_List); si && si->SortColumn >= 0)
		DoSort(si);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

LRESULT CCompareView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	ToolBarButtonInfo const buttons[] = {
		{ ID_COMPARE_SHOWSAME, 0, BTNS_CHECK | BTNS_SHOWTEXT, L"Show Unchanged" },
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 16);
	AddSimpleReBarBand(m_tb);
	UIAddToolBar(m_tb);

	CRect rc(0, 0, 250, 20);
	m_Filter.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
	m_Filter.SetFont(AtlGetDefaultGuiFont());
	m_Filter.SetWatermark(L"Filter (Esc to clear)");
	AddSimpleReBarBand(m_Filter, nullptr, FALSE, 250);

	// the band of the toolbar is as wide as its button, so the filter comes after it
	CRect rcLast;
	m_tb.GetItemRect(m_tb.GetButtonCount() - 1, &rcLast);
	CReBarCtrl rebar(m_hWndToolBar);
	REBARBANDINFO band{ sizeof(band) };
	band.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
	rebar.GetBandInfo(0, &band);
	band.cxMinChild = band.cxIdeal = rcLast.right;
	band.cx = rcLast.right + 16;
	rebar.SetBandInfo(0, &band);

	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Category", LVCFMT_LEFT, 120, Category);
	cm->AddColumn(L"Item", LVCFMT_LEFT, 300, Name);
	cm->AddColumn(L"Status", LVCFMT_LEFT, 70, Status);
	cm->AddColumn(L"This File", LVCFMT_LEFT, 300, Left);
	cm->AddColumn(L"Other File", LVCFMT_LEFT, 300, Right);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 250, Details);

	// a comparison of a file with itself would be empty: everything is shown then
	m_ShowSame = m_Diff.Identical;
	UISetCheck(ID_COMPARE_SHOWSAME, m_ShowSame);
	ApplyFilter();
	return 0;
}

// the differences in colors: added in green, removed in red, changed in blue
LRESULT CCompareView::OnCustomDraw(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	auto cd = (NMLVCUSTOMDRAW*)hdr;
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (int)cd->nmcd.dwItemSpec;
			if (row < 0 || row >= (int)m_Rows.size())
				break;
			switch (m_Diff.Items[m_Rows[row]].Status) {
				case DiffStatus::Added: cd->clrText = RGB(0, 160, 0); break;
				case DiffStatus::Removed: cd->clrText = RGB(220, 40, 40); break;
				case DiffStatus::Changed: cd->clrText = RGB(30, 120, 230); break;
				default: return CDRF_DODEFAULT;
			}
			return CDRF_NEWFONT;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CCompareView::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Filter) {
		handled = FALSE;
		return 0;
	}
	m_Filter.GetWindowText(m_FilterText);
	ApplyFilter();
	return 0;
}

LRESULT CCompareView::OnShowSame(WORD, WORD, HWND, BOOL&) {
	m_ShowSame = !m_ShowSame;
	UISetCheck(ID_COMPARE_SHOWSAME, m_ShowSame);
	ApplyFilter();
	return 0;
}

LRESULT CCompareView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr || m_List.GetItemCount() == 0)
		return 0;
	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());
	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

LRESULT CCompareView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List));
	return 0;
}
