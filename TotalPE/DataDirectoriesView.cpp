#include "pch.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include "resource.h"
#include "DataDirectoriesView.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>

CString CDataDirectoriesView::GetTitle() const {
    return L"Data Directories";
}

CDataDirectoriesView::CDataDirectoriesView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe), m_HexView(frame) {
}

CString CDataDirectoriesView::GetColumnText(HWND h, int row, int col) const {
	auto& dir = m_Directories[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return PEStrings::GetDataDirectoryName(dir.Index);
		case ColumnType::Section: return dir.Section.c_str();
		case ColumnType::Size: return PEStrings::ToMemorySize(dir.DataDir.Size).c_str();
		case ColumnType::Index: return std::to_wstring(dir.Index).c_str();
		case ColumnType::Address: return std::format(L"0x{:X}", dir.DataDir.VirtualAddress).c_str();
	}
	return CString();
}

int CDataDirectoriesView::GetRowImage(HWND, int row, int) const {
	return Frame()->GetDataDirectoryIconIndex(m_Directories[row].Index);
}

void CDataDirectoriesView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto compare = [&](auto& d1, auto& d2) {
		switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
			case ColumnType::Address: return SortHelper::Sort(d1.DataDir.VirtualAddress, d2.DataDir.VirtualAddress, asc);
			case ColumnType::Size: return SortHelper::Sort(d1.DataDir.Size, d2.DataDir.Size, asc);
			case ColumnType::Index: return SortHelper::Sort(d1.Index, d2.Index, asc);
			case ColumnType::Name: return SortHelper::Sort(PEStrings::GetDataDirectoryName(d1.Index), PEStrings::GetDataDirectoryName(d2.Index), asc);
			case ColumnType::Section: return SortHelper::Sort(d1.Section, d2.Section, asc);
		}
		return false;
	};
	std::ranges::sort(m_Directories, compare);
}

void CDataDirectoriesView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	if (newState & LVIS_SELECTED) {
		int selected = m_List.GetSelectedCount();
		if (selected == 1) {
			auto const& dir = m_Directories[m_List.GetNextItem(-1, LVNI_SELECTED)];
			auto offset = m_PE->GetOffsetFromRVA(dir.DataDir.VirtualAddress);
			m_HexView.SetData(m_PE, offset, dir.DataDir.Size);
		}
		else {
			m_HexView.ClearData();
		}
	}
	UpdateUI();
}

void CDataDirectoriesView::BuildItems() {
	int i = 0;
	for (auto& dir : *m_PE->GetDataDirs()) {
		if (dir.DataDir.Size) {
			DataDirectory dd(dir);
			dd.Index = i;
			m_Directories.push_back(std::move(dd));
		}
		i++;
	}
	m_List.SetItemCount((int)m_Directories.size());
}

void CDataDirectoriesView::UpdateUI() {
	auto& ui = Frame()->GetUI();
	auto pane = m_Splitter.GetActivePane();
	ui.UIEnable(ID_EDIT_COPY, (pane == 0 && m_List.GetSelectedCount() > 0) || (pane == 1 && m_HexView.Hex().HasSelection()));
}

LRESULT CDataDirectoriesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | 
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 170, ColumnType::Name);
	cm->AddColumn(L"Index", LVCFMT_RIGHT, 60, ColumnType::Index);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 90, ColumnType::Address);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 110, ColumnType::Size);
	cm->AddColumn(L"Section", LVCFMT_LEFT, 90, ColumnType::Section);

	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
	m_HexView.SetStatic(true);

	BuildItems();

	m_Splitter.SetSplitterPanes(m_List, m_HexView);
	m_Splitter.SetSplitterPosPct(25);

	return 0;
}

LRESULT CDataDirectoriesView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
}
