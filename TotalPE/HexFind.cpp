#include "pch.h"
#include "resource.h"
#include "HexFind.h"

LRESULT CHexFindDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());

	SetDlgItemText(IDC_HEXFIND_TEXT, m_Options.Text.c_str());
	CheckRadioButton(IDC_HEXFIND_HEX, IDC_HEXFIND_UNICODE, IDC_HEXFIND_HEX + (int)m_Options.Type);
	CheckDlgButton(IDC_HEXFIND_MATCHCASE, m_Options.MatchCase ? BST_CHECKED : BST_UNCHECKED);
	CheckRadioButton(IDC_HEXFIND_DOWN, IDC_HEXFIND_UP, m_Options.Down ? IDC_HEXFIND_DOWN : IDC_HEXFIND_UP);
	BOOL handled;
	OnTypeChanged(0, 0, nullptr, handled);

	GetDlgItem(IDC_HEXFIND_TEXT).SetFocus();
	((CEdit)GetDlgItem(IDC_HEXFIND_TEXT)).SetSelAll();
	return FALSE;
}

LRESULT CHexFindDlg::OnTypeChanged(WORD, WORD, HWND, BOOL&) {
	// case matters only for text searches
	GetDlgItem(IDC_HEXFIND_MATCHCASE).EnableWindow(IsDlgButtonChecked(IDC_HEXFIND_HEX) != BST_CHECKED);
	return 0;
}

LRESULT CHexFindDlg::OnOK(WORD, WORD wID, HWND, BOOL&) {
	HexFindOptions options;
	CString text;
	GetDlgItemText(IDC_HEXFIND_TEXT, text);
	options.Text = text;
	options.Type = IsDlgButtonChecked(IDC_HEXFIND_ASCII) == BST_CHECKED ? HexSearchType::Ascii :
		IsDlgButtonChecked(IDC_HEXFIND_UNICODE) == BST_CHECKED ? HexSearchType::Unicode : HexSearchType::Hex;
	options.MatchCase = IsDlgButtonChecked(IDC_HEXFIND_MATCHCASE) == BST_CHECKED;
	options.Down = IsDlgButtonChecked(IDC_HEXFIND_DOWN) == BST_CHECKED;

	HexPattern pattern;
	std::wstring error;
	if (!HexSearch::BuildPattern(options, pattern, error)) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		GetDlgItem(IDC_HEXFIND_TEXT).SetFocus();
		return 0;
	}

	m_Options = options;
	EndDialog(wID);
	return 0;
}

LRESULT CHexFindDlg::OnCancel(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}
