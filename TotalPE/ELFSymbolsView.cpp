#include "pch.h"
#include "ELFSymbolsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "resource.h"

CELFSymbolsView::CELFSymbolsView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFSymbolsView::GetTitle() const { return L"ELF Symbols"; }

CString CELFSymbolsView::GetColumnText(HWND h, int row, int col) const {
    auto& s = m_Symbols[row];
    switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
        case ColumnType::Name:       return s.Name.c_str();
        case ColumnType::Value:      return s.Value ? std::format(L"0x{:X}", s.Value).c_str() : L"";
        case ColumnType::Size:       return s.Size ? std::format(L"{}", s.Size).c_str() : L"";
        case ColumnType::Type:       return s.TypeStr.c_str();
        case ColumnType::Binding:    return s.BindingStr.c_str();
        case ColumnType::Visibility: return s.VisibilityStr.c_str();
        case ColumnType::Section:    return s.SectionIndex ? std::format(L"{}", s.SectionIndex).c_str() : L"";
        case ColumnType::Kind:       return s.IsDynamic ? L"Dynamic" : L"Static";
    }
    return L"";
}

int CELFSymbolsView::GetRowImage(HWND, int, int) const { return Frame()->GetIconIndex(IDI_EXPORTS); }

void CELFSymbolsView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    std::ranges::sort(m_Symbols, [&](auto& a, auto& b) {
        switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
            case ColumnType::Name:       return SortHelper::Sort(a.Name, b.Name, asc);
            case ColumnType::Value:      return SortHelper::Sort(a.Value, b.Value, asc);
            case ColumnType::Size:       return SortHelper::Sort(a.Size, b.Size, asc);
            case ColumnType::Type:       return SortHelper::Sort(a.TypeStr, b.TypeStr, asc);
            case ColumnType::Binding:    return SortHelper::Sort(a.BindingStr, b.BindingStr, asc);
            case ColumnType::Visibility: return SortHelper::Sort(a.VisibilityStr, b.VisibilityStr, asc);
            case ColumnType::Section:    return SortHelper::Sort(a.SectionIndex, b.SectionIndex, asc);
            case ColumnType::Kind:       return SortHelper::Sort(a.IsDynamic, b.IsDynamic, asc);
        }
        return false;
    });
}

void CELFSymbolsView::OnStateChanged(HWND, int, int, DWORD, DWORD newState) {
    if (newState & LVIS_SELECTED) UpdateUI();
}

void CELFSymbolsView::UpdateUI(bool first) {
    auto& ui = Frame()->GetUI();
    ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
    if (first)
        Frame()->SetStatusText(1, std::format(L"Symbols: {}", m_Symbols.size()).c_str());
}

LRESULT CELFSymbolsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"Name",       LVCFMT_LEFT,  250, ColumnType::Name);
    cm->AddColumn(L"Value",      LVCFMT_RIGHT, 120, ColumnType::Value);
    cm->AddColumn(L"Size",       LVCFMT_RIGHT, 70,  ColumnType::Size);
    cm->AddColumn(L"Type",       LVCFMT_LEFT,  80,  ColumnType::Type);
    cm->AddColumn(L"Binding",    LVCFMT_LEFT,  80,  ColumnType::Binding);
    cm->AddColumn(L"Visibility", LVCFMT_LEFT,  80,  ColumnType::Visibility);
    cm->AddColumn(L"Section",    LVCFMT_LEFT,  120, ColumnType::Section);
    cm->AddColumn(L"Kind",       LVCFMT_LEFT,  70,  ColumnType::Kind);

    m_Symbols = m_ELF.GetAllSymbols();
    m_List.SetItemCount((int)m_Symbols.size());
    UpdateUI(true);
    return 0;
}

LRESULT CELFSymbolsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
    return 0;
}
