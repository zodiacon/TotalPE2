#include "pch.h"
#include "ImportsView.h"
#include "PEStrings.h"
#include "ImportAnalysis.h"
#include "ApiSet.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>

namespace {
	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}
}

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
			case ColumnType::ResolvedTo: return DescribeApiSet(mod.ModuleName).c_str();
		}
	}
	else {
		auto& func = m_Functions[row];
		switch (tag) {
			case ColumnType::FunctionName:
				if (IsOrdinalImport(func, m_Is64))	// the name the ordinal has in the DLL on this system, if it can be found
					return Widen(ResolveOrdinalName(m_CurrentModule, ImportOrdinal(func), !m_Is64)).c_str();
				return func.FuncName.c_str();
			case ColumnType::ImportBy: return IsOrdinalImport(func, m_Is64) ? L"Ordinal" : L"Name";
			case ColumnType::Hint: return IsOrdinalImport(func, m_Is64) ? L"" : std::to_wstring(func.ImpByName.Hint).c_str();
			case ColumnType::Ordinal: return IsOrdinalImport(func, m_Is64) ? std::to_wstring(ImportOrdinal(func)).c_str() : L"";
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
				case ColumnType::ResolvedTo: return SortHelper::Sort(DescribeApiSet(m1.ModuleName), DescribeApiSet(m2.ModuleName), asc);
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
				case ColumnType::ImportBy: return SortHelper::Sort(IsOrdinalImport(f1, m_Is64), IsOrdinalImport(f2, m_Is64), asc);
				case ColumnType::Ordinal: return SortHelper::Sort(ImportOrdinal(f1), ImportOrdinal(f2), asc);
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
				m_CurrentModule = mod.ModuleName;
				Sort(m_FuncList);
				m_FuncList.SetItemCount((int)m_Functions.size());
			}
			else {
				m_Functions.clear();
				m_CurrentModule.clear();
				m_FuncList.SetItemCount(0);
			}
		}
		UpdateUI();
	}
}

void CImportsView::UpdateUI(bool first) const {
	if (!m_Imphash.empty())
		Frame()->SetStatusText(1, (L"Import hash: " + m_Imphash).c_str());
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
		ui.UIEnable(ID_IMPORT_GOTOFILELOCATION, hWnd == m_ModList && selected == 1);
		ui.UIEnable(ID_IMPORT_FILEPROPERTIES, hWnd == m_ModList && selected == 1);
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

	m_FuncList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | 
		LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0);
	m_FuncList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_FuncList.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_ModList);
	cm->AddColumn(L"Module", LVCFMT_LEFT, 280, ColumnType::ModuleName);
	cm->AddColumn(L"Resolved To", LVCFMT_LEFT, 180, ColumnType::ResolvedTo);
	cm->AddColumn(L"Count", LVCFMT_RIGHT, 60, ColumnType::FunctionCount);
	cm->AddColumn(L"Bound?", LVCFMT_RIGHT, 60, ColumnType::Bound);

	cm = GetColumnManager(m_FuncList);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 250, ColumnType::FunctionName);
	cm->AddColumn(L"Import By", LVCFMT_LEFT, 70, ColumnType::ImportBy);
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
	m_Modules = *m_PE.GetImport();
	m_ModList.SetItemCount((int)m_Modules.size());
	m_Is64 = m_PE.GetFileInfo()->IsPE64;
	m_Imphash = Widen(ComputeImphash(m_PE));
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
	if (index < 0) {
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	}
	return 0;
}

LRESULT CImportsView::OnFileProperties(WORD, WORD, HWND, BOOL&) const {
	auto& imp = m_Modules[m_ModList.GetNextItem(-1, LVNI_SELECTED)];
	WCHAR path[MAX_PATH]{};
	if (::SearchPath(nullptr, CString(imp.ModuleName.c_str()), nullptr, _countof(path), path, nullptr)) {
		SHELLEXECUTEINFO sei{ sizeof(sei) };
		sei.fMask = SEE_MASK_INVOKEIDLIST;
		sei.lpVerb = L"properties";
		sei.lpFile = path;
		::ShellExecuteEx(&sei);
	}
	else {
		AtlMessageBox(m_hWnd, L"Failed to locate module", IDR_MAINFRAME, MB_ICONERROR);
	}
	return 0;
}

LRESULT CImportsView::OnGotoFileLocation(WORD, WORD, HWND, BOOL&) const {
	auto& imp = m_Modules[m_ModList.GetNextItem(-1, LVNI_SELECTED)];
	WCHAR path[MAX_PATH]{};
	::SearchPath(nullptr, CString(imp.ModuleName.c_str()), nullptr, _countof(path), path, nullptr);
	if ((INT_PTR)::ShellExecute(nullptr, L"open", L"explorer",
		L"/select,\"" + CString(path) + L"\"",
		nullptr, SW_SHOWDEFAULT) < 32)
		AtlMessageBox(m_hWnd, L"Failed to locate module", IDR_MAINFRAME, MB_ICONERROR);

	return 0;
}

bool CImportsView::OnRightClickList(HWND h, int row, int col, POINT const& pt) const {
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(h == m_ModList ? 4 : 0), 0, pt.x, pt.y);
}
