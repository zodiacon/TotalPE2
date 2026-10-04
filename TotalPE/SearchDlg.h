#pragma once

#include <DialogHelper.h>
#include <VirtualListView.h>
#include "resource.h"
#include "SearchIndex.h"

// What the search dialog needs from the main frame: the items of the file, and a way to show one
struct ISearchHost abstract {
	// built when first asked for (and again after the file changes)
	virtual SearchIndex const& GetSearchIndex() = 0;
	virtual bool GoToSearchItem(SearchItem const& item) = 0;
	// the icons of the items (SearchItem::Image)
	virtual HIMAGELIST GetImageList() const = 0;
};

// Search All: one search through the imports, exports, strings, resources, symbols and section names of the file.
// Modeless and resizable; Search (or Enter) searches. Double-click (or Enter on) a result to show it.
class CSearchDlg :
	public CDialogImpl<CSearchDlg>,
	public CDialogResize<CSearchDlg>,
	public CVirtualListView<CSearchDlg> {
public:
	enum { IDD = IDD_SEARCH };

	explicit CSearchDlg(ISearchHost* host) : m_Host(host) {}

	// creates the dialog the first time; shows it and selects the text
	void Show(HWND hOwner);
	// The file (or what is known about it) changed: the results are dropped, and found again if the dialog is visible
	void Invalidate();

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_DLGRESIZE_MAP(CSearchDlg)
		DLGRESIZE_CONTROL(IDC_SEARCH_TEXT, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SEARCH_RESULTS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_SEARCH_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CSearchDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SEARCH_AGAIN, OnSearchAgain)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		COMMAND_RANGE_HANDLER(IDC_SEARCH_IMPORTS, IDC_SEARCH_MATCHCASE, OnOptionChanged)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		CHAIN_MSG_MAP(CVirtualListView<CSearchDlg>)
		CHAIN_MSG_MAP(CDialogResize<CSearchDlg>)
	END_MSG_MAP()

private:
	enum ColumnType { Kind, Name, Details, Location };

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSearchAgain(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnShowWindow(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOptionChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD, HWND, BOOL&);

	void Search();
	uint32_t SelectedKinds() const;
	bool GoTo(int row);

	ISearchHost* m_Host;
	CListViewCtrl m_List;
	std::vector<uint32_t> m_Results;	// indices in the search index
	SearchIndex const* m_Index{ nullptr };	// the index the results are from
	static constexpr size_t MaxResults = 20000;
	static constexpr UINT WM_SEARCH_AGAIN = WM_APP + 1;
};
