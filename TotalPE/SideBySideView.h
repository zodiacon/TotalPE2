#pragma once

#include "ViewBase.h"
#include "ScintillaView.h"
#include <GhidraDecompiler.h>

class PEFile;

//
// A function's disassembly and its C side by side: each statement of the C is on the row of the first instruction it was
// made from (as far as the order of the instructions allows), the two scroll together, and the caret's line on one side
// marks the lines it corresponds to on the other.
//
class CSideBySideView : public CViewBase<CSideBySideView> {
public:
	CSideBySideView(IMainFrame* frame, PEFile const& pe, PCWSTR title);

	CString GetTitle() const override;
	// the disassembly of the function, then its C (again when it changes: the C of a function is made on another thread),
	// with the addresses of the instructions of each line
	bool SetAsm(std::span<const std::byte> code, uint64_t address, CpuArch arch);
	void SetC(std::string const& c, GhidraDecompiler::LineAddresses lines, std::unordered_map<std::wstring, uint64_t> links);

	int64_t GetNavigationPosition() const override;
	void SetNavigationPosition(int64_t va) override;
	bool GoToAddress(uint64_t va) override;
	void XrefsChanged() override;

	BEGIN_MSG_MAP(CSideBySideView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnForward)
		CHAIN_MSG_MAP(CViewBase<CSideBySideView>)
	ALT_MSG_MAP(1)
		// the frame's commands are for the side the user is in
		if (uMsg == WM_COMMAND && m_Active && m_Active->ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult, 1))
			return TRUE;
		CHAIN_MSG_MAP_ALT(CViewBase<CSideBySideView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);

	void OnNotify(CScintillaView& side, SCNotification const& scn);
	void Align();
	void LinkFrom(CScintillaView& side);

	CString m_Title;
	PEFile const& m_PE;
	CSplitterWindow m_Splitter;
	CScintillaView* m_Asm{ nullptr };
	CScintillaView* m_C{ nullptr };
	CScintillaView* m_Active{ nullptr };		// the side the user was in last
	GhidraDecompiler::LineAddresses m_Lines;	// the addresses of each line of the C
	bool m_Syncing{ false };
};
