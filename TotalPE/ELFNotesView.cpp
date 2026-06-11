#include "pch.h"
#include "ELFNotesView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "resource.h"

CELFNotesView::CELFNotesView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFNotesView::GetTitle() const { return L"ELF Notes"; }

CString CELFNotesView::GetColumnText(HWND h, int row, int col) const {
    auto& n = m_Notes[row];
    switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
        case ColumnType::Name:            return n.Name.c_str();
        case ColumnType::Type:            return std::format(L"0x{:X}", n.Type).c_str();
        case ColumnType::DescriptionSize: return std::format(L"{}", n.DescriptionSize).c_str();
    }
    return L"";
}

int CELFNotesView::GetRowImage(HWND, int, int) const { return Frame()->GetIconIndex(IDI_SECTION); }

void CELFNotesView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    std::ranges::sort(m_Notes, [&](auto& a, auto& b) {
        switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
            case ColumnType::Name:            return SortHelper::Sort(a.Name, b.Name, asc);
            case ColumnType::Type:            return SortHelper::Sort(a.Type, b.Type, asc);
            case ColumnType::DescriptionSize: return SortHelper::Sort(a.DescriptionSize, b.DescriptionSize, asc);
        }
        return false;
    });
}

void CELFNotesView::OnStateChanged(HWND, int, int, DWORD, DWORD newState) {
    if (newState & LVIS_SELECTED) UpdateUI();
}

void CELFNotesView::UpdateUI(bool first) {
    auto& ui = Frame()->GetUI();
    ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
    if (first)
        Frame()->SetStatusText(1, std::format(L"Notes: {}", m_Notes.size()).c_str());
}

LRESULT CELFNotesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"Name",             LVCFMT_LEFT,  200, ColumnType::Name);
    cm->AddColumn(L"Type",             LVCFMT_LEFT,  150, ColumnType::Type);
    cm->AddColumn(L"Description Size", LVCFMT_RIGHT, 120, ColumnType::DescriptionSize);

    m_Notes = m_ELF.GetNotes();
    m_List.SetItemCount((int)m_Notes.size());
    UpdateUI(true);
    return 0;
}

LRESULT CELFNotesView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
    return 0;
}
