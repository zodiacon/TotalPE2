#include "pch.h"
#include "ELFDynamicView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include <ranges>
#include "resource.h"

CELFDynamicView::CELFDynamicView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFDynamicView::GetTitle() const { return L"ELF Dynamic"; }

CString CELFDynamicView::GetColumnText(HWND h, int row, int col) const {
    auto& e = m_Entries[row];
    switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
        case ColumnType::Tag:   return e.TagStr.c_str();
        case ColumnType::Value: return e.Value ? std::format(L"0x{:X}", e.Value).c_str() : L"";
        case ColumnType::Name:  return e.ValueStr.c_str();
    }
    return L"";
}

int CELFDynamicView::GetRowImage(HWND, int, int) const { return Frame()->GetIconIndex(IDI_IMPORTS); }

void CELFDynamicView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    std::ranges::sort(m_Entries, [&](auto& a, auto& b) {
        switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
            case ColumnType::Tag:   return SortHelper::Sort(a.TagStr, b.TagStr, asc);
            case ColumnType::Value: return SortHelper::Sort(a.Value, b.Value, asc);
        }
        return false;
    });
}

void CELFDynamicView::OnStateChanged(HWND, int, int, DWORD, DWORD newState) {
    if (newState & LVIS_SELECTED) UpdateUI();
}

void CELFDynamicView::UpdateUI(bool first) {
    auto& ui = Frame()->GetUI();
    ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
    if (first)
        Frame()->SetStatusText(1, std::format(L"Dynamic entries: {}", m_Entries.size()).c_str());
}

LRESULT CELFDynamicView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"Tag",   LVCFMT_LEFT,  150, ColumnType::Tag);
    cm->AddColumn(L"Value", LVCFMT_RIGHT, 120, ColumnType::Value);
    cm->AddColumn(L"Name",  LVCFMT_LEFT,  300, ColumnType::Name);

    m_Entries = m_ELF.GetDynamicEntries();
    m_List.SetItemCount((int)m_Entries.size());
    UpdateUI(true);
    return 0;
}

LRESULT CELFDynamicView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
    return 0;
}
