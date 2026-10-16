#pragma once

#include <WTLHelper.h>

//
// "Machine:" and the list of machines, for a band of the rebar: the band is as wide as what is left of its row, this keeps
// them at its left (the rest shows the rebar's background). The list's notifications go to the main window.
//
class CMachineBar : public CWindowImpl<CMachineBar> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerMachineBar", 0, -1)

	CComboBox& Combo() {
		return m_Combo;
	}
	int GetIdealHeight() const {
		CRect rc;
		m_Combo.GetWindowRect(&rc);
		return rc.Height();
	}

	BEGIN_MSG_MAP(CMachineBar)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
	END_MSG_MAP()

private:
	static constexpr int ComboWidth = 90, Gap = 6;

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
		SetFont(AtlGetDefaultGuiFont());
		CClientDC dc(m_hWnd);
		auto font = dc.SelectFont(AtlGetDefaultGuiFont());
		CSize size;
		dc.GetTextExtent(Label, -1, &size);
		dc.SelectFont(font);
		m_LabelWidth = size.cx;
		CRect rc(m_LabelWidth + Gap, 0, m_LabelWidth + Gap + ComboWidth, 200);
		m_Combo.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 0, IDC_MACHINE);
		m_Combo.SetFont(AtlGetDefaultGuiFont());
		return 0;
	}
	LRESULT OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL&) {
		::DrawThemeParentBackground(m_hWnd, (HDC)wp, nullptr);
		return 1;
	}
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
		CPaintDC dc(m_hWnd);
		CRect rc;
		GetClientRect(&rc);
		rc.right = m_LabelWidth;
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(WTLHelper::IsDarkMode() ? RGB(220, 220, 220) : ::GetSysColor(COLOR_BTNTEXT));
		auto font = dc.SelectFont(AtlGetDefaultGuiFont());
		dc.DrawText(Label, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
		dc.SelectFont(font);
		return 0;
	}
	LRESULT OnCommand(UINT msg, WPARAM wp, LPARAM lp, BOOL&) {
		return GetTopLevelParent().SendMessage(msg, wp, lp);
	}

	static constexpr PCWSTR Label = L"Machine:";
	CComboBox m_Combo;
	int m_LabelWidth{ 0 };
};
