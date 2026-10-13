#include "pch.h"
#include "InputDlg.h"

LRESULT CInputDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());
	SetWindowText(m_Caption.c_str());
	SetDlgItemText(IDC_INPUT_PROMPT, m_Prompt.c_str());
	SetDlgItemText(IDC_INPUT_HINT, m_Hint.c_str());
	SetDlgItemText(IDC_INPUT_TEXT, m_Text.c_str());
	GetDlgItem(IDC_INPUT_TEXT).SetFocus();
	((CEdit)GetDlgItem(IDC_INPUT_TEXT)).SetSelAll();
	return FALSE;
}

LRESULT CInputDlg::OnOK(WORD, WORD wID, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_INPUT_TEXT, text);
	text.Trim();
	if (m_Check) {
		if (auto error = m_Check((PCWSTR)text); !error.empty()) {
			AtlMessageBox(m_hWnd, error.c_str(), m_Caption.c_str(), MB_ICONWARNING);
			GetDlgItem(IDC_INPUT_TEXT).SetFocus();
			return 0;
		}
	}
	m_Text = (PCWSTR)text;
	EndDialog(wID);
	return 0;
}

LRESULT CInputDlg::OnCancel(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}
