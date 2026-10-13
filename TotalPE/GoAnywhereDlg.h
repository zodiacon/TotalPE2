#pragma once

#include <DialogHelper.h>
#include <VirtualListView.h>
#include "resource.h"
#include "SearchDlg.h"

// Go Anywhere (Ctrl+P): one box for an address (a VA, an RVA or a file offset), a view of the tree, an export, an import,
// a symbol, a section, a resource, a .NET type or member, or a string. The best matches are listed as the text is typed;
// Up and Down choose one, Enter goes there, Esc closes the box.
class CGoAnywhereDlg :
	public CDialogImpl<CGoAnywhereDlg>,
	public CVirtualListView<CGoAnywhereDlg> {
public:
	enum { IDD = IDD_GOANYWHERE };

	explicit CGoAnywhereDlg(ISearchHost* host) : m_Host(host), m_Edit(this, 1) {}

	// what was chosen (after IDOK)
	SearchItem const& Selected() const { return m_Selected; }

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const*) {}
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_MSG_MAP(CGoAnywhereDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_HANDLER(IDC_GOANY_TEXT, EN_CHANGE, OnTextChanged)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		CHAIN_MSG_MAP(CVirtualListView<CGoAnywhereDlg>)
	ALT_MSG_MAP(1)
		// the keys that move in the list go to it from the text
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditKeyDown)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD, HWND, BOOL&);
	LRESULT OnEditKeyDown(UINT, WPARAM, LPARAM, BOOL&);

	void Update();

	ISearchHost* m_Host;
	CContainedWindowT<CEdit> m_Edit;
	CListViewCtrl m_List;
	CStatic m_Status;
	std::vector<SearchItem> m_Results;
	SearchItem m_Selected;
	static constexpr size_t MaxResults = 200;
};
