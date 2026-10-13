#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <VirtualListView.h>

//
// The bookmarks of the file (kept with its other annotations): their names, addresses, the functions they are in and the
// comments there. Double-click (or Enter) goes to the code; F2 renames a bookmark, Delete removes it.
//
class CBookmarksView :
	public CViewBase<CBookmarksView>,
	public CVirtualListView<CBookmarksView> {
public:
	CBookmarksView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	void AnnotationsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_MSG_MAP(CBookmarksView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
		CHAIN_MSG_MAP(CVirtualListView<CBookmarksView>)
		CHAIN_MSG_MAP(CViewBase<CBookmarksView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_BOOKMARK_RENAME, OnRename)
		COMMAND_ID_HANDLER(ID_BOOKMARK_DELETE, OnDelete)
		CHAIN_MSG_MAP_ALT(CViewBase<CBookmarksView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListKeyDown(int, LPNMHDR, BOOL&);
	LRESULT OnRename(WORD, WORD, HWND, BOOL&);
	LRESULT OnDelete(WORD, WORD, HWND, BOOL&);
	void Fill();
	int Selected() const;

	struct Item {
		uint32_t Rva;
		std::wstring Name;
		std::wstring Function;	// where it is
		std::wstring Comment;
		int Order;				// in the order they were added
	};
	CListViewCtrl m_List;
	PEFile const& m_PE;
	std::vector<Item> m_Items;
};
