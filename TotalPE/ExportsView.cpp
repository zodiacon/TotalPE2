#include "pch.h"
#include "ExportsView.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "PEStrings.h"
#include <SortHelper.h>
#include "resource.h"
#include <DiaHelper.h>

CExportsView::CExportsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CExportsView::GetColumnText(HWND h, int row, int col) {
	auto& exp = m_Exports[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return exp.Name.c_str();
		case ColumnType::ForwardedName: return exp.ForwarderName.c_str();
		case ColumnType::Ordinal: return std::to_wstring(exp.Ordinal).c_str();
		case ColumnType::RVA: return std::format(L"0x{:X}", exp.FuncRVA).c_str();
		case ColumnType::NameRVA: return std::format(L"0x{:X}", exp.NameRVA).c_str();
		case ColumnType::UndecoratedName: return PEStrings::UndecorateName(exp.FuncName.c_str()).c_str();
		case ColumnType::Detials: return exp.FromSymbols ? L"From symbols" : L"";
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
	UINT icon = IDI_FUNCTION;
	if (!m_Exports[row].ForwarderName.empty())
		icon = IDI_FUNC_FORWARD;
	else if (m_Exports[row].FromSymbols)
		icon = IDI_FUNCTION2;
	return Frame()->GetIconIndex(icon);
}

int CExportsView::GetSaveColumnRange(HWND, int&) const {
	return 1;
}

bool CExportsView::OnRightClickList(HWND, int row, int col, POINT const& pt) const {
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->TrackPopupMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

void CExportsView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	int selected = m_List.GetSelectedCount();
	ui.UIEnable(ID_EDIT_COPY, selected > 0);
	ui.UIEnable(ID_VIEW_DISASSEMBLE, selected == 1 && m_Exports[m_List.GetNextItem(-1, LVNI_SELECTED)].ForwarderName.empty());
	if(first)
		Frame()->SetStatusText(1, std::format(L"Exports: {}", m_Exports.size()).c_str());
}

CString CExportsView::GetTitle() const {
	return L"Exports";
}

void CExportsView::BuildItems() {
	m_Exports.reserve(m_PE->GetExport()->Funcs.size());

	auto const& symbols = Frame()->GetSymbols();
	for (auto const& exp : m_PE->GetExport()->Funcs) {
		Export e(exp);
		if (!exp.FuncName.empty())
			e.Name = (PCWSTR)CString(exp.FuncName.c_str());
		else if(symbols) {
			auto sym = symbols.GetSymbolByRVA(exp.FuncRVA);
			if (sym) {
				e.Name = sym.Name();
				e.FromSymbols = true;
			}
		}
		m_Exports.emplace_back(std::move(e));
	}

	m_List.SetItemCount((int)m_Exports.size());
}

LRESULT CExportsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | 
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 250, ColumnType::Name);
	cm->AddColumn(L"Function RVA", LVCFMT_RIGHT, 100, ColumnType::RVA);
	cm->AddColumn(L"Ordinal", LVCFMT_RIGHT, 60, ColumnType::Ordinal);
	cm->AddColumn(L"Forwarded Name", LVCFMT_LEFT, 250, ColumnType::ForwardedName);
	cm->AddColumn(L"Name RVA", LVCFMT_RIGHT, 100, ColumnType::NameRVA);
	cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 250, ColumnType::UndecoratedName);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 150, ColumnType::Detials);

	BuildItems();

	return 0;
}

LRESULT CExportsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
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

LRESULT CExportsView::OnDissassemble(WORD, WORD, HWND, BOOL&) const {
	ATLASSERT(m_List.GetSelectedCount() == 1);
	auto& exp = m_Exports[m_List.GetNextItem(-1, LVNI_SELECTED)];

	uint32_t bias;
	auto offset = m_PE->GetOffsetFromRVA(exp.FuncRVA);
	uint32_t size = 0x1000;
	if (size + offset > m_PE.GetFileSize())
		size = m_PE.GetFileSize() - offset;
	auto code = m_PE.Map<const std::byte>(offset, size, bias);
	if (!code) {
		AtlMessageBox(m_hWnd, L"Failed to retrieve code", IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}

	auto start = code.get() + bias;
	ULONGLONG imageBase = m_PE->GetFileInfo()->IsPE64 ? m_PE->GetNTHeader()->NTHdr64.OptionalHeader.ImageBase : m_PE->GetNTHeader()->NTHdr32.OptionalHeader.ImageBase;
	Frame()->CreateAssemblyView(std::span<const std::byte>(start, size), offset + imageBase, exp.FuncRVA,
		exp.Name.c_str(), TreeItemType::DirectoryExports);

	return 0;
}

