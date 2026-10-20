#pragma once

#include "resource.h"
#include "MachineBar.h"
#include "LoadingView.h"
#include <ApiBrowser.h>
#include <thread>

//
// The main window: the Windows API to browse (CApiBrowser, the client, once the metadata is loaded: until then, a line
// that says it is), with its commands in the menus and the toolbar. The machine (x86, x64, ARM64) chooses the functions
// there are, and the layout of the structs; the metadata can be another (File).
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
		UPDATE_ELEMENT(ID_EDIT_COPYCSHARP, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_EDIT_COPYRUST, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_EDIT_COPYPYTHON, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_SHOW_DECLARATION, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_LAYOUT, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_COMPARE, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_SHOW_SINGLELINE, UPDUI_MENUPOPUP)

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
		// (while the metadata is loaded: Exit and About only; then the browser's commands are its own)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
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
		COMMAND_ID_HANDLER(ID_VIEW_DARKMODE, OnViewDarkMode)
		COMMAND_ID_HANDLER(ID_VIEW_FONT, OnViewFont)
		COMMAND_ID_HANDLER(ID_VIEW_UIFONT, OnViewUIFont)
		COMMAND_ID_HANDLER(ID_VIEW_RESETFONTS, OnResetFonts)
		COMMAND_RANGE_HANDLER(ID_MACHINE_X86, ID_MACHINE_ARM64, OnMachine)
		COMMAND_HANDLER(IDC_MACHINE, CBN_SELCHANGE, OnMachineCombo)
		CHAIN_MSG_MAP(CUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

private:
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
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	// the browser, of the metadata loaded
	void FinishCreate();
	// opens a function, type or constant by its name, in a tab that stays (false if there is none)
	bool OpenByName(std::wstring const& name);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAppCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnExit(WORD, WORD, HWND, BOOL&);
	LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewDarkMode(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewFont(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewUIFont(WORD, WORD, HWND, BOOL&);
	LRESULT OnResetFonts(WORD, WORD, HWND, BOOL&);
	LRESULT OnMachine(WORD, WORD, HWND, BOOL&);
	LRESULT OnMachineCombo(WORD, WORD, HWND, BOOL&);

	void CreateToolBar();
	void SetMachine(Win32Api::Machine machine);
	// the browser's status, and the machine
	void UpdateStatus();
	void UpdateTitle();
	void UpdateUIState();
	void ApplyColors();
	// the interface font (of the settings, else the system's) to the browser, the machine and the status bar
	void ApplyUIFont();
	static LOGFONT UIFont();
	void SaveSettings();

	Win32Api* m_Api{ nullptr };
	Win32Api::Machine m_Machine{ Win32Api::Machine::X64 };
	std::wstring m_StartName;		// to open when the metadata is loaded
	std::thread m_Loader;
	CLoadingView m_Loading;
	bool m_MetadataBusy{ false };	// (looked for, downloaded)
	CImageList m_Images;
	std::unique_ptr<CApiBrowser> m_Browser;
	CMachineBar m_MachineBar;
	CToolBarCtrl m_ToolBar;
	CFont m_UIFont;
	std::wstring m_StatusText;		// the browser's
};
