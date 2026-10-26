#pragma once

#include "resource.h"
#include "Interfaces.h"
#include <ApiBrowser.h>
#include <functional>
#include <unordered_set>

//
// The Windows API in a window of its own: the browser of Win32 API Explorer (CApiBrowser: the search, the namespaces, tabs
// of declarations, the documentation), with a page of the functions the file imports (Imports), and where the file calls
// the one shown (References, in the main window). It is not of the file: the file only chooses the machine, and what
// Imports has. One for each main window, which closes it when it closes; Tools > Windows API opens it.
//
class CWin32ApiFrame :
	public CFrameWindowImpl<CWin32ApiFrame>,
	public CUpdateUI<CWin32ApiFrame>,
	public CMessageFilter,
	public CIdleHandler {
public:
	DECLARE_FRAME_WND_CLASS(L"TotalPEWin32ApiWndClass", IDR_MAINFRAME)

	// closed: when the window has gone (the main window forgets it)
	CWin32ApiFrame(IMainFrame* main, std::function<void()> closed);

	bool Open(Win32Api& api, std::unordered_set<std::string> imports, CString const& title);
	// another file was opened in the main window (or none is)
	void SetFile(Win32Api& api, std::unordered_set<std::string> imports, CString const& title);
	// opens a function or type, in a tab; false if there is none
	bool Select(std::string const& name);
	// Microsoft's documentation shown (if it was not)
	void ShowDocumentation();
	// to the front (restored, if it was minimized)
	void Activate();
	// Win32 API Explorer (Win32ApiExp.exe, next to TotalPE.exe), with a function or type of it opened (empty: none); the
	// window it has, if it is running (it is one). False if it is not there.
	static bool OpenInExplorer(std::string const& name);

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;

	BEGIN_UPDATE_UI_MAP(CWin32ApiFrame)
		UPDATE_ELEMENT(ID_GO_BACK, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_GO_FORWARD, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_GO_REVEAL, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_GO_USEDBY, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_DECLARATION, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_LAYOUT, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_COMPARE, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_SINGLELINE, UPDUI_TOOLBAR)

		UPDATE_ELEMENT(ID_VIEW_DOCUMENTATION, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_DOC_BROWSER, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_WIN32_REFERENCES, UPDUI_TOOLBAR)
	END_UPDATE_UI_MAP()

	BEGIN_MSG_MAP(CWin32ApiFrame)
		// (the browser's commands are its own)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_APPCOMMAND, OnAppCommand)
		MESSAGE_HANDLER(WM_UPDATE_DARKMODE, OnUpdateDarkMode)
		COMMAND_ID_HANDLER(ID_WIN32_REFERENCES, OnReferences)
		COMMAND_ID_HANDLER(ID_WIN32_EXPLORER, OnOpenInExplorer)
		CHAIN_MSG_MAP(CUpdateUI<CWin32ApiFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CWin32ApiFrame>)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override;

private:
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAppCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnUpdateDarkMode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnReferences(WORD, WORD, HWND, BOOL&);
	LRESULT OnOpenInExplorer(WORD, WORD, HWND, BOOL&);

	void CreateToolBar();
	// the imports, in the browser's page of them
	void SetImports(std::unordered_set<std::string> imports);
	// a function of the file's imports is what a command is for: where it is called can be shown
	bool CanShowReferences() const;
	// the fonts of TotalPE (Options): the code font, of the declarations; the application font, of the rest
	void ApplyFonts();
	void UpdateTitle();
	void SaveState();

	IMainFrame* m_Main;
	std::function<void()> m_Closed;
	std::unordered_set<std::string> m_Imports;
	CString m_Title;		// the main window's: of the file (the name shown is added)
	CImageList m_Images;
	std::unique_ptr<CApiBrowser> m_Browser;
	CToolBarCtrl m_ToolBar;
	CFont m_UIFont;
};
