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
    m_TL.GetTreeControl().DeleteAllItems();
    auto hRoot = m_TL.GetTreeControl().InsertItem(m_Object.Name().c_str(), 0, 0, TVI_ROOT, TVI_LAST);
    Helpers::FillTreeListView(Frame(), m_TL, hRoot, m_Object, Frame()->GetSymbols(), address);
    m_TL.GetTreeControl().Expand(hRoot, TVE_EXPAND);
    m_HexView.SetData(address, (uint32_t)m_Object.Length());
}

LRESULT CStructView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

    m_TL.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN 
        | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_HASLINES);

    m_TL.GetTreeControl().SetImageList(Frame()->GetImageList(), TVSIL_NORMAL);
    HDITEM hdi{ HDI_TEXT | HDI_FORMAT | HDI_WIDTH };
    hdi.fmt = LVCFMT_LEFT;
    hdi.cxy = 200;
    hdi.pszText = (PWSTR)L"Member";
    m_TL.GetHeaderControl().AddItem(&hdi);

    hdi.fmt = LVCFMT_RIGHT;
    hdi.pszText = (PWSTR)L"Offset";
    hdi.cxy = 60;
    m_TL.GetHeaderControl().AddItem(&hdi);

    hdi.fmt = LVCFMT_CENTER;
    hdi.pszText = (PWSTR)L"Type";
    hdi.cxy = 180;
    m_TL.GetHeaderControl().AddItem(&hdi);

    hdi.fmt = LVCFMT_RIGHT;
    hdi.pszText = (PWSTR)L"Value";
    hdi.cxy = 150;
    m_TL.GetHeaderControl().AddItem(&hdi);

    m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
    m_HexView.SetStatic(true);
    m_Splitter.SetSplitterPanes(m_TL, m_HexView);
    m_Splitter.SetSplitterPosPct(70);

    return 0;
}

LRESULT CStructView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    return 0;
}
