#include "pch.h"
#include "Win32ApiFrame.h"
#include "AppSettings.h"
#include <WTLHelper.h>

CWin32ApiFrame::CWin32ApiFrame(IMainFrame* main, PEFile const& pe, std::function<void()> closed) :
	m_Main(main), m_PE(pe), m_Closed(std::move(closed)) {
}

LRESULT CWin32ApiFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	// (in WM_CREATE: dark mode's hook themes the frame's controls once it has handled it, as the main window's)
	CreateSimpleStatusBar(L"");
	handled = FALSE;
	return 0;
}

bool CWin32ApiFrame::Open(Win32Api& api, std::unordered_set<std::string> imports, CString const& title) {
	if (!Create(nullptr, rcDefault, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN))
		return false;
	m_View = new CWin32ApiView(m_Main, m_PE, api, std::move(imports));
	m_View->SetStatusBar(m_hWndStatusBar);
	m_hWndClient = m_View->DoCreate(m_hWnd);
	if (!m_hWndClient)
		return false;

	// where it was the last time (else, of a size that shows the list and the declarations)
	auto wp = AppSettings::Get().Win32ApiWindowPlacement();
	if (wp.length == sizeof(wp) && wp.showCmd != SW_HIDE) {
		if (wp.showCmd == SW_SHOWMINIMIZED)
			wp.showCmd = SW_SHOWNORMAL;
		SetWindowPlacement(&wp);
	}
	else {
		SetWindowPos(nullptr, 0, 0, 1300, 800, SWP_NOMOVE | SWP_NOZORDER);
		CenterWindow();
		ShowWindow(SW_SHOW);
	}
	UpdateLayout();
	m_View->SetFocus();
	return true;
}

void CWin32ApiFrame::SetFile(Win32Api& api, std::unordered_set<std::string> imports, CString const& title) {
	SetWindowText(title);
	m_View->SetFile(api, std::move(imports));
}

bool CWin32ApiFrame::Select(std::string const& name) {
	return m_View->Navigate(name);
}

void CWin32ApiFrame::ShowDocumentation() {
	m_View->ShowDocumentationPane();
}

void CWin32ApiFrame::Activate() {
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	::SetForegroundWindow(m_hWnd);
}

LRESULT CWin32ApiFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
	WINDOWPLACEMENT wp{ sizeof(wp) };
	GetWindowPlacement(&wp);
	AppSettings::Get().Win32ApiWindowPlacement(wp);
	// (not to the frame's: for a window with no parent, it ends the application)
	return 0;
}

LRESULT CWin32ApiFrame::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_View)
		m_View->SetFocus();
	return 0;
}

// the mouse's Back and Forward buttons
LRESULT CWin32ApiFrame::OnAppCommand(UINT, WPARAM, LPARAM lParam, BOOL& handled) {
	switch (GET_APPCOMMAND_LPARAM(lParam)) {
		case APPCOMMAND_BROWSER_BACKWARD: m_View->GoBack(); return TRUE;
		case APPCOMMAND_BROWSER_FORWARD: m_View->GoForward(); return TRUE;
	}
	handled = FALSE;
	return 0;
}

LRESULT CWin32ApiFrame::OnUpdateDarkMode(UINT, WPARAM, LPARAM, BOOL&) {
	// (another top-level window may have switched the mode already: this one's controls are themed all the same)
	if (!WTLHelper::SwitchToMode(AppSettings::Get().DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd))
		WTLHelper::ApplyMode(m_hWnd);
	SendMessageToDescendants(WM_UPDATE_DARKMODE);
	SendMessageToDescendants(::RegisterWindowMessage(L"WTLHelperUpdateTheme"));
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
	return 0;
}

void CWin32ApiFrame::OnFinalMessage(HWND) {
	if (m_Closed)
		m_Closed();
	delete this;
}
