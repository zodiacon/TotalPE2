#pragma once

#include <DialogHelper.h>
#include "resource.h"

// Asks for the API key of VirusTotal.
class CVtKeyDlg :
	public CDialogImpl<CVtKeyDlg>,
	public CDialogHelper<CVtKeyDlg> {
public:
	enum { IDD = IDD_VTKEY };

	explicit CVtKeyDlg(std::wstring const& key = L"") : m_Key(key) {}
	std::wstring const& GetKey() const { return m_Key; }

	BEGIN_MSG_MAP(CVtKeyDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_VT_GETKEY, OnGetKey)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnGetKey(WORD, WORD, HWND, BOOL&);

private:
	std::wstring m_Key;
};
