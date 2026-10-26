#pragma once

#include <format>
#include <string>
#include <Win32Api.h>
#include <ClipboardHelper.h>
#include <DpiHelper.h>
#include "Values.h"

//
// A value of an enum by its names (Ctrl+Shift+V): a box to type it in (decimal or hex), and below it its names, as it is
// typed ("FILE_SHARE_READ | FILE_SHARE_WRITE"). Enter copies them; Esc, or going elsewhere, closes it.
//
class CDecodeBox : public CWindowImpl<CDecodeBox> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiUIDecode", CS_DROPSHADOW, -1)

	// shown at the top of the owner's client area, for an enum of an API
	void Show(Win32Api& api, size_t entry) {
		m_Api = &api;
		m_Entry = entry;
		auto const& e = api.Entries()[entry];
		m_Title = std::format(L"A value of {}:", std::wstring(e.Name.begin(), e.Name.end()));
		m_Result.clear();
		auto owner = GetWindow(GW_OWNER);
		CRect client;
		::GetClientRect(owner, &client);
		::MapWindowPoints(owner, nullptr, (POINT*)&client, 2);
		// (of the owner's DPI: of the monitor it is shown on)
		auto dpi = DpiHelper::GetDpi(owner);
		auto width = std::min(DpiHelper::Scale(640, dpi), client.Width() - DpiHelper::Scale(20, dpi));
		SetWindowPos(HWND_TOP, client.left + (client.Width() - width) / 2, client.top + DpiHelper::Scale(40, dpi), width, Height(),
			SWP_SHOWWINDOW);
		// (as tall as its font is there: it may have come from a monitor of another DPI)
		SetWindowPos(nullptr, 0, 0, width, Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		m_Edit.SetWindowText(L"");
		m_Edit.SetFocus();
		Invalidate();
	}
	void SetUIFont(HFONT font) {
		m_Font = font;
		if (m_Edit)
			m_Edit.SetFont(font);
		if (!m_OwnFont.IsNull())
			m_OwnFont.DeleteObject();
		UpdateLayout();
	}
	BOOL PreTranslateMessage(MSG* pMsg) {
		if (!m_hWnd || !IsWindowVisible() || pMsg->message != WM_KEYDOWN || pMsg->hwnd != m_Edit)
			return FALSE;
		if (pMsg->wParam == VK_ESCAPE) {
			ShowWindow(SW_HIDE);
			return TRUE;
		}
		if (pMsg->wParam == VK_RETURN) {
			if (!m_Result.empty() && m_Decoded)
				ClipboardHelper::CopyText(m_hWnd, m_Result.c_str());
			ShowWindow(SW_HIDE);
			return TRUE;
		}
		return FALSE;
	}

	BEGIN_MSG_MAP(CDecodeBox)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		COMMAND_CODE_HANDLER(EN_CHANGE, OnChange)
	END_MSG_MAP()

private:
	static constexpr int Gap = 6;	// (the sizes at 96 DPI)

	int Scale(int value) const {
		return DpiHelper::Scale(value, m_hWnd);
	}
	// the default GUI font at the window's DPI (until the host gives one): it is of 96 DPI, whatever the DPI
	void CreateOwnFont() {
		CFont font(DpiHelper::ScaleFont(AtlGetDefaultGuiFont(), DpiHelper::GetDpi(m_hWnd), USER_DEFAULT_SCREEN_DPI));
		m_Font = font;
		m_Edit.SetFont(font);
		if (!m_OwnFont.IsNull())
			m_OwnFont.DeleteObject();
		m_OwnFont.Attach(font.Detach());
	}

	int LineHeight() const {
		CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_Font);
		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		return tm.tmHeight;
	}
	// the title, the box, two lines of names
	int Height() const {
		auto line = LineHeight();
		auto gap = Scale(Gap);
		return gap + line + gap + line + Scale(10) + gap + 2 * line + gap + Scale(2);
	}
	void UpdateLayout() {
		if (!m_Edit)
			return;
		CRect rc;
		GetClientRect(&rc);
		auto line = LineHeight();
		auto gap = Scale(Gap);
		m_Edit.SetWindowPos(nullptr, gap, gap + line + gap, rc.Width() - 2 * gap, line + Scale(10), SWP_NOZORDER | SWP_NOACTIVATE);
		Invalidate();
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
		m_Edit.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
		CreateOwnFont();
		m_Edit.SetCueBannerText(L"5, 0x80070005, -1", TRUE);
		return 0;
	}
	// (a popup: it is of the DPI of the monitor it is shown on; Show places it)
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
		if (!m_OwnFont.IsNull())
			CreateOwnFont();
		UpdateLayout();
		return 0;
	}
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
		UpdateLayout();
		return 0;
	}
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
		CPaintDC dc(m_hWnd);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, COLOR_WINDOW);
		dc.SetBkMode(TRANSPARENT);
		auto font = dc.SelectFont(m_Font);
		auto line = LineHeight();
		auto gap = Scale(Gap);
		CRect title(gap, gap, rc.right - gap, gap + line);
		dc.SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
		dc.DrawText(m_Title.c_str(), -1, &title, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		CRect result(gap, gap + line + gap + line + Scale(10) + gap, rc.right - gap, rc.bottom - gap);
		dc.SetTextColor(::GetSysColor(m_Decoded ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
		dc.DrawText(m_Result.c_str(), -1, &result, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
		dc.SelectFont(font);
		return 0;
	}
	LRESULT OnActivate(UINT, WPARAM wp, LPARAM, BOOL& handled) {
		if (LOWORD(wp) == WA_INACTIVE)
			ShowWindow(SW_HIDE);
		handled = FALSE;
		return 0;
	}
	LRESULT OnChange(WORD, WORD, HWND, BOOL&) {
		CString text;
		m_Edit.GetWindowText(text);
		m_Decoded = false;
		if (text.IsEmpty())
			m_Result.clear();
		else if (auto value = Values::Parse((PCWSTR)text); value && m_Api) {
			auto names = m_Api->DecodeValue(m_Api->Entries()[m_Entry], *value);
			m_Result = std::wstring(names.begin(), names.end());
			m_Decoded = !m_Result.empty();
		}
		else
			m_Result = L"(not a number)";
		Invalidate();
		return 0;
	}

	Win32Api* m_Api{ nullptr };
	size_t m_Entry{ 0 };
	std::wstring m_Title, m_Result;
	bool m_Decoded{ false };
	CEdit m_Edit;
	HFONT m_Font{ nullptr };
	CFont m_OwnFont;	// until the host gives one
};
