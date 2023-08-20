#include "pch.h"
#include "DelayImportView.h"
#include "resource.h"
#include <SortHelper.h>

CDelayImportView::CDelayImportView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CDelayImportView::GetColumnText(HWND h, int row, int colIndex) const {
	auto col = GetColumnManager(h)->GetColumnTag<ColumnType>(colIndex);
	if (h == m_List) {
		auto& item = m_Items[row];
		switch (col) {
			case ColumnType::Module: return item.ModuleName.c_str();
			case ColumnType::Functions: return std::to_wstring(item.DelayImpFunc.size()).c_str();
			case ColumnType::DllName: return std::format(L"0x{:X}", item.DelayImpDesc.DllNameRVA).c_str();
			case ColumnType::IAT: return std::format(L"0x{:X}", item.DelayImpDesc.ImportAddressTableRVA).c_str();
			case ColumnType::ImportNameTable: return std::format(L"0x{:X}", item.DelayImpDesc.ImportNameTableRVA).c_str();
			case ColumnType::ModuleHandle: return std::format(L"0x{:X}", item.DelayImpDesc.ModuleHandleRVA).c_str();
			case ColumnType::TimeStamp: return std::format(L"0x{:X}", item.DelayImpDesc.TimeDateStamp).c_str();
			case ColumnType::Attributes: return std::format(L"0x{:X}", item.DelayImpDesc.Attributes.AllAttributes).c_str();
		}
	}
	else {
		auto& func = m_Functions[row];
		switch (col) {
			case ColumnType::Name: return func.FuncName.c_str();
			case ColumnType::Hint: return std::to_wstring(func.ImpByName.Hint).c_str();
			case ColumnType::IAT: return std::format(L"0x{:X}", m_PE->GetFileInfo()->IsPE64 ? func.unThunk.st64.ImportAddressTable.u1.Function : func.unThunk.st32.ImportAddressTable.u1.Function).c_str();
			case ColumnType::ImportNameTable: return std::format(L"0x{:X}", m_PE->GetFileInfo()->IsPE64 ? func.unThunk.st64.ImportNameTable.u1.Function : func.unThunk.st32.ImportNameTable.u1.Function).c_str();
			case ColumnType::BoundImport: return std::format(L"0x{:X}", m_PE->GetFileInfo()->IsPE64 ? func.unThunk.st64.BoundImportAddressTable.u1.Function : func.unThunk.st32.BoundImportAddressTable.u1.Function).c_str();
			case ColumnType::UnloadInfo: return std::format(L"0x{:X}", m_PE->GetFileInfo()->IsPE64 ? func.unThunk.st64.UnloadInformationTable.u1.Function : func.unThunk.st32.UnloadInformationTable.u1.Function).c_str();
		}
	}
	return CString();
}

int CDelayImportView::GetRowImage(HWND h, int row, int) const {
	if (h == m_List)
		return Frame()->GetIconIndex(m_Items[row].ModuleName.substr(0, 7) == "api-ms-" ? IDI_INTERFACE : IDI_DLL_IMPORT);
	return Frame()->GetIconIndex(IDI_FUNCTION);
}

void CDelayImportView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	if ((oldState & LVIS_SELECTED) || (newState & LVIS_SELECTED)) {
		int index = m_List.GetSelectedIndex();
		if (index < 0)
			m_Functions.clear();
		else
			m_Functions = m_Items[index].DelayImpFunc;
		m_FuncList.SetItemCount((int)m_Functions.size());
	}
}

void CDelayImportView::DoSort(SortInfo const* si) {
	auto col = GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn);
	auto asc = si->SortAscending;
	if (si->hWnd == m_List) {
		auto compare = [&](auto& i1, auto& i2) {
			switch (col) {
				case ColumnType::Module: return SortHelper::Sort(i1.ModuleName, i2.ModuleName, asc);
				case ColumnType::Functions: return SortHelper::Sort(i1.DelayImpFunc.size(), i2.DelayImpFunc.size(), asc);
				case ColumnType::IAT: return SortHelper::Sort(i1.DelayImpDesc.ImportAddressTableRVA, i2.DelayImpDesc.ImportAddressTableRVA, asc);
				case ColumnType::ImportNameTable: return SortHelper::Sort(i1.DelayImpDesc.ImportNameTableRVA, i2.DelayImpDesc.ImportNameTableRVA, asc);
				case ColumnType::ModuleHandle: return SortHelper::Sort(i1.DelayImpDesc.ModuleHandleRVA, i2.DelayImpDesc.ModuleHandleRVA, asc);
				case ColumnType::TimeStamp: return SortHelper::Sort(i1.DelayImpDesc.TimeDateStamp, i2.DelayImpDesc.TimeDateStamp, asc);
				case ColumnType::Attributes: return SortHelper::Sort(i1.DelayImpDesc.Attributes.AllAttributes, i2.DelayImpDesc.Attributes.AllAttributes, asc);
			}
			return false;
			};
		std::ranges::sort(m_Items, compare);
	}
}

CString CDelayImportView::GetTitle() const {
	return L"Delay Import";
}

void CDelayImportView::BuildItems() {
	m_Items = *m_PE->GetDelayImport();

	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CDelayImportView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_SINGLESEL |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Module", LVCFMT_LEFT, 260, ColumnType::Module);
	cm->AddColumn(L"Functions", LVCFMT_RIGHT, 70, ColumnType::Functions);
	cm->AddColumn(L"DLL Name RVA", LVCFMT_RIGHT, 100, ColumnType::DllName);
	cm->AddColumn(L"IAT RVA", LVCFMT_RIGHT, 100, ColumnType::IAT);
	cm->AddColumn(L"Import Name RVA", LVCFMT_RIGHT, 100, ColumnType::ImportNameTable);
	cm->AddColumn(L"Module Handle RVA", LVCFMT_RIGHT, 100, ColumnType::ModuleHandle);
	cm->AddColumn(L"Time/Date Stamp", LVCFMT_RIGHT, 100, ColumnType::TimeStamp);
	cm->AddColumn(L"Attributes", LVCFMT_RIGHT, 100, ColumnType::Attributes);

	m_FuncList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
	m_FuncList.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	cm = GetColumnManager(m_FuncList);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 160, ColumnType::Name);
	cm->AddColumn(L"Hint", LVCFMT_RIGHT, 70, ColumnType::Hint);
	cm->AddColumn(L"IAT", LVCFMT_RIGHT, 100, ColumnType::IAT);
	cm->AddColumn(L"Import Name Table", LVCFMT_RIGHT, 100, ColumnType::ImportNameTable);
	cm->AddColumn(L"Bound IAT", LVCFMT_RIGHT, 100, ColumnType::BoundImport);
	cm->AddColumn(L"Unload Info Table", LVCFMT_RIGHT, 100, ColumnType::UnloadInfo);

	m_Splitter.SetSplitterPosPct(50);
	m_Splitter.SetSplitterPanes(m_List, m_FuncList);

	BuildItems();

	return 0;
}
