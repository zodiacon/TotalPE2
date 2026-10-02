#include "pch.h"
#include "VtKeyDlg.h"
#include "VirusTotal.h"

LRESULT CVtKeyDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());
	SetDlgItemText(IDC_VT_KEY, m_Key.c_str());
	GetDlgItem(IDC_VT_KEY).SetFocus();
	return FALSE;
}

LRESULT CVtKeyDlg::OnOK(WORD, WORD wID, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_VT_KEY, text);
	text.Trim();
	if (!vt::LooksLikeKey((PCWSTR)text)) {
		AtlMessageBox(m_hWnd, L"An API key of VirusTotal is 64 hexadecimal digits. Copy it from your profile on virustotal.com.", IDR_MAINFRAME, MB_ICONWARNING);
		GetDlgItem(IDC_VT_KEY).SetFocus();
		return 0;
	}
	m_Key = (PCWSTR)text;
	EndDialog(wID);
	return 0;
}

LRESULT CVtKeyDlg::OnCancel(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}

LRESULT CVtKeyDlg::OnGetKey(WORD, WORD, HWND, BOOL&) {
	::ShellExecute(m_hWnd, L"open", L"https://www.virustotal.com/gui/my-apikey", nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}
