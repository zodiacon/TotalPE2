#pragma once

#include <DialogHelper.h>
#include "resource.h"
#include "HexSearch.h"

class CHexFindDlg :
	public CDialogImpl<CHexFindDlg>,
	public CDialogHelper<CHexFindDlg> {
public:
	enum { IDD = IDD_HEXFIND };

	explicit CHexFindDlg(HexFindOptions& options) : m_Options(options) {}

	BEGIN_MSG_MAP(CHexFindDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_RANGE_HANDLER(IDC_HEXFIND_HEX, IDC_HEXFIND_UNICODE, OnTypeChanged)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnTypeChanged(WORD, WORD, HWND, BOOL&);

private:
	HexFindOptions& m_Options;
};
