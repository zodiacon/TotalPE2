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
		case 0: return PEStrings::DebugTypeToString(item.Directory.Type);
		case 1: return std::format(L"0x{:08X}", item.Directory.TimeDateStamp).c_str();
		case 2: return std::format(L"{}.{}", item.Directory.MajorVersion, item.Directory.MinorVersion).c_str();
		case 3: return std::format(L"0x{:X}", item.Directory.AddressOfRawData).c_str();
		case 4: return PEStrings::ToMemorySize(item.Directory.SizeOfData).c_str();
		case 5: return std::format(L"0x{:X}", item.Directory.PointerToRawData).c_str();
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
			case 0: return SortHelper::Sort(PEStrings::DebugTypeToString(d1.Directory.Type), PEStrings::DebugTypeToString(d2.Directory.Type), asc);
			case 1: return SortHelper::Sort(d1.Directory.TimeDateStamp, d2.Directory.TimeDateStamp, asc);
			case 2: return SortHelper::Sort((d1.Directory.MajorVersion << 16) | d1.Directory.MinorVersion, (d2.Directory.MajorVersion << 16) | d2.Directory.MinorVersion, asc);
			case 3: return SortHelper::Sort(d1.Directory.AddressOfRawData, d2.Directory.AddressOfRawData, asc);
			case 4: return SortHelper::Sort(d1.Directory.SizeOfData, d2.Directory.SizeOfData, asc);
			case 5: return SortHelper::Sort(d1.Directory.PointerToRawData, d2.Directory.PointerToRawData, asc);
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
			m_HexView.SetData(m_PE, item.Directory.PointerToRawData, item.Directory.SizeOfData);
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
	switch (item.Directory.Type) {
		case IMAGE_DEBUG_TYPE_CODEVIEW:
			auto data = (CodeView const*)(m_PE.GetData() + item.Directory.PointerToRawData);
			return std::format(L"Format: {}{}{}{} GUID: {} Pdb: {}",
				data->format[0], data->format[1], data->format[2], data->format[3],
				PEStrings::GuidToString(data->guid), (PCWSTR)CString(data->pdb));
	}
	return std::wstring();
}

LRESULT CDebugView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_List.Create(m_hWndClient, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
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
	m_Items = *m_PE.GetDebug();
	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CDebugView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List);
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}
