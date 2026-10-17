#pragma once

#include <format>
#include <string>
#include <Win32Api.h>
#include <ClipboardHelper.h>
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
		auto width = std::min(640, client.Width() - 20);
		SetWindowPos(HWND_TOP, client.left + (client.Width() - width) / 2, client.top + 40, width, Height(), SWP_SHOWWINDOW);
		m_Edit.SetWindowText(L"");
		m_Edit.SetFocus();
		Invalidate();
	}
	void SetUIFont(HFONT font) {
		m_Font = font;
		if (m_Edit)
			m_Edit.SetFont(font);
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
		COMMAND_CODE_HANDLER(EN_CHANGE, OnChange)
	END_MSG_MAP()

private:
	static constexpr int Gap = 6;

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
		return Gap + line + Gap + line + 10 + Gap + 2 * line + Gap + 2;
	}
	void UpdateLayout() {
		if (!m_Edit)
			return;
		CRect rc;
		GetClientRect(&rc);
		auto line = LineHeight();
		m_Edit.SetWindowPos(nullptr, Gap, Gap + line + Gap, rc.Width() - 2 * Gap, line + 10, SWP_NOZORDER | SWP_NOACTIVATE);
		Invalidate();
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
		m_Font = AtlGetDefaultGuiFont();
		m_Edit.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
		m_Edit.SetFont(m_Font);
		m_Edit.SetCueBannerText(L"5, 0x80070005, -1", TRUE);
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
		CRect title(Gap, Gap, rc.right - Gap, Gap + line);
		dc.SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
		dc.DrawText(m_Title.c_str(), -1, &title, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		CRect result(Gap, Gap + line + Gap + line + 10 + Gap, rc.right - Gap, rc.bottom - Gap);
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
};
