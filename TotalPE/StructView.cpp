#include "pch.h"
#include "StructView.h"
#include "Helpers.h"
#include <DpiHelper.h>
#include <ListViewhelper.h>
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
    m_TL.BeginUpdate();
    m_TL.DeleteAllItems();
    auto hRoot = m_TL.InsertItem(TLV_ROOT, TLV_LAST, m_Object.Name().c_str(), Frame()->GetIconIndex(IDI_TYPE));
    Helpers::FillTreeListView(Frame(), m_TL, hRoot, m_Object, Frame()->GetSymbols(), address);
    // only the members of the object itself are shown at first (items are created expanded)
    m_TL.CollapseAll(hRoot);
    m_TL.EndUpdate();
    m_TL.Expand(hRoot);
    m_HexView.SetData(address, (uint32_t)m_Object.Length());
}

LRESULT CStructView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

    m_TL.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

    // the image list is the frame's: not to be destroyed with the control
    m_TL.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
    m_TL.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
    m_TL.InsertColumn(0, L"Member", DpiHelper::Scale(250, m_TL));
    m_TL.InsertColumn(1, L"Offset", DpiHelper::Scale(60, m_TL), LVCFMT_RIGHT);
    m_TL.InsertColumn(2, L"Type", DpiHelper::Scale(180, m_TL), LVCFMT_CENTER);
    m_TL.InsertColumn(3, L"Value", DpiHelper::Scale(150, m_TL), LVCFMT_RIGHT);
    ListViewHelper::EnableColumnDpiScaling(m_TL.GetListCtrl());

    m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
    m_HexView.SetStatic(true);
    m_Splitter.SetSplitterPanes(m_TL, m_HexView);
    m_Splitter.SetSplitterPosPct(70);

    return 0;
}

LRESULT CStructView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    return 0;
}
