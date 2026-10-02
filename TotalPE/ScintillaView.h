// View.h : interface of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ScintillaCtrl.h"
#include "ViewBase.h"
#include <optional>
#include <unordered_map>

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

	// Enter follows the call or jump of the line, X lists the references to the instruction
	BOOL PreTranslateMessage(MSG* pMsg);

	void UpdateUI(bool first = false);

	bool SetAsmCode(std::span<const std::byte> code, uint64_t address, bool is32Bit);

	void SetText(PCWSTR text);
	void SetText(PCSTR text);
	void SetLanguage(LexLanguage lang);

	int64_t GetNavigationPosition() const override;
	void SetNavigationPosition(int64_t va) override;
	bool GoToAddress(uint64_t va) override;

	BEGIN_MSG_MAP(CScintillaView)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(SCN_DOUBLECLICK, OnDoubleClick)
		CHAIN_MSG_MAP(CViewBase<CScintillaView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_GOTOADDRESS, OnGoToAddress)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_FOLLOW, OnFollow)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_XREFS_HERE, OnXrefsHere)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_XREFS_TARGET, OnXrefsTarget)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_DISASSEMBLEATTHEEND, OnDisassembleAtEnd)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_DISASSEMBLEINANEWTAB, OnDisassembleNewTab)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnEditCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CScintillaView>, 1)
	END_MSG_MAP()

private:
	// what the user can follow from a line of the disassembly
	struct Line {
		uint64_t Va{ 0 };					// 0 for the lines that are not an instruction (labels)
		std::optional<uint64_t> Branch;		// the target of a call or jump
		std::optional<uint64_t> Memory;		// the address of a memory operand
		std::optional<uint64_t> Target() const { return Branch ? Branch : Memory; }
	};

	void UpdateColors();
	CStringA Disassemble(std::span<const std::byte> code, uint64_t address);
	int CurrentLine() const;
	int InstructionLine(int line) const;
	Line const* GetLine(int line) const;
	void ShowLine(int line);
	bool NavigateTo(uint64_t va);
	bool Follow(int line);

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
	LRESULT OnEditCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFollow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnXrefsHere(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnXrefsTarget(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CString m_Title;
	CScintillaCtrl m_Sci;
	LexLanguage m_Language;
	PEFile const& m_PE;
	bool m_Is32Bit{ false };
	std::vector<Line> m_Lines;						// by line of the text: what each line of the disassembly is
	std::unordered_map<uint64_t, int> m_LineOfVa;	// the line of each instruction
	int m_ContextLine{ -1 };						// the line the context menu was opened on
};
