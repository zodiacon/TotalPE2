// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "ScintillaView.h"
#include "SciLexer.h"
#include "LexerModule.h"
#include "..\External\Capstone\capstone.h"
#include "PEStrings.h"

extern Lexilla::LexerModule lmXML;
extern Lexilla::LexerModule lmAsm;

CScintillaView::CScintillaView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CScintillaView::GetTitle() const {
	return m_Title;
}

CScintillaCtrl& CScintillaView::GetCtrl() {
	return m_Sci;
}

bool CScintillaView::SetAsmCode(std::span<const std::byte> code, uint64_t address, bool is32Bit) {
	csh handle;
	if (cs_open(CS_ARCH_X86, is32Bit ? CS_MODE_32 : CS_MODE_64, &handle) != CS_ERR_OK)
		return false;
	auto bytes = (const uint8_t*)code.data();
	auto size = code.size();
	cs_insn inst{};
	CStringA text;
	while (cs_disasm_iter(handle, &bytes, &size, &address, &inst)) {
		text += PEStrings::FormatInstruction(inst) + L"\r\n";
		if (_strcmpi(inst.mnemonic, "ret") == 0)
			break;
	}

	m_Sci.SetText(text);
	cs_close(&handle);
	return true;
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
			m_Sci.SetLexer(lmAsm.Create());
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

