#include "pch.h"
#include "RelocationsView.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>

CRelocationsView::CRelocationsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CRelocationsView::GetTitle() const {
	return L"Relocations";
}

CString CRelocationsView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_List) {
		auto const& item = m_Items[row];
		switch (col) {
			case 0: return std::format(L"0x{:X}", item.BaseReloc.VirtualAddress).c_str();
			case 1: return std::format(L"0x{:X}", item.BaseReloc.SizeOfBlock).c_str();
			case 2: return std::format(L"0x{:X}", item.RelocData.size()).c_str();
		}
	}
	else {
		auto const& reloc = m_RelocData[row];
		switch (col) {
			case 0: return PEStrings::x64RelocationTypeToString(reloc.RelocType);
			case 1: return std::format(L"0x{:X}", reloc.RelocOffset).c_str();
		}
	}
	return CString();
}

void CRelocationsView::DoSort(SortInfo const* si) {
	if (si->hWnd == m_List) {
		auto compare = [&](auto& item1, auto& item2) {
			switch (si->SortColumn) {
				case 0: return SortHelper::Sort(item1.BaseReloc.VirtualAddress, item2.BaseReloc.VirtualAddress, si->SortAscending);
				case 1: return SortHelper::Sort(item1.BaseReloc.SizeOfBlock, item2.BaseReloc.SizeOfBlock, si->SortAscending);
				case 2: return SortHelper::Sort(item1.RelocData.size(), item2.RelocData.size(), si->SortAscending);
			}
			return false;
		};
		std::ranges::sort(m_Items, compare);
	}
	else {
		auto compare = [&](auto& r1, auto& r2) {
			switch (si->SortColumn) {
				case 0: return SortHelper::Sort(PEStrings::x64RelocationTypeToString(r1.RelocType), PEStrings::x64RelocationTypeToString(r2.RelocType), si->SortAscending);
				case 1: return SortHelper::Sort(r1.RelocOffset, r2.RelocOffset, si->SortAscending);
			}
			return false;
		};
		std::ranges::sort(m_RelocData, compare);
	}
}

void CRelocationsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED)) {
		if (m_List.GetSelectedCount() != 1) {
			m_RelocList.SetItemCount(0);
			m_RelocData.clear();
		}
		else {
			m_RelocData = m_Items[m_List.GetNextItem(-1, LVNI_SELECTED)].RelocData;
			m_RelocList.SetItemCount((int)m_RelocData.size());
			Sort(m_RelocList);
		}
	}
}

LRESULT CRelocationsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	m_RelocList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_RelocList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Dummy", 0, 0);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 100);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 100);
	cm->AddColumn(L"Count", LVCFMT_RIGHT, 80);
	cm->DeleteColumn(0);

	cm = GetColumnManager(m_RelocList);
	cm->AddColumn(L"Type", LVCFMT_LEFT, 100);
	cm->AddColumn(L"Offset", LVCFMT_RIGHT, 100);

	m_Splitter.SetSplitterPanes(m_List, m_RelocList);
	m_Splitter.SetSplitterPosPct(40);

	BuildItems();
	return 0;
}

void CRelocationsView::BuildItems() {
	m_Items = *m_PE->GetRelocations();
	m_List.SetItemCount((int)m_Items.size());
}

