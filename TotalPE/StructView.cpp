#include "pch.h"
#include "StructView.h"
#include "Helpers.h"

CStructView::CStructView(IMainFrame* frame, DiaSymbol const& sym, CString const& title) : CViewBase(frame), m_Object(sym), m_Title(title), m_HexView(frame) {
}

void CStructView::SetValue(PVOID address) {
    m_TL.GetTreeControl().DeleteAllItems();
    auto hRoot = m_TL.GetTreeControl().InsertItem(m_Object.Name().c_str(), 0, 0, TVI_ROOT, TVI_LAST);
    m_TL.SetSubItemState(hRoot, 0, TLVIS_BOLD, TLVIS_BOLD);
    Helpers::FillTreeListView(m_TL, hRoot, m_Object, Frame()->GetSymbols(), address);
    m_TL.GetTreeControl().Expand(hRoot, TVE_EXPAND);
    m_HexView.SetData(address, (uint32_t)m_Object.Length());
}

LRESULT CStructView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

    m_TL.SetHeaderStyle(HDS_FULLDRAG | WS_CHILD | WS_VISIBLE);
    m_TL.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
        TVS_HASBUTTONS | TVS_LINESATROOT | TVS_HASLINES | TVS_SHOWSELALWAYS);
    m_TL.GetTreeControl().SetExtendedStyle(TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    m_TL.AddColumn(L"Member", 200, HDF_LEFT);
    m_TL.AddColumn(L"Offset", 50, HDF_CENTER);
    m_TL.AddColumn(L"Type", 180, HDF_LEFT);
    m_TL.AddColumn(L"Value", 150, HDF_LEFT);
    m_TL.AddColumn(L"Details", 150, HDF_LEFT);

    m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
    m_HexView.SetStatic(true);
    m_Splitter.SetSplitterPanes(m_TL, m_HexView);
    m_Splitter.SetSplitterPosPct(70);

    return 0;
}

LRESULT CStructView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    return 0;
}
