#pragma once

#include <Win32Api.h>
#include <QuickFindEdit.h>
#include <VirtualListView.h>

//
// The functions, types and constants of the metadata in a list: those whose names contain the filter's text, of the kinds
// chosen. The one selected is previewed (OnOpen, pin false); Enter or a double-click opens it in a tab that stays
// (pin true). Down or Enter in the filter goes to the list.
//
class CSearchPane :
	public CWindowImpl<CSearchPane>,
	public CVirtualListView<CSearchPane> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerSearch", 0, -1)

	explicit CSearchPane(Win32Api& api);

	std::function<void(size_t entry, bool pin)> OnOpen;
	// the counts changed
	std::function<void()> OnCountChanged;

	// the API of another machine (the filter stays; the one selected, by name, if it is there)
	void SetApi(Win32Api& api);
	void SetImageList(HIMAGELIST images);
	void FocusFilter();
	// selects an entry (the filter is cleared if it hides it), without opening it
	bool SelectEntry(size_t entry);
	size_t GetShownCount() const;
	BOOL PreTranslateMessage(MSG* pMsg);

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_MSG_MAP(CSearchPane)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_CODE_HANDLER(CBN_SELCHANGE, OnKindChanged)
		CHAIN_MSG_MAP(CVirtualListView<CSearchPane>)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	// (in the theme's color: the class's brush is the system's)
	LRESULT OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL&) {
		CDCHandle dc((HDC)wp);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, COLOR_WINDOW);
		return 1;
	}
	LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnKindChanged(WORD, WORD, HWND, BOOL&);

	void Fill();
	void UpdateLayout();
	bool KindShown(Win32Api::Entry::Kinds kind) const;
	int SelectedEntry() const;

	Win32Api* m_Api;
	std::vector<std::string> m_LowerNames;	// of the entries (the filter is not case sensitive)
	std::vector<size_t> m_Items;			// the entries shown
	std::string m_Filter;					// in lowercase
	int m_KindFilter{ 0 };
	bool m_Selecting{ false };				// (a selection that is not the user's)
	CQuickFindEdit m_FilterEdit;
	CComboBox m_Kinds;
	CListViewCtrl m_List;
};
