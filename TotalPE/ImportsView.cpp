#include "pch.h"
#include "ImportsView.h"
#include "PEStrings.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>

CImportsView::CImportsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CImportsView::GetColumnText(HWND h, int row, int col) const {
	auto cm = GetColumnManager(h);
	auto tag = cm->GetColumnTag<ColumnType>(col);

	if (h == m_ModList) {
		auto& mod = m_Modules[row];
		switch (tag) {
			case ColumnType::ModuleName: return mod.ModuleName.c_str();
			case ColumnType::FunctionCount: return std::to_wstring(mod.ImportFunc.size()).c_str();
			case ColumnType::Bound: return mod.ImportDesc.TimeDateStamp ? L"Yes" : L"No";
		}
	}
	else {
		auto& func = m_Functions[row];
		switch (tag) {
			case ColumnType::FunctionName: return func.FuncName.c_str();
			case ColumnType::Hint: return std::to_wstring(func.ImpByName.Hint).c_str();
			case ColumnType::Ordinal: return func.ImpByName.Hint == 0 ? std::to_wstring(func.unThunk.Thunk32.u1.Ordinal).c_str() : L"0";
			case ColumnType::UndecoratedName: return func.FuncName.empty() ? "" : PEStrings::UndecorateName(func.FuncName.c_str()).c_str();
		}
	}
	return CString();
}

int CImportsView::GetRowImage(HWND h, int row, int) const {
	if (h == m_ModList) {
		return Frame()->GetIconIndex(m_Modules[row].ModuleName.starts_with("api-ms-win") ? IDI_INTERFACE : IDI_DLL_IMPORT);
	}
	return Frame()->GetIconIndex(IDI_FUNCTION);
}

void CImportsView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto col = GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn);
	if (si->hWnd == m_ModList) {
		auto compare = [&](auto& m1, auto& m2) {
			switch (col) {
				case ColumnType::ModuleName: return SortHelper::Sort(m1.ModuleName, m2.ModuleName, asc);
				case ColumnType::FunctionCount: return SortHelper::Sort(m1.ImportFunc.size(), m2.ImportFunc.size(), asc);
				case ColumnType::Bound: return SortHelper::Sort(m1.ImportDesc.TimeDateStamp, m2.ImportDesc.TimeDateStamp, asc);
			}
			return false;
		};
		std::ranges::sort(m_Modules, compare);
	}
	else {
		auto compare = [&](auto& f1, auto& f2) {
			switch (col) {
				case ColumnType::FunctionName: return SortHelper::Sort(f1.FuncName, f2.FuncName, asc);
				case ColumnType::UndecoratedName: return SortHelper::Sort(PEStrings::UndecorateName(f1.FuncName.c_str()), PEStrings::UndecorateName(f2.FuncName.c_str()), asc);
				case ColumnType::Hint: return SortHelper::Sort(f1.ImpByName.Hint, f2.ImpByName.Hint, asc);
				case ColumnType::Ordinal: return SortHelper::Sort(
					f1.ImpByName.Hint == 0 ? f1.unThunk.Thunk32.u1.Ordinal : 0, 
					f2.ImpByName.Hint == 0 ? f2.unThunk.Thunk32.u1.Ordinal : 0, asc);
			}
			return false;
		};
		std::ranges::sort(m_Functions, compare);
	}
}

void CImportsView::OnStateChanged(HWND hWnd, int from, int to, DWORD oldState, DWORD newState) {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED)) {
		if (hWnd == m_ModList) {
			int selected = m_ModList.GetSelectedCount();
			if (selected == 1) {
				auto const& mod = m_Modules[m_ModList.GetNextItem(-1, LVNI_SELECTED)];
				m_Functions = mod.ImportFunc;
				Sort(m_FuncList);
				m_FuncList.SetItemCount((int)m_Functions.size());
			}
			else {
				m_Functions.clear();
				m_FuncList.SetItemCount(0);
			}
		}
		UpdateUI();
	}
}

void CImportsView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	auto hWnd = ::GetFocus();
	CListViewCtrl lv;
	if (hWnd == m_ModList)
		lv = m_ModList;
	else if (hWnd == m_FuncList)
		lv = m_FuncList;
	if (lv) {
		auto selected = lv.GetSelectedCount();
		ui.UIEnable(ID_EDIT_COPY, selected > 0);
	}
}

CString CImportsView::GetTitle() const {
	return L"Imports";
}

LRESULT CImportsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

	m_ModList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | 
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0);
	m_ModList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_ModList.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	m_FuncList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
	m_FuncList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_FuncList.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_ModList);
	cm->AddColumn(L"Module", LVCFMT_LEFT, 280, ColumnType::ModuleName);
	cm->AddColumn(L"Count", LVCFMT_RIGHT, 60, ColumnType::FunctionCount);
	cm->AddColumn(L"Bound?", LVCFMT_RIGHT, 60, ColumnType::Bound);

	cm = GetColumnManager(m_FuncList);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 250, ColumnType::FunctionName);
	cm->AddColumn(L"Hint", LVCFMT_RIGHT, 60, ColumnType::Hint);
	cm->AddColumn(L"Ordinal", LVCFMT_RIGHT, 60, ColumnType::Ordinal);
	cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 250, ColumnType::UndecoratedName);

	BuildItems();

	m_Splitter.SetSplitterPanes(m_ModList, m_FuncList);
	m_Splitter.SetSplitterPosPct(50);

	return 0;
}

LRESULT CImportsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(::GetFocus(), L","));
	return 0;
}

void CImportsView::BuildItems() {
	m_Modules = *m_PE->GetImport();
	m_ModList.SetItemCount((int)m_Modules.size());
	m_Is64 = m_PE->GetFileInfo()->IsPE64;
}

LRESULT CImportsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	auto& lv = ::GetFocus() == m_ModList || m_Functions.empty() ? m_ModList : m_FuncList;
	int index = -1;
	if (lv.GetItemCount()) {
		index = ListViewHelper::SearchItem(lv, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());

		if (index >= 0) {
			lv.SelectItem(index);
			lv.SetFocus();
		}
	}
	if(index < 0) {
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	}
	return 0;
}

