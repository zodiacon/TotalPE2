#pragma once

#include <DialogHelper.h>

class CSymbolSettingsDlg :
	public CDialogImpl<CSymbolSettingsDlg>,
	public CDialogHelper<CSymbolSettingsDlg> {
public:
	enum { IDD = IDD_SYMBOLS };

	BEGIN_MSG_MAP(CSymbolSettingsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_SYM_BROWSE, OnBrowse)
		COMMAND_ID_HANDLER(IDC_SYM_DEFAULTS, OnDefaults)
		COMMAND_ID_HANDLER(IDC_SYM_USE_SERVER, OnChanged)
		COMMAND_ID_HANDLER(IDC_SYM_USE_ENV, OnChanged)
		COMMAND_HANDLER(IDC_SYM_SERVER_URL, EN_CHANGE, OnChanged)
		COMMAND_HANDLER(IDC_SYM_CACHE, EN_CHANGE, OnChanged)
		COMMAND_HANDLER(IDC_SYM_EXTRA, EN_CHANGE, OnChanged)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD, HWND, BOOL&);
	LRESULT OnBrowse(WORD, WORD, HWND, BOOL&);
	LRESULT OnDefaults(WORD, WORD, HWND, BOOL&);
	LRESULT OnChanged(WORD, WORD, HWND, BOOL&);

private:
	void Load(bool useServer, std::wstring const& url, std::wstring const& cache, std::wstring const& extra, bool useEnv);
	void UpdateUI();
	CString GetText(UINT id) const;
};
