// aboutdlg.cpp : implementation of the CAboutDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "AboutDlg.h"

LRESULT CAboutDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CenterWindow(GetParent());
	return TRUE;
}

LRESULT CAboutDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}

LRESULT CAboutDlg::OnClickSyslink(int, LPNMHDR hdr, BOOL&) {
	if (hdr->idFrom == IDC_LINK) {
		::ShellExecute(nullptr, L"open", L"https://github.com/zodiacon/totalpe2", nullptr, nullptr, SW_SHOWDEFAULT);
	}
	else {
		CLinkCtrl link(hdr->hwndFrom);
		LHITTESTINFO ti;
		CPoint pt;
		::GetCursorPos(&pt);
		link.ScreenToClient(&pt);
		ti.pt = pt;
		ti.item.mask = LIF_ITEMINDEX;
		if (link.HitTest(&ti)) {
			std::wstring target;
			switch (ti.item.iLink) {
				case 0:
					target = L"https://github.com/leethomason/tinyxml2";
					break;

				case 1:
					target = L"https://github.com/jnastarot/enma_pe";
					break;

				case 2:
					target = L"http://www.capstone-engine.org/";
					break;
			}
			if (!target.empty())
				::ShellExecute(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
		}
	}
	return 0;
}
