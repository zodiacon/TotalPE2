#include "pch.h"
#include "ELFSectionsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include "resource.h"

CELFSectionsView::CELFSectionsView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFSectionsView::GetTitle() const { return L"ELF Sections"; }

CString CELFSectionsView::GetColumnText(HWND h, int row, int col) const {
    auto& s = m_Sections[row];
    switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
        case ColumnType::Index:          return std::format(L"{}", row).c_str();
        case ColumnType::Name:           return s.Name.c_str();
        case ColumnType::Type:           return s.TypeStr.c_str();
        case ColumnType::Flags:          return s.FlagsStr.c_str();
        case ColumnType::VirtualAddress: return s.VirtualAddress ? std::format(L"0x{:X}", s.VirtualAddress).c_str() : L"";
        case ColumnType::FileOffset:     return std::format(L"0x{:X}", s.FileOffset).c_str();
        case ColumnType::Size:           return std::format(L"0x{:X}", s.Size).c_str();
        case ColumnType::Alignment:      return s.Alignment ? std::format(L"{}", s.Alignment).c_str() : L"";
    }
    return L"";
}

int CELFSectionsView::GetRowImage(HWND, int, int) const { return Frame()->GetIconIndex(IDI_SECTION); }

void CELFSectionsView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    std::ranges::sort(m_Sections, [&](auto& a, auto& b) {
        switch (GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn)) {
            case ColumnType::Name:           return SortHelper::Sort(a.Name, b.Name, asc);
            case ColumnType::Size:           return SortHelper::Sort(a.Size, b.Size, asc);
            case ColumnType::VirtualAddress: return SortHelper::Sort(a.VirtualAddress, b.VirtualAddress, asc);
            case ColumnType::FileOffset:     return SortHelper::Sort(a.FileOffset, b.FileOffset, asc);
        }
        return false;
    });
}

void CELFSectionsView::OnStateChanged(HWND, int, int, DWORD, DWORD newState) {
    if (newState & LVIS_SELECTED) UpdateUI();
}

void CELFSectionsView::UpdateUI(bool first) {
    auto& ui = Frame()->GetUI();
    ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
    if (first)
        Frame()->SetStatusText(1, std::format(L"Sections: {}", m_Sections.size()).c_str());
}

LRESULT CELFSectionsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"#",            LVCFMT_RIGHT, 40,  ColumnType::Index);
    cm->AddColumn(L"Name",         LVCFMT_LEFT,  160, ColumnType::Name);
    cm->AddColumn(L"Type",         LVCFMT_LEFT,  120, ColumnType::Type);
    cm->AddColumn(L"Flags",        LVCFMT_LEFT,  60,  ColumnType::Flags);
    cm->AddColumn(L"Virt Address", LVCFMT_RIGHT, 120, ColumnType::VirtualAddress);
    cm->AddColumn(L"File Offset",  LVCFMT_RIGHT, 110, ColumnType::FileOffset);
    cm->AddColumn(L"Size",         LVCFMT_RIGHT, 90,  ColumnType::Size);
    cm->AddColumn(L"Alignment",    LVCFMT_RIGHT, 80,  ColumnType::Alignment);

    m_Sections = m_ELF.GetELFSections();
    m_List.SetItemCount((int)m_Sections.size());
    UpdateUI(true);
    return 0;
}

LRESULT CELFSectionsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
    return 0;
}
