// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "ScintillaView.h"
#include "SciLexer.h"
#include "LexerModule.h"

extern Lexilla::LexerModule lmXML;
extern Lexilla::LexerModule lmAs;

CScintillaView::CScintillaView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CScintillaView::GetTitle() const {
	return m_Title;
}

CScintillaCtrl& CScintillaView::GetCtrl() {
	return m_Sci;
}

void CScintillaView::SetText(PCWSTR text) {
	m_Sci.SetText(CStringA(text));
}

void CScintillaView::SetText(PCSTR text) {
	m_Sci.SetText(text);
}

void CScintillaView::SetLanguage(LexLanguage lang) {
	switch (lang) {
		case LexLanguage::Asm:
			m_Sci.SetLexer(lmAs.Create());
			break;

		case LexLanguage::Xml:
			m_Sci.SetLexer(lmXML.Create());
			break;
	}
}

LRESULT CScintillaView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Sci.Focus();
	return 0;
}

LRESULT CScintillaView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_Sci.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

	m_Sci.StyleSetFont(STYLE_DEFAULT, "Consolas");
	m_Sci.StyleSetSize(STYLE_DEFAULT, 11);
	m_Sci.StyleSetFore(SCE_PROPS_COMMENT, RGB(0, 128, 0));
	m_Sci.StyleSetFore(SCE_PROPS_SECTION, RGB(160, 0, 0));
	m_Sci.StyleSetFore(SCE_PROPS_ASSIGNMENT, RGB(0, 0, 255));
	m_Sci.StyleSetFore(SCE_PROPS_DEFVAL, RGB(255, 0, 255));

	return 0;
}

