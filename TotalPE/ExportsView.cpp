#include "pch.h"
#include "ExportsView.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "PEStrings.h"
#include <SortHelper.h>
#include "resource.h"
#include <DiaHelper.h>
#include "ApiSet.h"

namespace {
	// what is worth knowing about an export besides its name and address
	std::wstring ExportDetails(PEExportFunction const& exp, bool nameFromSymbols) {
		std::vector<std::wstring> parts;
		if (exp.FuncName.empty())
			parts.push_back(L"Ordinal only");
		if (nameFromSymbols)
			parts.push_back(L"Name from symbols");
		if (!exp.ForwarderName.empty()) {
			// "NTDLL.RtlAllocateHeap": the module is everything before the last dot
			auto dot = exp.ForwarderName.rfind('.');
			if (dot == std::string::npos) {
				parts.push_back(L"Forwarded");
			}
			else {
				auto module = exp.ForwarderName.substr(0, dot);
				if (ApiSetMap::IsApiSetName(module)) {
					auto host = DescribeApiSet(module);
					parts.push_back(L"Forwarded to an API set, hosted by " + host);
				}
				else {
					parts.push_back(L"Forwarded to " + std::wstring(module.begin(), module.end()));
				}
			}
		}
		std::wstring text;
		for (auto& p : parts)
			text += (text.empty() ? L"" : L"; ") + p;
		return text;
	}
}

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
		case ColumnType::UndecoratedName: return PEStrings::UndecorateName(exp.Name.c_str()).c_str();
		case ColumnType::Details: return ExportDetails(exp, exp.FromSymbols).c_str();
	}
	return CString();
}

void CExportsView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto compare = [&](auto& e1, auto& e2) {
		switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
			case ColumnType::RVA: return SortHelper::Sort(e1.FuncRVA, e2.FuncRVA, asc);
			case ColumnType::Ordinal: return SortHelper::Sort(e1.Ordinal, e2.Ordinal, asc);
			case ColumnType::NameRVA: return SortHelper::Sort(e1.NameRVA, e2.NameRVA, asc);
			case ColumnType::Name: return SortHelper::Sort(e1.Name, e2.Name, asc);
			case ColumnType::ForwardedName: return SortHelper::Sort(e1.ForwarderName, e2.ForwarderName, asc);
			case ColumnType::UndecoratedName: return SortHelper::Sort(PEStrings::UndecorateName(e1.FuncName.c_str()), PEStrings::UndecorateName(e2.FuncName.c_str()), asc);
			case ColumnType::Details: return SortHelper::Sort(ExportDetails(e1, e1.FromSymbols), ExportDetails(e2, e2.FromSymbols), asc);
		}
		return false;
	};
	std::ranges::sort(m_Exports, compare);
}

void CExportsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const {
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
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
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
	if (m_PE.GetExport()) {
		m_Exports.reserve(m_PE.GetExport()->Funcs.size());

		auto const& symbols = Frame()->GetSymbols();
		for (auto const& exp : m_PE.GetExport()->Funcs) {
			Export e(exp);
			if (!exp.FuncName.empty())
				e.Name = (PCWSTR)CString(exp.FuncName.c_str());
			else if (symbols) {
				auto sym = symbols.GetSymbolByRVA(exp.FuncRVA);
				if (sym) {
					e.Name = sym.Name();
					e.FromSymbols = true;
				}
			}
			m_Exports.emplace_back(std::move(e));
		}
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
	cm->AddColumn(L"Details", LVCFMT_LEFT, 300, ColumnType::Details);

	BuildItems();

	return 0;
}

LRESULT CExportsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
}

LRESULT CExportsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr)
		return 0;

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

	auto offset = m_PE.GetOffsetFromRVA(exp.FuncRVA);
	uint32_t size = 0x1000;
	if (size + offset > m_PE.GetFileSize())
		size = m_PE.GetFileSize() - offset;
	auto code = m_PE.GetSpan(offset, size);

	ULONGLONG imageBase = m_PE.GetFileInfo()->IsPE64 ? m_PE.GetNTHeader()->NTHdr64.OptionalHeader.ImageBase : m_PE.GetNTHeader()->NTHdr32.OptionalHeader.ImageBase;
	Frame()->CreateAssemblyView(code, exp.FuncRVA + imageBase, exp.FuncRVA,
		exp.Name.c_str(), TreeItemType::DirectoryExports);

	return 0;
}

