#pragma once

#include "ViewBase.h"
#include "resource.h"
#include "ScintillaView.h"
#include "Win32Api.h"
#include <QuickFindEdit.h>
#include <VirtualListView.h>
#include <unordered_set>

//
// The functions and types of Windows, from the Win32 metadata: a list (the filter shows those whose names contain its text;
// Imports: the functions the file imports), and the C declaration of the one selected: a function's prototype, an enum's
// values, a struct's fields and their offsets, an interface's methods. Enter (or a double-click) on a line of the
// declaration shows the type the line refers to (Back, or Alt+Left, returns). Ctrl+Q or Ctrl+F goes to the filter; Down or
// Enter there goes to the list. It is in a window of its own (CWin32ApiFrame).
//
class CWin32ApiView :
	public CViewBase<CWin32ApiView>,
	public CVirtualListView<CWin32ApiView> {
public:
	CWin32ApiView(IMainFrame* frame, PEFile const& pe, Win32Api& api, std::unordered_set<std::string> imports);

	CString GetTitle() const override;
	void UpdateUI(bool first = false);
	BOOL PreTranslateMessage(MSG* pMsg);
	// selects a function or type by its name (the filter is cleared if it hides it); false if there is none
	bool Select(std::string const& name);
	// another file was opened (or none is): the API of its machine, the functions it imports
	void SetFile(Win32Api& api, std::unordered_set<std::string> imports);
	// shows a function or type, which Back returns from
	bool Navigate(std::string const& name);
	void GoBack();
	void GoForward();
	// where the counts are shown
	void SetStatusBar(HWND hStatusBar);
	int64_t GetNavigationPosition() const override;
	void SetNavigationPosition(int64_t position) override;

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_MSG_MAP(CWin32ApiView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnForward)
		NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_WIN32_IMPORTS, OnImports)
		COMMAND_ID_HANDLER(ID_NAV_BACK, OnBack)
		COMMAND_ID_HANDLER(ID_NAV_FORWARD, OnForwardCommand)
		CHAIN_MSG_MAP(CVirtualListView<CWin32ApiView>)
		CHAIN_MSG_MAP(CViewBase<CWin32ApiView>)
	ALT_MSG_MAP(1)
		// the frame's commands (copy, find) are for the declaration when the user is in it
		if (uMsg == WM_COMMAND && m_Code && ::GetFocus() == m_Code->GetCtrl().m_hWnd &&
			m_Code->ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult, 1))
			return TRUE;
		CHAIN_MSG_MAP_ALT(CViewBase<CWin32ApiView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnImports(WORD, WORD, HWND, BOOL&);
	LRESULT OnBack(WORD, WORD, HWND, BOOL&);
	LRESULT OnForwardCommand(WORD, WORD, HWND, BOOL&);

	void Fill();
	void UpdateImportsButton();
	void UpdateHistoryButtons();
	void Remember(std::string const& name);
	// the entry shown again from the history (by name: it may be of another machine)
	bool ShowFromHistory(std::string const& name);
	void Show(int row);
	bool GoToLine(int line);
	int SelectedEntry() const;

	PEFile const& m_PE;
	Win32Api* m_Api;
	std::unordered_set<std::string> m_Imports;	// the names of the functions the file imports
	bool m_ImportsOnly{ false };
	std::string m_Filter;		// in lowercase
	std::vector<size_t> m_Items;	// the entries shown (indices into the API's entries)
	std::vector<std::string> m_LineTypes;	// what each line of the declaration refers to
	CSplitterWindow m_Splitter;
	CListViewCtrl m_List;
	CScintillaView* m_Code{ nullptr };
	CQuickFindEdit m_FilterEdit;
	CToolBarCtrl m_tb;
	int m_Shown{ -1 };	// the entry whose declaration is shown
	// What was shown, by name: what the user selected in the list (if it was shown for a while: not each one the arrow
	// keys pass), and the types followed from the declarations
	std::vector<std::string> m_Back, m_Forward;
	ULONGLONG m_ShownAt{ 0 };	// when the one shown was
	bool m_Navigating{ false };	// (selections that are not the user's)
	HWND m_hStatusBar{ nullptr };
};
