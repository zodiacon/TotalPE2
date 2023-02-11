#include "pch.h"
#include "StructView.h"
#include "Helpers.h"
#include "PEFile.h"

CStructView::CStructView(IMainFrame* frame, DiaSymbol const& sym, CString const& title) : CViewBase(frame), m_Object(sym), m_Title(title), m_HexView(frame) {
}

void CStructView::SetValue(PVOID address) {
    ShowObject(address);
}

void CStructView::SetPEOffset(PEFile const& pe, DWORD offset) {
    auto size = (uint32_t)m_Object.Length();
    ShowObject((PVOID)(pe.GetData() + offset));
}

void CStructView::ShowObject(PVOID address) {
    m_TL.DeleteAllItems();
    auto hRoot = m_TL.AddItem(m_Object.Name().c_str(), 0);
    Helpers::FillTreeListView(Frame(), m_TL, hRoot, m_Object, Frame()->GetSymbols(), address);
    m_HexView.SetData(address, (uint32_t)m_Object.Length());
}

LRESULT CStructView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

    m_TL.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
        LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_NOSORTHEADER);
    m_TL.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
    m_TL.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
    m_TL.InsertColumn(0, L"Member", LVCFMT_LEFT, 250);
    m_TL.InsertColumn(1, L"Offset", LVCFMT_RIGHT, 60);
    m_TL.InsertColumn(2, L"Type", 0, 180);
    m_TL.InsertColumn(3, L"Value", LVCFMT_RIGHT, 150);

    m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
    m_HexView.SetStatic(true);
    m_Splitter.SetSplitterPanes(m_TL, m_HexView);
    m_Splitter.SetSplitterPosPct(70);

    return 0;
}

LRESULT CStructView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    return 0;
}
