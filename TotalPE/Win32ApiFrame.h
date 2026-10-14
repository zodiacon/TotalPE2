#pragma once

#include "Win32ApiView.h"
#include <functional>

//
// The Windows API in a window of its own: it is not of the file (the file only chooses the machine, and the functions
// that Imports shows). One for each main window, which closes it when it closes; Tools > Windows API opens it.
//
class CWin32ApiFrame : public CFrameWindowImpl<CWin32ApiFrame> {
public:
	DECLARE_FRAME_WND_CLASS(L"TotalPEWin32ApiWndClass", IDR_MAINFRAME)

	// closed: when the window has gone (the main window forgets it)
	CWin32ApiFrame(IMainFrame* main, PEFile const& pe, std::function<void()> closed);

	bool Open(Win32Api& api, std::unordered_set<std::string> imports, CString const& title);
	// another file was opened in the main window (or none is)
	void SetFile(Win32Api& api, std::unordered_set<std::string> imports, CString const& title);
	// shows a function or type (Back returns to the one shown before); false if there is none
	bool Select(std::string const& name);
	// to the front (restored, if it was minimized)
	void Activate();

	BEGIN_MSG_MAP(CWin32ApiFrame)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_APPCOMMAND, OnAppCommand)
		MESSAGE_HANDLER(WM_UPDATE_DARKMODE, OnUpdateDarkMode)
		CHAIN_MSG_MAP(CFrameWindowImpl<CWin32ApiFrame>)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override;

private:
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAppCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnUpdateDarkMode(UINT, WPARAM, LPARAM, BOOL&);

	IMainFrame* m_Main;
	PEFile const& m_PE;
	std::function<void()> m_Closed;
	CWin32ApiView* m_View{ nullptr };
};
