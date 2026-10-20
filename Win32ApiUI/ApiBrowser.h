#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ApiBrowserIds.h"
#include "StripHost.h"
#include "DeclarationView.h"
#include "SearchPane.h"
#include "NamespacePane.h"
#include "QuickOpen.h"
#include "DecodeBox.h"
#include <CustomSplitterWindow.h>
#include <DocPane.h>

//
// The Windows API, to browse: on the left, the search (a list), the namespaces (a tree), what uses a type, the favorites
// and the history (lists), and a list of the host's (Imports), in tabs; on the right, the declarations of what was opened,
// in tabs (and to their side, if one is opened there: a declaration to compare with), and below them Microsoft's
// documentation of the one shown. What the left side selects is shown in a preview tab (its title in italics), which the
// next one replaces; Enter or a double-click (or following a type, or Keep Open) makes a tab stay.
//
// The client of a frame (Win32 API Explorer's, TotalPE's Windows API window), which passes it the commands of its range
// (IsCommand), and its messages to translate (PreTranslateMessage: its keys are its own); the frame's update of its UI
// asks it for their states (UpdateUI). Its menus' commands go to the frame (its top level window), so that a command of
// the host's that a menu has (OnExtendMenu) is the frame's; TargetEntry is what it is for.
//
class CApiBrowser : public CWindowImpl<CApiBrowser> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiUIBrowser", 0, -1)

	// what is kept between sessions (by the host, in its settings)
	struct State {
		bool ShowDocumentation{ true };
		int DocumentationPercent{ 45 };	// of the height of the right side
		int NavigatorWidth{ 0 };		// in pixels (0: the default)
		int NavigatorPage{ 0 };
		int ShowMode{ 0 };				// CDeclarationView::Mode
		bool SingleLine{ false };		// functions declared on a line each (CDeclarationView::SetSingleLine)
		std::vector<std::wstring> OpenTabs;		// the names of the tabs that stay, in their order
		int ActiveTab{ -1 };
		std::vector<std::wstring> Favorites;	// in the order they were added
		std::vector<std::wstring> History;		// the last first
	};

	// images: of Images::Index (Images::Create), which it does not destroy
	CApiBrowser(Win32Api& api, HIMAGELIST images, State state = {});

	// the text of the status (the counts of the list shown; what was copied)
	std::function<void(std::wstring const& text)> OnStatus;
	// the entry the active tab shows changed (Title)
	std::function<void()> OnTitleChanged;
	// a menu of a tab or of a declaration, which the host adds its commands to (TargetEntry: what they are for)
	std::function<void(CMenu& menu)> OnExtendMenu;

	static bool IsCommand(UINT id);
	// its keys (in the windows of hScope, the frame), and those of its panes
	BOOL PreTranslateMessage(MSG* pMsg, HWND hScope);
	// the states of its commands, to the host's menus and toolbars
	template<typename T>
	void UpdateUI(CUpdateUI<T>& ui) const {
		for (auto const& command : CommandStates()) {
			ui.UIEnable(command.Id, command.Enabled);
			if (command.Check >= 0)
				ui.UISetCheck(command.Id, command.Check);
		}
		for (auto const& radio : RadioStates())
			ui.UISetRadioMenuItem(radio.Id, radio.First, radio.Last);
	}
	State GetState() const;

	Win32Api& Api() const;
	// the API of another machine: what is shown, by name, of it
	void SetApi(Win32Api& api);
	// opens a function, type or constant by its name, in a tab that stays (false if there is none)
	bool OpenByName(std::string const& name);
	// what a command of the host is for: the entry of the tab whose menu is shown, else the one shown where the keyboard
	// is (-1: none)
	int TargetEntry() const;
	// the name of what the active tab shows (empty: none)
	std::string Title() const;
	void ShowDocumentation(bool show);
	void GoBack();
	void GoForward();
	// a list of the host's, in a page of its own (ID_VIEW_HOSTLIST): the names of its entries (an empty title: none)
	void SetHostList(std::wstring const& title, std::vector<std::string> const& names, std::wstring const& description);
	// the colors of the mode (dark or light), which the controls of the frame have already
	void UpdateColors(bool dark);
	// the code font changed (CDeclarationView::SetCodeFont)
	void UpdateCodeFont();
	// the font of the lists, the tree and the tabs (custom: null for the system's)
	void SetUIFont(HFONT font, LOGFONT const* custom);

	BEGIN_MSG_MAP(CApiBrowser)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_SELECT_ALL, OnSelectAll)
		COMMAND_ID_HANDLER(ID_EDIT_COPYNAME, OnCopyName)
		COMMAND_ID_HANDLER(ID_EDIT_DECODE, OnDecode)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_RANGE_HANDLER(ID_VIEW_SEARCH, ID_VIEW_HOSTLIST, OnViewNavigator)
		COMMAND_ID_HANDLER(ID_VIEW_CLEARHISTORY, OnClearHistory)
		COMMAND_ID_HANDLER(ID_VIEW_DOCUMENTATION, OnViewDocumentation)
		COMMAND_ID_HANDLER(ID_EDIT_FAVORITE, OnFavorite)
		COMMAND_RANGE_HANDLER(ID_EDIT_COPYCSHARP, ID_EDIT_COPYPYTHON, OnCopyAs)
		COMMAND_RANGE_HANDLER(ID_SHOW_DECLARATION, ID_SHOW_COMPARE, OnShowMode)
		COMMAND_ID_HANDLER(ID_SHOW_SINGLELINE, OnSingleLine)
		COMMAND_ID_HANDLER(ID_GO_BACK, OnBack)
		COMMAND_ID_HANDLER(ID_GO_FORWARD, OnForward)
		COMMAND_ID_HANDLER(ID_GO_DEFINITION, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_GO_DEFINITION_NEWTAB, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_GO_REVEAL, OnReveal)
		COMMAND_ID_HANDLER(ID_GO_USEDBY, OnFindUsages)
		COMMAND_ID_HANDLER(ID_GO_QUICKOPEN, OnQuickOpen)
		COMMAND_ID_HANDLER(ID_GO_SIDE, OnOpenToSide)
		COMMAND_ID_HANDLER(ID_DOC_BROWSER, OnOpenInBrowser)
		COMMAND_ID_HANDLER(ID_TAB_CLOSE, OnCloseTab)
		COMMAND_ID_HANDLER(ID_TAB_CLOSEOTHERS, OnCloseOtherTabs)
		COMMAND_ID_HANDLER(ID_TAB_CLOSEALL, OnCloseAllTabs)
		COMMAND_ID_HANDLER(ID_TAB_NEXT, OnNextTab)
		COMMAND_ID_HANDLER(ID_TAB_PREV, OnNextTab)
		COMMAND_ID_HANDLER(ID_TAB_KEEPOPEN, OnKeepOpen)
	END_MSG_MAP()

private:
	struct Tab {
		std::unique_ptr<CDeclarationView> View;
		bool Preview{ false };
	};
	struct CommandState {
		UINT Id;
		bool Enabled;
		int Check{ -1 };	// (-1: not a check)
	};
	struct RadioState {
		UINT Id, First, Last;
	};
	std::vector<CommandState> CommandStates() const;
	std::vector<RadioState> RadioStates() const;

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
	LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopyName(WORD, WORD, HWND, BOOL&);
	LRESULT OnDecode(WORD, WORD, HWND, BOOL&);
	LRESULT OnFind(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewNavigator(WORD, WORD, HWND, BOOL&);
	LRESULT OnClearHistory(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewDocumentation(WORD, WORD, HWND, BOOL&);
	LRESULT OnFavorite(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopyAs(WORD, WORD, HWND, BOOL&);
	LRESULT OnShowMode(WORD, WORD, HWND, BOOL&);
	LRESULT OnSingleLine(WORD, WORD, HWND, BOOL&);

	LRESULT OnBack(WORD, WORD, HWND, BOOL&);
	LRESULT OnForward(WORD, WORD, HWND, BOOL&);
	LRESULT OnGoToDefinition(WORD, WORD, HWND, BOOL&);
	LRESULT OnReveal(WORD, WORD, HWND, BOOL&);
	LRESULT OnFindUsages(WORD, WORD, HWND, BOOL&);
	LRESULT OnQuickOpen(WORD, WORD, HWND, BOOL&);
	LRESULT OnOpenToSide(WORD, WORD, HWND, BOOL&);
	LRESULT OnOpenInBrowser(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseTab(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseOtherTabs(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseAllTabs(WORD, WORD, HWND, BOOL&);
	LRESULT OnNextTab(WORD, WORD, HWND, BOOL&);
	LRESULT OnKeepOpen(WORD, WORD, HWND, BOOL&);

	void CreateNavigator();
	void CreateEditors();
	// the keys of the commands
	static HACCEL Accelerators();
	// a menu's command, to the frame (which passes those of the browser back)
	void SendCommand(UINT id);

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
	void RestoreTabs();
	// a view of a tab or of the side, made
	std::unique_ptr<CDeclarationView> CreateView(HWND hParent);

	// the pages of the navigator (Imports: the host's list, if it gave one)
	enum Page { SearchPage, NamespacesPage, UsedByPage, FavoritesPage, HistoryPage, HostPage };
	int PageCount() const;
	void ShowNavigatorPage(int page);
	// the favorites, the history and the host's list, by name: their entries (of the machine), in their panes
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
	void UpdateDocumentation();
	void UpdateStatus();
	void Status(std::wstring const& text);
	void TitleChanged();

	Win32Api* m_Api;
	HIMAGELIST m_Images;
	State m_State;
	HACCEL m_Accelerators{ nullptr };
	CCustomSplitterWindow m_Splitter;
	CCustomHorSplitterWindow m_Right;
	CStripHost m_Navigator, m_Editors, m_SideHost;
	CCustomSplitterWindow m_EditorSplit;		// the tabs, and the side
	std::unique_ptr<CDeclarationView> m_Side;
	std::unique_ptr<CSearchPane> m_Search;
	std::unique_ptr<CNamespacePane> m_Namespaces;
	std::unique_ptr<CSearchPane> m_UsedBy;
	std::unique_ptr<CSearchPane> m_Favorites, m_History, m_HostList;
	std::vector<std::string> m_FavoriteNames, m_HistoryNames, m_HostNames;	// (the history: the last first)
	std::vector<size_t> m_FavoriteEntries, m_HistoryEntries, m_HostEntries;
	std::wstring m_HostTitle, m_HostDescription;
	bool m_SwitchingMachine{ false };	// (the tabs show the same as they did: not opened)
	std::string m_UsedBySubject;		// the type whose usages are shown (empty: none)
	std::vector<size_t> const m_NoEntries;
	CDocPane m_Doc;
	std::vector<Tab> m_Tabs;		// as the strip has them
	int m_MenuTab{ -1 };			// the tab whose menu is shown
	CQuickOpen m_QuickOpen;
	CDecodeBox m_Decode;
};
