#include "pch.h"
#include "ExceptionsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include "PEStrings.h"

CExceptionsView::CExceptionsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CExceptionsView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];

	switch (col) {
		case 0: return std::format(L"0x{:08X}", item.RuntimeFuncEntry.BeginAddress).c_str();
		case 1: return std::format(L"0x{:08X}", item.RuntimeFuncEntry.EndAddress).c_str();
		case 2: return std::format(L"0x{:08X}", item.RuntimeFuncEntry.UnwindInfoAddress).c_str();
		case 3: return item.Offset ? std::format(L"{} + 0x{:X}", item.UndecoratedName, item.Offset).c_str() : item.UndecoratedName.c_str();
		case 4:
			if (!item.FuncName.empty()) {
				return item.Disp == 0 ? item.FuncName.c_str() : std::format(L"{} + 0x{:X}", item.FuncName, item.Offset).c_str();
			}
			break;
	}
	return CString();
}

void CExceptionsView::DoSort(SortInfo const* si) {

	auto compare = [&](auto& item1, auto& item2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(item1.RuntimeFuncEntry.BeginAddress, item2.RuntimeFuncEntry.BeginAddress, si->SortAscending);
			case 1: return SortHelper::Sort(item1.RuntimeFuncEntry.EndAddress, item2.RuntimeFuncEntry.EndAddress, si->SortAscending);
			case 2: return SortHelper::Sort(item1.RuntimeFuncEntry.UnwindInfoAddress, item2.RuntimeFuncEntry.UnwindInfoAddress, si->SortAscending);
		}
		return false;
	};
	std::ranges::sort(m_Items, compare);
}

void CExceptionsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CExceptionsView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

CString CExceptionsView::GetTitle() const {
	return L"Exceptions";
}

void CExceptionsView::BuildItems() {
	m_Items.reserve(m_PE->GetExceptions()->size());
	auto& symbols = Frame()->GetSymbols();
	for (auto const& ex : *m_PE->GetExceptions()) {
		Exception e(ex);
		if (symbols) {
			auto sym = symbols.GetSymbolByRVA(ex.RuntimeFuncEntry.BeginAddress, SymbolTag::Null, &e.Disp);
			if (sym) {
				e.FuncName = sym.Name();
				if (!e.FuncName.empty())
					e.UndecoratedName = PEStrings::UndecorateName(e.FuncName.c_str());
			}
		}
		m_Items.emplace_back(std::move(e));
	}
	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CExceptionsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Dummy", 0, 10);
	cm->AddColumn(L"Begin Address", LVCFMT_RIGHT, 120);
	cm->AddColumn(L"End Address", LVCFMT_RIGHT, 120);
	cm->AddColumn(L"Unwind Info", LVCFMT_RIGHT, 120);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 320);
	//cm->AddColumn(L"Symbol Name", LVCFMT_LEFT, 220);
	cm->UpdateColumns();
	cm->DeleteColumn(0);

	BuildItems();

	return 0;
}

LRESULT CExceptionsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List);
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}

LRESULT CExceptionsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
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

