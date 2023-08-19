// View.h : interface of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ScintillaCtrl.h"
#include "ViewBase.h"

class PEFile;

enum class LexLanguage {
	Xml,
	Asm,
};

class CScintillaView :
	public CViewBase<CScintillaView> {
public:
	CScintillaView(IMainFrame* frame, PEFile const& pe, PCWSTR title);

	CString GetTitle() const override;
	CScintillaCtrl& GetCtrl();
	
	void UpdateUI(bool first = false);

	bool SetAsmCode(std::span<const std::byte> code, uint64_t address, bool is32Bit);

	void SetText(PCWSTR text);
	void SetText(PCSTR text);
	void SetLanguage(LexLanguage lang);

	BEGIN_MSG_MAP(CScintillaView)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CViewBase<CScintillaView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_GOTOADDRESS, OnGoToAddress)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_DISASSEMBLEATTHEEND, OnDisassembleAtEnd)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_DISASSEMBLEINANEWTAB, OnDisassembleNewTab)
		CHAIN_MSG_MAP_ALT(CViewBase<CScintillaView>, 1)
	END_MSG_MAP()

private:
	void UpdateColors();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateTheme(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGoToAddress(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDisassembleNewTab(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDisassembleAtEnd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CString m_Title;
	CScintillaCtrl m_Sci;
	LexLanguage m_Language;
	PEFile const& m_PE;
	bool m_Is32Bit;
};
