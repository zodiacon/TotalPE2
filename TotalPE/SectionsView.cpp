#include "pch.h"
#include "SectionsView.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "resource.h"

CString CSectionsView::GetTitle() const {
	return L"Sections";
}

CSectionsView::CSectionsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe), m_HexView(frame) {
}

CString CSectionsView::GetColumnText(HWND h, int row, int col) const {
	auto& section = m_Sections[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return section.SectionName.c_str();
		case ColumnType::Size: return PEStrings::ToMemorySize(section.SecHdr.Misc.VirtualSize).c_str();
		case ColumnType::Address: return std::format(L"0x{:X}", section.SecHdr.VirtualAddress).c_str();
		case ColumnType::RawData: return std::format(L"0x{:X}", section.SecHdr.PointerToRawData).c_str();
		case ColumnType::RawSize: return PEStrings::ToMemorySize(section.SecHdr.SizeOfRawData).c_str();
		case ColumnType::Characteristics: return std::format(L"0x{:08X} ({})", section.SecHdr.Characteristics,
			PEStrings::SectionCharacteristicsToString(section.SecHdr.Characteristics)).c_str();
		case ColumnType::LineNumbers: return std::format(L"{}", section.SecHdr.NumberOfLinenumbers).c_str();
		case ColumnType::Relocations: return std::format(L"{}", section.SecHdr.NumberOfRelocations).c_str();
	}
	return CString();
}

int CSectionsView::GetRowImage(HWND, int row, int) const {
	return 0;
}

void CSectionsView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto compare = [&](auto& s1, auto& s2) {
		switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
			case ColumnType::Address: return SortHelper::Sort(s1.SecHdr.VirtualAddress, s2.SecHdr.VirtualAddress, asc);
			case ColumnType::Size: return SortHelper::Sort(s1.SecHdr.Misc.VirtualSize, s2.SecHdr.Misc.VirtualSize, asc);
			case ColumnType::Name: return SortHelper::Sort(s1.SectionName, s2.SectionName, asc);
			case ColumnType::RawData: return SortHelper::Sort(s1.SecHdr.PointerToRawData, s2.SecHdr.PointerToRawData, asc);
			case ColumnType::RawSize: return SortHelper::Sort(s1.SecHdr.SizeOfRawData, s2.SecHdr.SizeOfRawData, asc);
			case ColumnType::Characteristics: return SortHelper::Sort(s1.SecHdr.Characteristics, s2.SecHdr.Characteristics, asc);
		}
		return false;
	};
	std::ranges::sort(m_Sections, compare);
}

void CSectionsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	if (newState & LVIS_SELECTED) {
		int selected = m_List.GetSelectedCount();
		if (selected == 1) {
			auto const& sec = m_Sections[m_List.GetNextItem(-1, LVNI_SELECTED)];
			auto offset = sec.SecHdr.PointerToRawData;
			m_HexView.SetData(m_PE, offset, sec.SecHdr.SizeOfRawData);
		}
		else {
			m_HexView.ClearData();
		}
	}
	UpdateUI();
}

void CSectionsView::BuildItems() {
	m_Sections = *m_PE->GetSecHeaders();
	m_List.SetItemCount((int)m_Sections.size());
}

void CSectionsView::UpdateUI() {
	auto& ui = Frame()->GetUI();
	auto pane = m_Splitter.GetActivePane();
	ui.UIEnable(ID_EDIT_COPY, (pane == 0 && m_List.GetSelectedCount() > 0) || (pane == 1 && m_HexView.Hex().HasSelection()));
}

LRESULT CSectionsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 90, ColumnType::Name);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 90, ColumnType::Address);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 110, ColumnType::Size);
	cm->AddColumn(L"Ptr to Raw Data", LVCFMT_RIGHT, 110, ColumnType::RawData);
	cm->AddColumn(L"Size of Raw Data", LVCFMT_RIGHT, 130, ColumnType::RawSize);
	//cm->AddColumn(L"Relocations", LVCFMT_RIGHT, 60, ColumnType::Relocations);
	//cm->AddColumn(L"Ptr to Reloc", LVCFMT_RIGHT, 60, ColumnType::PointerToReloc);
	//cm->AddColumn(L"Line Numbers", LVCFMT_RIGHT, 60, ColumnType::LineNumbers);
	//cm->AddColumn(L"Ptr to Lines", LVCFMT_RIGHT, 60, ColumnType::PointerToLines);
	cm->AddColumn(L"Characteristics", LVCFMT_LEFT, 300, ColumnType::Characteristics);

	CImageList images;
	images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
	images.AddIcon(AtlLoadIconImage(IDI_SECTION, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);

	m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
	m_HexView.SetStatic(true);

	BuildItems();

	m_Splitter.SetSplitterPanes(m_List, m_HexView);
	m_Splitter.SetSplitterPosPct(25);

	return 0;
}

LRESULT CSectionsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
}
