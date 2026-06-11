#include "pch.h"
#include "ELFRelocationsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "resource.h"

CELFRelocationsView::CELFRelocationsView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFRelocationsView::GetTitle() const { return L"ELF Relocations"; }

CString CELFRelocationsView::GetColumnText(HWND h, int row, int col) const {
    auto& r = m_Relocations[row];
    switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
        case ColumnType::Address: return std::format(L"0x{:X}", r.Address).c_str();
        case ColumnType::Type:    return std::format(L"0x{:X}", r.Type).c_str();
        case ColumnType::Addend:  return r.IsRela ? std::format(L"{}", r.Addend).c_str() : L"";
        case ColumnType::Symbol:  return r.SymbolName.c_str();
        case ColumnType::Section: return r.SectionName.c_str();
        case ColumnType::Kind:    return r.IsRela ? L"RELA" : L"REL";
    }
    return L"";
}

int CELFRelocationsView::GetRowImage(HWND, int, int) const { return Frame()->GetIconIndex(IDI_SECTION); }

void CELFRelocationsView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    std::ranges::sort(m_Relocations, [&](auto& a, auto& b) {
        switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
            case ColumnType::Address: return SortHelper::Sort(a.Address, b.Address, asc);
            case ColumnType::Type:    return SortHelper::Sort(a.Type, b.Type, asc);
            case ColumnType::Symbol:  return SortHelper::Sort(a.SymbolName, b.SymbolName, asc);
            case ColumnType::Section: return SortHelper::Sort(a.SectionName, b.SectionName, asc);
            case ColumnType::Addend:  return SortHelper::Sort(a.Addend, b.Addend, asc);
        }
        return false;
    });
}

void CELFRelocationsView::OnStateChanged(HWND, int, int, DWORD, DWORD newState) {
    if (newState & LVIS_SELECTED) UpdateUI();
}

void CELFRelocationsView::UpdateUI(bool first) {
    auto& ui = Frame()->GetUI();
    ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
    if (first)
        Frame()->SetStatusText(1, std::format(L"Relocations: {}", m_Relocations.size()).c_str());
}

LRESULT CELFRelocationsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"Address", LVCFMT_RIGHT, 120, ColumnType::Address);
    cm->AddColumn(L"Type",    LVCFMT_LEFT,  120, ColumnType::Type);
    cm->AddColumn(L"Addend",  LVCFMT_RIGHT, 90,  ColumnType::Addend);
    cm->AddColumn(L"Symbol",  LVCFMT_LEFT,  200, ColumnType::Symbol);
    cm->AddColumn(L"Section", LVCFMT_LEFT,  120, ColumnType::Section);
    cm->AddColumn(L"Kind",    LVCFMT_LEFT,  80,  ColumnType::Kind);

    m_Relocations = m_ELF.GetRelocations();
    m_List.SetItemCount((int)m_Relocations.size());
    UpdateUI(true);
    return 0;
}

LRESULT CELFRelocationsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
    return 0;
}
