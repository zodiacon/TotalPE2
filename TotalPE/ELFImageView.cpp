#include "pch.h"
#include "ELFImageView.h"
#include "resource.h"

CELFImageView::CELFImageView(IMainFrame* frame, ELFFile const& elf)
    : CViewBase(frame), m_ELF(elf) {}

CString CELFImageView::GetTitle() const { return L"ELF Header"; }

void CELFImageView::UpdateUI(bool) {}

LRESULT CELFImageView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

    m_List.InsertColumn(0, L"Property", LVCFMT_LEFT, 200);
    m_List.InsertColumn(1, L"Value",    LVCFMT_LEFT, 400);

    auto& info = m_ELF.GetELFInfo();

    auto add = [&](PCWSTR name, std::wstring_view val) {
        int row = m_List.GetItemCount();
        m_List.InsertItem(row, name);
        m_List.SetItemText(row, 1, std::wstring(val).c_str());
    };

    add(L"Class",            info.ClassName);
    add(L"Endianness",       info.Endianness);
    add(L"OS/ABI",           info.OSABI);
    add(L"File Type",        info.FileType);
    add(L"Machine",          info.Machine);
    add(L"Entry Point",      std::format(L"0x{:X}", info.EntryPoint));
    add(L"Flags",            std::format(L"0x{:X}", info.Flags));
    add(L"Sections",         std::format(L"{}", info.SectionCount));
    add(L"Segments",         std::format(L"{}", info.SegmentCount));
    add(L"Static Symbols",   std::format(L"{}", info.StaticSymbolCount));
    add(L"Dynamic Symbols",  std::format(L"{}", info.DynamicSymbolCount));
    add(L"Dynamic Entries",  std::format(L"{}", info.DynamicEntryCount));
    add(L"Relocations",      std::format(L"{}", info.RelocationCount));
    add(L"Notes",            std::format(L"{}", info.NoteCount));
    add(L"File Size",        std::format(L"{} bytes", m_ELF.GetFileSize()));
    add(L"Image Base",       std::format(L"0x{:X}", m_ELF.GetImageBase()));

    Frame()->SetStatusText(1, std::format(L"ELF {}", info.Machine).c_str());
    return 0;
}

LRESULT CELFImageView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto w = LOWORD(lParam), h = HIWORD(lParam);
    if (m_List)
        m_List.MoveWindow(0, 0, w, h);
    return 0;
}
