#include "pch.h"
#include "TextView.h"

CTextView::CTextView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

void CTextView::SetXmlText(PCWSTR text) {
    m_Edit.SetWindowText(text);
}

CString CTextView::GetTitle() const {
    return m_Title;
}

LRESULT CTextView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_Edit.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | WS_HSCROLL | WS_VSCROLL);

    CFont font;
    font.CreatePointFont(110, L"Consolas");
    m_Edit.SetFont(font.Detach());
    m_Edit.SetTabStops(10);

    return 0;
}
