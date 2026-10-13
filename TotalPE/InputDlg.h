#pragma once

#include <DialogHelper.h>
#include <functional>
#include "resource.h"

//
// A line of text the user enters: a name, a type. 'check' (if given) says what is wrong with the text (empty if nothing
// is): the dialog stays until the text is right, or is cancelled.
//
class CInputDlg :
	public CDialogImpl<CInputDlg>,
	public CDialogHelper<CInputDlg> {
public:
	enum { IDD = IDD_INPUT };
	using Check = std::function<std::wstring(std::wstring const& text)>;

	CInputDlg(std::wstring caption, std::wstring prompt, PCWSTR hint, std::wstring& text, Check check = nullptr)
		: m_Caption(std::move(caption)), m_Prompt(std::move(prompt)), m_Hint(hint ? hint : L""), m_Text(text), m_Check(std::move(check)) {}

	BEGIN_MSG_MAP(CInputDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);

	std::wstring m_Caption, m_Prompt, m_Hint;
	std::wstring& m_Text;
	Check m_Check;
};
