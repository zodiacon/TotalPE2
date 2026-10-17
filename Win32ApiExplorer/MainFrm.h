#pragma once

#include "resource.h"
#include "StripHost.h"
#include "DeclarationView.h"
#include "SearchPane.h"
#include "NamespacePane.h"
#include "MachineBar.h"
#include "QuickOpen.h"
#include <CustomSplitterWindow.h>
#include <DocPane.h>

//
// The main window: on the left, the search (a list), the namespaces (a tree) and what uses a type (a list), in tabs; on the right, the declarations
// of what was opened, in tabs, and below them Microsoft's documentation of the one shown. What the left side selects is
// shown in a preview tab (its title in italics), which the next one replaces; Enter or a double-click (or following a
// type, or Keep Open) makes a tab stay. The machine (x86, x64, ARM64) chooses the functions there are, and the layout of
// the structs.
//
class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CUpdateUI<CMainFrame>,
	public CMessageFilter,
	public CIdleHandler {
public:
	DECLARE_FRAME_WND_CLASS(L"Win32ApiExplorerWndClass", IDR_MAINFRAME)

	// what to show first, if anything (a name given on the command line)
	explicit CMainFrame(std::wstring startName = {});

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;

	BEGIN_UPDATE_UI_MAP(CMainFrame)
		UPDATE_ELEMENT(ID_GO_BACK, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_GO_FORWARD, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_GO_DEFINITION, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_GO_DEFINITION_NEWTAB, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_GO_REVEAL, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_GO_USEDBY, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_DOC_BROWSER, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_VIEW_DOCUMENTATION, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_VIEW_DARKMODE, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_SEARCH, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_NAMESPACES, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_USEDBY, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_MACHINE_X86, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_MACHINE_X64, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_MACHINE_ARM64, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_TAB_CLOSE, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_TAB_CLOSEOTHERS, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_TAB_CLOSEALL, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_TAB_NEXT, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_TAB_PREV, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_TAB_KEEPOPEN, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_EDIT_COPYNAME, UPDUI_MENUPOPUP)
	END_UPDATE_UI_MAP()

	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_APPCOMMAND, OnAppCommand)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_SELECT_ALL, OnSelectAll)
		COMMAND_ID_HANDLER(ID_EDIT_COPYNAME, OnCopyName)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SEARCH, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_NAMESPACES, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_USEDBY, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_DOCUMENTATION, OnViewDocumentation)
		COMMAND_ID_HANDLER(ID_VIEW_DARKMODE, OnViewDarkMode)
		COMMAND_ID_HANDLER(ID_VIEW_FONT, OnViewFont)
		COMMAND_ID_HANDLER(ID_VIEW_UIFONT, OnViewUIFont)
		COMMAND_ID_HANDLER(ID_VIEW_RESETFONTS, OnResetFonts)
		COMMAND_ID_HANDLER(ID_GO_BACK, OnBack)
		COMMAND_ID_HANDLER(ID_GO_FORWARD, OnForward)
		COMMAND_ID_HANDLER(ID_GO_DEFINITION, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_GO_DEFINITION_NEWTAB, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_GO_REVEAL, OnReveal)
		COMMAND_ID_HANDLER(ID_GO_USEDBY, OnFindUsages)
		COMMAND_ID_HANDLER(ID_GO_QUICKOPEN, OnQuickOpen)
		COMMAND_ID_HANDLER(ID_DOC_BROWSER, OnOpenInBrowser)
		COMMAND_RANGE_HANDLER(ID_MACHINE_X86, ID_MACHINE_ARM64, OnMachine)
		COMMAND_HANDLER(IDC_MACHINE, CBN_SELCHANGE, OnMachineCombo)
		COMMAND_ID_HANDLER(ID_TAB_CLOSE, OnCloseTab)
		COMMAND_ID_HANDLER(ID_TAB_CLOSEOTHERS, OnCloseOtherTabs)
		COMMAND_ID_HANDLER(ID_TAB_CLOSEALL, OnCloseAllTabs)
		COMMAND_ID_HANDLER(ID_TAB_NEXT, OnNextTab)
		COMMAND_ID_HANDLER(ID_TAB_PREV, OnNextTab)
		COMMAND_ID_HANDLER(ID_TAB_KEEPOPEN, OnKeepOpen)
		CHAIN_MSG_MAP(CUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

private:
	struct Tab {
		std::unique_ptr<CDeclarationView> View;
		bool Preview{ false };
	};

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAppCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnExit(WORD, WORD, HWND, BOOL&);
	LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
	LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopyName(WORD, WORD, HWND, BOOL&);
	LRESULT OnFind(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewNavigator(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewDocumentation(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewDarkMode(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewFont(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewUIFont(WORD, WORD, HWND, BOOL&);
	LRESULT OnResetFonts(WORD, WORD, HWND, BOOL&);
	LRESULT OnBack(WORD, WORD, HWND, BOOL&);
	LRESULT OnForward(WORD, WORD, HWND, BOOL&);
	LRESULT OnGoToDefinition(WORD, WORD, HWND, BOOL&);
	LRESULT OnReveal(WORD, WORD, HWND, BOOL&);
	LRESULT OnFindUsages(WORD, WORD, HWND, BOOL&);
	LRESULT OnQuickOpen(WORD, WORD, HWND, BOOL&);
	LRESULT OnOpenInBrowser(WORD, WORD, HWND, BOOL&);
	LRESULT OnMachine(WORD, WORD, HWND, BOOL&);
	LRESULT OnMachineCombo(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseTab(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseOtherTabs(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseAllTabs(WORD, WORD, HWND, BOOL&);
	LRESULT OnNextTab(WORD, WORD, HWND, BOOL&);
	LRESULT OnKeepOpen(WORD, WORD, HWND, BOOL&);

	void CreateToolBar();
	void CreateNavigator();
	void CreateEditors();

	// opens an entry: in the tab that shows it, if there is one; else in the preview (replacing what it showed) or in a
	// tab of its own (pin); name: one to select in it (a value of an enum)
	void OpenEntry(size_t entry, bool pin, std::string const& name = {});
	// (position: after the active one if it is -1)
	int AddTab(int entry, bool preview, int position = -1);
	void ActivateTab(int index);
	void CloseTab(int index);
	void Pin(int index);
	int ActiveTab() const;
	// the tab of a command: the one whose menu is shown, else the active one
	int TargetTab() const;
	CDeclarationView* ActiveView() const;
	int TabOf(CDeclarationView const* view) const;
	int TabShowing(int entry) const;
	// a view's type followed (here, or in a tab of its own)
	void Follow(CDeclarationView* view, std::string const& name, bool newTab);
	void ViewChanged(CDeclarationView* view);
	void ShowTabMenu(int index, CPoint pt);

	void ShowNavigatorPage(int page);
	// what uses a type, in the Used By page (which is shown)
	void FindUsages(size_t entry);
	// the scope of the Used By page: of the type it was asked for (by name: of the machine's API)
	void UpdateUsedBy();
	// the type whose usages Find Usages finds: the tab's whose menu is shown, else the one the caret's line refers to, else
	// the active tab's (-1: none, or not a type)
	int UsagesTarget() const;
	void SetMachine(Win32Api::Machine machine);
	void UpdateDocumentation();
	void UpdateStatus();
	void UpdateTitle();
	void UpdateUIState();
	void ApplyColors();
	// the interface font (of the settings, else the system's) to the lists, tree, tabs, machine and status bar
	void ApplyUIFont();
	static LOGFONT UIFont();
	void SaveSettings();
	void RestoreTabs();

	Win32Api* m_Api{ nullptr };
	Win32Api::Machine m_Machine{ Win32Api::Machine::X64 };
	std::wstring m_StartName;
	CImageList m_Images;
	CCustomSplitterWindow m_Splitter;
	CCustomHorSplitterWindow m_Right;
	CStripHost m_Navigator, m_Editors;
	std::unique_ptr<CSearchPane> m_Search;
	std::unique_ptr<CNamespacePane> m_Namespaces;
	std::unique_ptr<CSearchPane> m_UsedBy;
	std::string m_UsedBySubject;		// the type whose usages are shown (empty: none)
	std::vector<size_t> const m_NoEntries;
	CDocPane m_Doc;
	std::vector<Tab> m_Tabs;		// as the strip has them
	int m_MenuTab{ -1 };			// the tab whose menu is shown
	CMachineBar m_MachineBar;
	CToolBarCtrl m_ToolBar;
	CFont m_UIFont;
	CQuickOpen m_QuickOpen;
};
