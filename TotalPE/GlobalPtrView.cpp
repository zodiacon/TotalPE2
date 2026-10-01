#include "pch.h"
#include "GlobalPtrView.h"

CGlobalPtrView::CGlobalPtrView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CGlobalPtrView::GetTitle() const {
	return L"Global Pointer";
}

CString CGlobalPtrView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Data[row];
	return col == 0 ? item.Name.c_str() : item.Value.c_str();
}

void CGlobalPtrView::DoSort(SortInfo const*) {
}

LRESULT CGlobalPtrView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 180);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 300);

	BuildItems();
	return 0;
}

void CGlobalPtrView::BuildItems() {
	auto dirs = m_PE.GetDataDirs();
	if (!dirs || dirs->size() <= IMAGE_DIRECTORY_ENTRY_GLOBALPTR)
		return;
	auto& dir = (*dirs)[IMAGE_DIRECTORY_ENTRY_GLOBALPTR];
	auto rva = dir.DataDir.VirtualAddress;

	m_Data.push_back({ L"Global Pointer RVA", std::format(L"0x{:X}", rva) });
	m_Data.push_back({ L"Global Pointer VA", std::format(L"0x{:X}", m_PE.GetImageBase() + rva) });
	if (dir.DataDir.Size)
		m_Data.push_back({ L"Size", std::format(L"0x{:X} (expected 0)", dir.DataDir.Size) });
	if (!dir.Section.empty())
		m_Data.push_back({ L"Section", std::wstring(dir.Section.begin(), dir.Section.end()) });

	auto offset = m_PE.GetOffsetFromRVA(rva);
	if (offset != 0 && offset < m_PE.GetFileSize())
		m_Data.push_back({ L"File Offset", std::format(L"0x{:X}", offset) });

	auto& sym = Frame()->GetSymbols();
	if (sym) {
		auto name = sym.GetSymbolByRVA(rva).Name();
		if (!name.empty())
			m_Data.push_back({ L"Symbol", name });
	}
	m_List.SetItemCount((int)m_Data.size());
}
