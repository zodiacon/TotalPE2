#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <QuickFindEdit.h>
#include "PEDiff.h"
#include "SearchDlg.h"
#include "resource.h"

// The comparison of the open PE file with another (see PEDiff.h): headers, data directories, sections, imports, exports,
// resources, version and debug information. The differences are colored; the items that are the same are hidden unless
// Show Unchanged is checked. Double-click an item to see it in the open file.
class CCompareView :
	public CViewBase<CCompareView>,
	public CVirtualListView<CCompareView> {
public:
	CCompareView(IMainFrame* frame, ISearchHost* host, PEDiff diff, std::wstring otherPath);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CCompareView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_COMPARE_SHOWSAME, OnShowSame)
		CHAIN_MSG_MAP(CVirtualListView<CCompareView>)
		CHAIN_MSG_MAP(CViewBase<CCompareView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CCompareView>, 1)
	END_MSG_MAP()

private:
	enum Column { Category, Name, Status, Left, Right, Details };

	void ApplyFilter();
	bool GoTo(DiffLocation const& where) const;

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCustomDraw(int, LPNMHDR hdr, BOOL& handled);
	LRESULT OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled);
	LRESULT OnShowSame(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	ISearchHost* m_Host;
	PEDiff m_Diff;
	std::wstring m_OtherPath;
	std::vector<int> m_Rows;	// the items that are shown
	CListViewCtrl m_List;
	CToolBarCtrl m_tb;
	CQuickFindEdit m_Filter;
	CString m_FilterText;
	bool m_ShowSame{ false };
};
