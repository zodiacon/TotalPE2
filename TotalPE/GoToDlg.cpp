#include "pch.h"
#include "GoToDlg.h"

LRESULT CGoToDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());

	SetDlgItemText(IDC_GOTO_VALUE, m_Options.Text.c_str());
	CheckRadioButton(IDC_GOTO_RVA, IDC_GOTO_OFFSET, IDC_GOTO_RVA + (int)m_Options.Kind);
	CheckDlgButton(IDC_GOTO_DISASM, m_Options.Disassemble ? BST_CHECKED : BST_UNCHECKED);

	GetDlgItem(IDC_GOTO_VALUE).SetFocus();
	((CEdit)GetDlgItem(IDC_GOTO_VALUE)).SetSelAll();
	return FALSE;
}

LRESULT CGoToDlg::OnOK(WORD, WORD wID, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_GOTO_VALUE, text);

	uint64_t value;
	if (!ParseHexNumber((PCWSTR)text, value)) {
		AtlMessageBox(m_hWnd, L"Enter a hexadecimal number, such as 1A40 or 0x140001A40.", IDR_MAINFRAME, MB_ICONWARNING);
		GetDlgItem(IDC_GOTO_VALUE).SetFocus();
		return 0;
	}

	m_Options.Text = (PCWSTR)text;
	m_Options.Value = value;
	m_Options.Kind = IsDlgButtonChecked(IDC_GOTO_VA) == BST_CHECKED ? GoToKind::Va :
		IsDlgButtonChecked(IDC_GOTO_OFFSET) == BST_CHECKED ? GoToKind::FileOffset : GoToKind::Rva;
	m_Options.Disassemble = IsDlgButtonChecked(IDC_GOTO_DISASM) == BST_CHECKED;
	EndDialog(wID);
	return 0;
}

LRESULT CGoToDlg::OnCancel(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}
