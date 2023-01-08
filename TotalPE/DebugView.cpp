#include "pch.h"
#include "DebugView.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <PEFile.h>

CDebugView::CDebugView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe), m_HexView(frame) {
}

CString CDebugView::GetTitle() const {
	return L"Debug";
}

CString CDebugView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return PEStrings::DebugTypeToString(item.DebugDir.Type);
		case 1: return std::format(L"0x{:08X}", item.DebugDir.TimeDateStamp).c_str();
		case 2: return std::format(L"{}.{}", item.DebugDir.MajorVersion, item.DebugDir.MinorVersion).c_str();
		case 3: return std::format(L"0x{:X}", item.DebugDir.AddressOfRawData).c_str();
		case 4: return PEStrings::ToMemorySize(item.DebugDir.SizeOfData).c_str();
		case 5: return std::format(L"0x{:X}", item.DebugDir.PointerToRawData).c_str();
		case 6: return GetDetails(row).c_str();
	}
	return CString();
}

void CDebugView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	auto compare = [&](auto& d1, auto& d2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(PEStrings::DebugTypeToString(d1.DebugDir.Type), PEStrings::DebugTypeToString(d2.DebugDir.Type), asc);
			case 1: return SortHelper::Sort(d1.DebugDir.TimeDateStamp, d2.DebugDir.TimeDateStamp, asc);
			case 2: return SortHelper::Sort((d1.DebugDir.MajorVersion << 16) | d1.DebugDir.MinorVersion, (d2.DebugDir.MajorVersion << 16) | d2.DebugDir.MinorVersion, asc);
			case 3: return SortHelper::Sort(d1.DebugDir.AddressOfRawData, d2.DebugDir.AddressOfRawData, asc);
			case 4: return SortHelper::Sort(d1.DebugDir.SizeOfData, d2.DebugDir.SizeOfData, asc);
			case 5: return SortHelper::Sort(d1.DebugDir.PointerToRawData, d2.DebugDir.PointerToRawData, asc);
		}
		return false;
	};
	std::ranges::sort(m_Items, compare);
}

void CDebugView::OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState) {
	if (newState & LVIS_SELECTED) {
		int index = m_List.GetSelectedIndex();
		ATLASSERT(index == from);
		if (index >= 0) {
			auto const& item = m_Items[index];
			m_HexView.SetData(m_PE, item.DebugDir.PointerToRawData, item.DebugDir.SizeOfData);
		}
		else {
			m_HexView.ClearData();
		}
	}
}

std::wstring CDebugView::GetDetails(int row) const {
	struct CodeView {
		char format[4];
		GUID guid;
		ULONG count;
		char pdb[64];
	};
	auto& item = m_Items[row];
	switch (item.DebugDir.Type) {
		case IMAGE_DEBUG_TYPE_CODEVIEW:
			auto data = (CodeView const*)(m_PE.GetData() + item.DebugDir.PointerToRawData);
			return std::format(L"Format: {}{}{}{} GUID: {} Pdb: {}",
				data->format[0], data->format[1], data->format[2], data->format[3],
				PEStrings::GuidToString(data->guid), (PCWSTR)CString(data->pdb));
	}
	return std::wstring();
}

LRESULT CDebugView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_List.Create(m_hWndClient, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_HexView.SetStatic(true);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Type", LVCFMT_LEFT, 170);
	cm->AddColumn(L"Time Stamp", LVCFMT_RIGHT, 130);
	cm->AddColumn(L"Version", LVCFMT_RIGHT, 110);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 110);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 130);
	cm->AddColumn(L"Ptr to Raw Data", LVCFMT_RIGHT, 130);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 500);

	m_Splitter.SetSplitterPosPct(30);
	m_Splitter.SetSplitterPanes(m_List, m_HexView);

	BuildItems();
	return 0;
}

void CDebugView::BuildItems() {
	m_Items = *m_PE->GetDebug();
	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CDebugView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List);
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}
