#pragma once

#include <DialogHelper.h>
#include "CodeColors.h"

//
// Options > Colors: the colors of the elements of code, of the light theme and of the dark one. Each can be given a color
// (and be bold), or its default back; OK keeps them (only those that differ from their defaults).
//
class CCodeColorsDlg :
	public CDialogImpl<CCodeColorsDlg>,
	public CDialogHelper<CCodeColorsDlg> {
public:
	enum { IDD = IDD_CODECOLORS };

	BEGIN_MSG_MAP(CCodeColorsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_COLORS_CHANGE, OnChange)
		COMMAND_ID_HANDLER(IDC_COLORS_BOLD, OnBold)
		COMMAND_ID_HANDLER(IDC_COLORS_DEFAULT, OnDefault)
		COMMAND_ID_HANDLER(IDC_COLORS_RESETALL, OnResetAll)
		COMMAND_HANDLER(IDC_COLORS_SCHEME, CBN_SELCHANGE, OnScheme)
		NOTIFY_HANDLER(IDC_COLORS_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_COLORS_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_COLORS_LIST, NM_DBLCLK, OnDoubleClick)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD id, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD id, HWND, BOOL&);
	LRESULT OnChange(WORD, WORD, HWND, BOOL&);
	LRESULT OnBold(WORD, WORD, HWND, BOOL&);
	LRESULT OnDefault(WORD, WORD, HWND, BOOL&);
	LRESULT OnResetAll(WORD, WORD, HWND, BOOL&);
	LRESULT OnScheme(WORD, WORD, HWND, BOOL&);
	LRESULT OnCustomDraw(int, LPNMHDR, BOOL&);
	LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
	LRESULT OnDoubleClick(int, LPNMHDR, BOOL&);

	// the theme shown (0: light, 1: dark), and the element selected (-1 if none)
	bool Dark() const;
	int Selected() const;
	CodeColor& ColorOf(int element);
	void UpdateButtons();

	CListViewCtrl m_List;
	CComboBox m_Scheme;
	// the colors of the elements, of each theme, as they are being changed
	std::vector<CodeColor> m_Colors[2];
	COLORREF m_Custom[16]{};
};
