#include "pch.h"
#include "ELFSegmentsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "resource.h"

CELFSegmentsView::CELFSegmentsView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFSegmentsView::GetTitle() const { return L"ELF Segments"; }

CString CELFSegmentsView::GetColumnText(HWND h, int row, int col) const {
    auto& s = m_Segments[row];
    switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
        case ColumnType::Index:           return std::format(L"{}", row).c_str();
        case ColumnType::Type:            return s.TypeStr.c_str();
        case ColumnType::Flags:           return s.FlagsStr.c_str();
        case ColumnType::FileOffset:      return std::format(L"0x{:X}", s.FileOffset).c_str();
        case ColumnType::VirtualAddress:  return std::format(L"0x{:X}", s.VirtualAddress).c_str();
        case ColumnType::PhysicalAddress: return std::format(L"0x{:X}", s.PhysicalAddress).c_str();
        case ColumnType::FileSize:        return std::format(L"0x{:X}", s.FileSize).c_str();
        case ColumnType::MemorySize:      return std::format(L"0x{:X}", s.MemorySize).c_str();
        case ColumnType::Alignment:       return s.Alignment ? std::format(L"{}", s.Alignment).c_str() : L"";
    }
    return L"";
}

int CELFSegmentsView::GetRowImage(HWND, int, int) const { return Frame()->GetIconIndex(IDI_SECTION); }

void CELFSegmentsView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    std::ranges::sort(m_Segments, [&](auto& a, auto& b) {
        switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
            case ColumnType::FileSize:       return SortHelper::Sort(a.FileSize, b.FileSize, asc);
            case ColumnType::MemorySize:     return SortHelper::Sort(a.MemorySize, b.MemorySize, asc);
            case ColumnType::VirtualAddress: return SortHelper::Sort(a.VirtualAddress, b.VirtualAddress, asc);
            case ColumnType::FileOffset:     return SortHelper::Sort(a.FileOffset, b.FileOffset, asc);
            case ColumnType::Type:           return SortHelper::Sort(a.TypeStr, b.TypeStr, asc);
        }
        return false;
    });
}

void CELFSegmentsView::OnStateChanged(HWND, int, int, DWORD, DWORD newState) {
    if (newState & LVIS_SELECTED) UpdateUI();
}

void CELFSegmentsView::UpdateUI(bool first) {
    auto& ui = Frame()->GetUI();
    ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
    if (first)
        Frame()->SetStatusText(1, std::format(L"Segments: {}", m_Segments.size()).c_str());
}

LRESULT CELFSegmentsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"#",               LVCFMT_RIGHT, 40,  ColumnType::Index);
    cm->AddColumn(L"Type",            LVCFMT_LEFT,  120, ColumnType::Type);
    cm->AddColumn(L"Flags",           LVCFMT_LEFT,  60,  ColumnType::Flags);
    cm->AddColumn(L"File Offset",     LVCFMT_RIGHT, 110, ColumnType::FileOffset);
    cm->AddColumn(L"Virt Address",    LVCFMT_RIGHT, 120, ColumnType::VirtualAddress);
    cm->AddColumn(L"Phys Address",    LVCFMT_RIGHT, 120, ColumnType::PhysicalAddress);
    cm->AddColumn(L"File Size",       LVCFMT_RIGHT, 90,  ColumnType::FileSize);
    cm->AddColumn(L"Memory Size",     LVCFMT_RIGHT, 90,  ColumnType::MemorySize);
    cm->AddColumn(L"Alignment",       LVCFMT_RIGHT, 80,  ColumnType::Alignment);

    m_Segments = m_ELF.GetSegments();
    m_List.SetItemCount((int)m_Segments.size());
    UpdateUI(true);
    return 0;
}

LRESULT CELFSegmentsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
    return 0;
}
