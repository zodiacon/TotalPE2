#include "pch.h"
#include "ExportsView.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "PEStrings.h"
#include <SortHelper.h>
#include "resource.h"

CExportsView::CExportsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CExportsView::GetColumnText(HWND h, int row, int col) const {
	auto const& exp = m_Exports[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return exp.FuncName.c_str();
		case ColumnType::ForwardedName: return exp.ForwarderName.c_str();
		case ColumnType::Ordinal: return std::to_wstring(exp.Ordinal).c_str();
		case ColumnType::RVA: return std::format(L"0x{:X}", exp.FuncRVA).c_str();
		case ColumnType::NameRVA: return std::format(L"0x{:X}", exp.NameRVA).c_str();
		case ColumnType::UndecoratedName: return PEStrings::UndecorateName(exp.FuncName.c_str()).c_str();
	}
	return CString();
}

void CExportsView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto compare = [&](auto& e1, auto& e2) {
		switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
			case ColumnType::RVA: return SortHelper::Sort(e1.FuncRVA, e2.FuncRVA, asc);
			case ColumnType::NameRVA: return SortHelper::Sort(e1.NameRVA, e2.NameRVA, asc);
			case ColumnType::Name: return SortHelper::Sort(e1.FuncName, e2.FuncName, asc);
			case ColumnType::ForwardedName: return SortHelper::Sort(e1.ForwarderName, e2.ForwarderName, asc);
			case ColumnType::UndecoratedName: return SortHelper::Sort(PEStrings::UndecorateName(e1.FuncName.c_str()), PEStrings::UndecorateName(e2.FuncName.c_str()), asc);
		}
		return false;
	};
	std::ranges::sort(m_Exports, compare);
}

void CExportsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	if((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

int CExportsView::GetRowImage(HWND, int row, int) const {
	return Frame()->GetIconIndex(m_Exports[row].ForwarderName.empty() ? IDI_FUNC_FORWARD : IDI_FUNCTION);
}

int CExportsView::GetSaveColumnRange(HWND, int&) const {
	return 1;
}

void CExportsView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	int selected = m_List.GetSelectedCount();
	ui.UIEnable(ID_EDIT_COPY, selected > 0);
	if(first)
		Frame()->SetStatusText(1, std::format(L"Exports: {}", m_Exports.size()).c_str());
}

CString CExportsView::GetTitle() const {
	return L"Exports";
}

void CExportsView::BuildItems() {
    m_Exports = m_PE->GetExport()->Funcs;

	m_List.SetItemCount((int)m_Exports.size());
}

LRESULT CExportsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | 
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 200, ColumnType::Name);
	cm->AddColumn(L"Function RVA", LVCFMT_RIGHT, 100, ColumnType::RVA);
	cm->AddColumn(L"Ordinal", LVCFMT_RIGHT, 60, ColumnType::Ordinal);
	cm->AddColumn(L"Forwarded Name", LVCFMT_LEFT, 250, ColumnType::ForwardedName);
	cm->AddColumn(L"Name RVA", LVCFMT_RIGHT, 100, ColumnType::NameRVA);
	cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 250, ColumnType::UndecoratedName);

	BuildItems();

	return 0;
}

LRESULT CExportsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    return LRESULT();
}

LRESULT CExportsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
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

