#pragma once

#include "TabStrip.h"

//
// A strip of tabs, and below it the window of the page shown (its owner says which: the strip only tells it what the user
// does). Without a page, it is blank.
//
class CStripHost : public CWindowImpl<CStripHost> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerStripHost", 0, -1)

	CTabStrip& Strip() {
		return m_Strip;
	}
	CTabStrip const& Strip() const {
		return m_Strip;
	}
	// the window shown below the strip (the others it had are hidden); null for none
	void SetPage(HWND hPage) {
		if (m_hPage == hPage)
			return;
		if (m_hPage && ::IsWindow(m_hPage))
			::ShowWindow(m_hPage, SW_HIDE);
		m_hPage = hPage;
		UpdateLayout();
		if (m_hPage)
			::ShowWindow(m_hPage, SW_SHOW);
		Invalidate();
	}
	HWND GetPage() const {
		return m_hPage;
	}

	BEGIN_MSG_MAP(CStripHost)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
	END_MSG_MAP()

private:
	void UpdateLayout() {
		if (!m_hWnd)
			return;
		CRect rc;
		GetClientRect(&rc);
		auto height = m_Strip.GetIdealHeight();
		m_Strip.SetWindowPos(nullptr, 0, 0, rc.Width(), height, SWP_NOZORDER | SWP_NOACTIVATE);
		if (m_hPage)
			::SetWindowPos(m_hPage, nullptr, 0, height, rc.Width(), std::max(0, rc.Height() - height), SWP_NOZORDER | SWP_NOACTIVATE);
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
		m_Strip.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS);
		return 0;
	}
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
		UpdateLayout();
		return 0;
	}
	LRESULT OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL&) {
		if (m_hPage)
			return 1;
		CDCHandle dc((HDC)wp);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, COLOR_WINDOW);
		return 1;
	}
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
		if (m_hPage)
			::SetFocus(m_hPage);
		return 0;
	}

	CTabStrip m_Strip;
	HWND m_hPage{ nullptr };
};
