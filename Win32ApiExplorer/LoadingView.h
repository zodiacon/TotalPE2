#pragma once

//
// What the main window shows while the metadata is loaded: a line that says so, in the middle, in the theme's colors
//
class CLoadingView : public CWindowImpl<CLoadingView> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerLoading", 0, -1)

	BEGIN_MSG_MAP(CLoadingView)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
	END_MSG_MAP()

private:
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
		CPaintDC dc(m_hWnd);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, COLOR_WINDOW);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(::GetSysColor(COLOR_GRAYTEXT));
		auto font = dc.SelectFont(AtlGetDefaultGuiFont());
		dc.DrawText(L"Loading the metadata of Windows...", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(font);
		return 0;
	}
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
		Invalidate();
		return 0;
	}
};
