#include "pch.h"
#include "ResourcesView.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include "resource.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>

CResourcesView::CResourcesView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe), m_HexView(frame) {
}

CString CResourcesView::GetTitle() const {
	return L"Resources";
}

CString CResourcesView::GetColumnText(HWND h, int row, int col) const {
	auto& res = m_Resources[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return res.Name.c_str();
		case ColumnType::Type: return res.Type.c_str();
		case ColumnType::Size: return PEStrings::ToMemorySize(res.Data.size()).c_str();
		case ColumnType::Language: return res.Language.c_str();
	}
	return CString();
}

int CResourcesView::GetRowImage(HWND, int row, int) const {
	return Frame()->GetResourceIconIndex(m_Resources[row].TypeID);
}

void CResourcesView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto compare = [&](auto& r1, auto& r2) {
		switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
			case ColumnType::Name: return SortHelper::Sort(r1.Name, r2.Name, asc);
			case ColumnType::Size: return SortHelper::Sort(r1.Data.size(), r2.Data.size(), asc);
			case ColumnType::Type: return SortHelper::Sort(r1.Type, r2.Type, asc);
			case ColumnType::Language: return SortHelper::Sort(r1.Language, r2.Language, asc);
		}
		return false;
	};
	std::ranges::sort(m_Resources, compare);
}

void CResourcesView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	if (newState & LVIS_SELECTED) {
		int selected = m_List.GetSelectedCount();
		if (selected == 1) {
			auto const& res = m_Resources[m_List.GetNextItem(-1, LVNI_SELECTED)];
			m_HexView.SetData(res.Data);
		}
		else {
			m_HexView.ClearData();
		}
	}
	UpdateUI();
}

void CResourcesView::BuildItems() {
	m_Resources = Frame()->GetFlatResources();
	m_List.SetItemCount((int)m_Resources.size());
}

void CResourcesView::UpdateUI(bool first) {
	auto& ui = Frame()->GetUI();
	auto pane = m_Splitter.GetActivePane();
	ui.UIEnable(ID_EDIT_COPY, (pane == 0 && m_List.GetSelectedCount() > 0) || (pane == 1 && m_HexView.Hex().HasSelection()));
}

LRESULT CResourcesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Type", LVCFMT_RIGHT, 170, ColumnType::Type);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 120, ColumnType::Name);
	cm->AddColumn(L"Language", LVCFMT_RIGHT, 90, ColumnType::Language);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 100, ColumnType::Size);
//	cm->AddColumn(L"RVA", LVCFMT_LEFT, 90, ColumnType::RVA);

	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
	m_HexView.SetStatic(true);

	BuildItems();

	m_Splitter.SetSplitterPanes(m_List, m_HexView);
	m_Splitter.SetSplitterPosPct(50);

	return 0;
}

LRESULT CResourcesView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
}

LRESULT CResourcesView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());

	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else {
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	}
	return 0;
}
