#pragma once

#include <WebView2.h>
#include <wrl/client.h>
#include "Interfaces.h"

//
// Microsoft's documentation of a function or type of Windows (a page of learn.microsoft.com), in the browser that Windows
// has (WebView2). It is started when the first page is asked for; a page is loaded after a pause (the arrow keys in a list
// pass many). Links stay in it, except those that open a window of their own: those open in the default browser. Its colors
// follow the dark mode.
//
class CDocPane : public CWindowImpl<CDocPane> {
public:
	DECLARE_WND_CLASS_EX(L"TotalPEDocPane", 0, -1)

	// the page to show: 'url' (empty: none, 'name' has no documentation)
	void Navigate(std::wstring url, std::wstring const& name);
	// the page asked for last (empty if there is none)
	std::wstring const& Url() const { return m_Url; }

	BEGIN_MSG_MAP(CDocPane)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_UPDATE_DARKMODE, OnUpdateDarkMode)
	END_MSG_MAP()

private:
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnShowWindow(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnUpdateDarkMode(UINT, WPARAM, LPARAM, BOOL&);

	void Start();
	void Started(ICoreWebView2Controller* controller);
	void Load();
	void ApplyColors();

	Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_Controller;
	Microsoft::WRL::ComPtr<ICoreWebView2> m_WebView;
	std::wstring m_Url, m_Name;
	std::wstring m_Loaded{ L"\n" };	// (the page loaded: none yet)
	std::wstring m_Message;		// shown until there is a browser (it is starting, it cannot be)
	bool m_Starting{ false };
};
