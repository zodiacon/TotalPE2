#pragma once

#include "resource.h"
#include "StripHost.h"
#include "DeclarationView.h"
#include "SearchPane.h"
#include "NamespacePane.h"
#include "MachineBar.h"
#include "QuickOpen.h"
#include "LoadingView.h"
#include "DecodeBox.h"
#include <thread>
#include <CustomSplitterWindow.h>
#include <DocPane.h>

//
// The main window: on the left, the search (a list), the namespaces (a tree), what uses a type, the favorites and the history
// (lists), in tabs; on the right, the declarations of what was opened, in tabs (and to their side, if one is opened there:
// a declaration to compare with), and below them Microsoft's documentation of the one shown. What the left side selects is
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
		UPDATE_ELEMENT(ID_GO_SIDE, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_GO_USEDBY, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_DOC_BROWSER, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_VIEW_DOCUMENTATION, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_VIEW_DARKMODE, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_SEARCH, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_NAMESPACES, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_USEDBY, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_FAVORITES, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_HISTORY, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_CLEARHISTORY, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_EDIT_FAVORITE, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_SHOW_DECLARATION, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_LAYOUT, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_COMPARE, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
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
		UPDATE_ELEMENT(ID_EDIT_DECODE, UPDUI_MENUPOPUP)
	END_UPDATE_UI_MAP()

	// a name sent by another instance (Win32ApiExp.exe CreateFileW): the data of WM_COPYDATA, in UTF-16
	static constexpr ULONG_PTR CopyDataName = 0x4E333257;	// 'W32N'

	BEGIN_MSG_MAP(CMainFrame)
		// (while the metadata is loaded: Exit and About only)
		MESSAGE_HANDLER(WM_COMMAND, OnCommandWhileLoading)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_LOADED, OnLoaded)
		MESSAGE_HANDLER(WM_COPYDATA, OnCopyData)
		MESSAGE_HANDLER(WM_METADATA_CHECKED, OnMetadataChecked)
		MESSAGE_HANDLER(WM_METADATA_STATUS, OnMetadataStatus)
		MESSAGE_HANDLER(WM_METADATA_INSTALLED, OnMetadataInstalled)
		COMMAND_ID_HANDLER(ID_FILE_METADATAFOLDER, OnMetadataFolder)
		COMMAND_ID_HANDLER(ID_FILE_DEFAULTMETADATA, OnDefaultMetadata)
		COMMAND_ID_HANDLER(ID_FILE_CHECKMETADATA, OnCheckMetadata)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_APPCOMMAND, OnAppCommand)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_SELECT_ALL, OnSelectAll)
		COMMAND_ID_HANDLER(ID_EDIT_COPYNAME, OnCopyName)
		COMMAND_ID_HANDLER(ID_EDIT_DECODE, OnDecode)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SEARCH, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_NAMESPACES, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_USEDBY, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_FAVORITES, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_HISTORY, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_CLEARHISTORY, OnClearHistory)
		COMMAND_ID_HANDLER(ID_EDIT_FAVORITE, OnFavorite)
		COMMAND_ID_HANDLER(ID_VIEW_DOCUMENTATION, OnViewDocumentation)
		COMMAND_ID_HANDLER(ID_VIEW_DARKMODE, OnViewDarkMode)
		COMMAND_ID_HANDLER(ID_VIEW_FONT, OnViewFont)
		COMMAND_RANGE_HANDLER(ID_SHOW_DECLARATION, ID_SHOW_COMPARE, OnShowMode)
		COMMAND_ID_HANDLER(ID_VIEW_UIFONT, OnViewUIFont)
		COMMAND_ID_HANDLER(ID_VIEW_RESETFONTS, OnResetFonts)
		COMMAND_ID_HANDLER(ID_GO_BACK, OnBack)
		COMMAND_ID_HANDLER(ID_GO_FORWARD, OnForward)
		COMMAND_ID_HANDLER(ID_GO_DEFINITION, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_GO_DEFINITION_NEWTAB, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_GO_REVEAL, OnReveal)
		COMMAND_ID_HANDLER(ID_GO_USEDBY, OnFindUsages)
		COMMAND_ID_HANDLER(ID_GO_QUICKOPEN, OnQuickOpen)
		COMMAND_ID_HANDLER(ID_GO_SIDE, OnOpenToSide)
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

	static constexpr UINT WM_LOADED = WM_APP + 1;	// the metadata was loaded (lParam: the API, null if there is none)

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLoaded(UINT, WPARAM, LPARAM, BOOL&);
	// the metadata: chosen, looked for on nuget.org, downloaded (on threads of their own)
	static constexpr UINT WM_METADATA_CHECKED = WM_APP + 2;		// lParam: MetadataVersions* (null: not found)
	static constexpr UINT WM_METADATA_STATUS = WM_APP + 3;		// lParam: std::wstring*
	static constexpr UINT WM_METADATA_INSTALLED = WM_APP + 4;	// wParam: done, lParam: MetadataVersions*
	struct MetadataVersions {
		std::string Win32, Wdk;
		std::wstring Folder;
	};
	LRESULT OnMetadataFolder(WORD, WORD, HWND, BOOL&);
	LRESULT OnDefaultMetadata(WORD, WORD, HWND, BOOL&);
	LRESULT OnCheckMetadata(WORD, WORD, HWND, BOOL&);
	LRESULT OnMetadataChecked(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMetadataStatus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMetadataInstalled(UINT, WPARAM, LPARAM, BOOL&);
	// the metadata is read when the application starts: again now, if the user wants
	void AskRestart();
	LRESULT OnCopyData(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCommandWhileLoading(UINT, WPARAM, LPARAM, BOOL&);
	// what needs the metadata: the panes, the tabs
	void FinishCreate();
	// opens a function, type or constant by its name, in a tab that stays (false if there is none)
	bool OpenByName(std::wstring const& name);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAppCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnExit(WORD, WORD, HWND, BOOL&);
	LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
	LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopyName(WORD, WORD, HWND, BOOL&);
	LRESULT OnDecode(WORD, WORD, HWND, BOOL&);
	LRESULT OnFind(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewNavigator(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewDocumentation(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewDarkMode(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewFont(WORD, WORD, HWND, BOOL&);
	LRESULT OnShowMode(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewUIFont(WORD, WORD, HWND, BOOL&);
	LRESULT OnResetFonts(WORD, WORD, HWND, BOOL&);
	LRESULT OnBack(WORD, WORD, HWND, BOOL&);
	LRESULT OnForward(WORD, WORD, HWND, BOOL&);
	LRESULT OnGoToDefinition(WORD, WORD, HWND, BOOL&);
	LRESULT OnReveal(WORD, WORD, HWND, BOOL&);
	LRESULT OnFindUsages(WORD, WORD, HWND, BOOL&);
	LRESULT OnClearHistory(WORD, WORD, HWND, BOOL&);
	LRESULT OnFavorite(WORD, WORD, HWND, BOOL&);
	LRESULT OnQuickOpen(WORD, WORD, HWND, BOOL&);
	LRESULT OnOpenToSide(WORD, WORD, HWND, BOOL&);
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
	// the view of the keyboard: the side's, if it has the focus, else the active tab's
	CDeclarationView* FocusedView() const;
	// an entry to the side of the tabs (the side is opened, if it is not)
	void OpenToSide(size_t entry);
	void CloseSide();
	void SideChanged();
	// what Open to the Side opens: the tab's whose menu is shown, else what the caret's line refers to, else what is shown
	int SideTarget() const;
	int TabOf(CDeclarationView const* view) const;
	int TabShowing(int entry) const;
	// a view's type followed (here, or in a tab of its own)
	void Follow(CDeclarationView* view, std::string const& name, bool newTab);
	void ViewChanged(CDeclarationView* view);
	void ShowTabMenu(int index, CPoint pt);

	void ShowNavigatorPage(int page);
	// the favorites and the history, by name: their entries (of the machine), in their panes
	void UpdateLists();
	// what was opened (in a tab that stays): first in the history
	void AddToHistory(int entry);
	// what uses a type, in the Used By page (which is shown)
	void FindUsages(size_t entry);
	// the scope of the Used By page: of the type it was asked for (by name: of the machine's API)
	void UpdateUsedBy();
	// the type whose usages Find Usages finds: the tab's whose menu is shown, else the one the caret's line refers to, else
	// the active tab's (-1: none, or not a type)
	int UsagesTarget() const;
	// the enum whose values Decode Value is of: as Find Usages', of an enum (-1: none)
	int DecodeTarget() const;
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
	std::wstring m_StartName;		// to open when the metadata is loaded
	std::thread m_Loader;
	CLoadingView m_Loading;
	bool m_MetadataBusy{ false };	// (looked for, downloaded)
	CImageList m_Images;
	CCustomSplitterWindow m_Splitter;
	CCustomHorSplitterWindow m_Right;
	CStripHost m_Navigator, m_Editors, m_SideHost;
	CCustomSplitterWindow m_EditorSplit;		// the tabs, and the side
	std::unique_ptr<CDeclarationView> m_Side;
	std::unique_ptr<CSearchPane> m_Search;
	std::unique_ptr<CNamespacePane> m_Namespaces;
	std::unique_ptr<CSearchPane> m_UsedBy;
	std::unique_ptr<CSearchPane> m_Favorites, m_History;
	std::vector<std::string> m_FavoriteNames, m_HistoryNames;	// (the history: the last first)
	std::vector<size_t> m_FavoriteEntries, m_HistoryEntries;
	bool m_SwitchingMachine{ false };	// (the tabs show the same as they did: not opened)
	std::string m_UsedBySubject;		// the type whose usages are shown (empty: none)
	std::vector<size_t> const m_NoEntries;
	CDocPane m_Doc;
	std::vector<Tab> m_Tabs;		// as the strip has them
	int m_MenuTab{ -1 };			// the tab whose menu is shown
	CMachineBar m_MachineBar;
	CToolBarCtrl m_ToolBar;
	CFont m_UIFont;
	CQuickOpen m_QuickOpen;
	CDecodeBox m_Decode;
};
