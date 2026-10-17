#pragma once

#include "resource.h"
#include <DialogHelper.h>
#include <Win32Api.h>

//
// Help > About: the name and version (of the version resource), the copyright, the metadata used (where it is, its
// version, what it has), and links to the sources and to the metadata's
//
class CAboutDlg :
	public CDialogImpl<CAboutDlg>,
	public CDialogHelper<CAboutDlg> {
public:
	enum { IDD = IDD_ABOUTBOX };

	// api: the metadata loaded (null: not yet)
	explicit CAboutDlg(Win32Api* api) : m_Api(api) {
	}

	BEGIN_MSG_MAP(CAboutDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_CODE_HANDLER(NM_CLICK, OnClickLink)
		NOTIFY_CODE_HANDLER(NM_RETURN, OnClickLink)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD id, HWND, BOOL&) {
		EndDialog(id);
		return 0;
	}
	LRESULT OnClickLink(int, LPNMHDR hdr, BOOL&);

	Win32Api* m_Api;
};
