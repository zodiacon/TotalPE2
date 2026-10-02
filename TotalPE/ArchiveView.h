#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "LibArchive.h"

enum class ArchiveViewKind {
	Members, Symbols, Imports,
};

// The lists of a library (see LibArchive.h): its members, the index of its symbols and the records of an import library.
// Double-click a row to see the data of the member in the hex view.
class CArchiveView :
	public CViewBase<CArchiveView>,
	public CVirtualListView<CArchiveView> {
public:
	CArchiveView(IMainFrame* frame, LibArchive const& archive, ArchiveViewKind kind);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CArchiveView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
		CHAIN_MSG_MAP(CVirtualListView<CArchiveView>)
		CHAIN_MSG_MAP(CViewBase<CArchiveView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CArchiveView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	enum Column {
		Index, Name, Type, Machine, Size, Offset, Sections, Symbols, Date,			// members
		Symbol, Undecorated, Member, MemberIndex,									// symbols
		Dll, ImportType, ImportBy, HintOrdinal,										// imports
	};

	int MemberOf(int row) const;
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnItemChanged(int, LPNMHDR, BOOL&);

	CListViewCtrl m_List;
	LibArchive const& m_Archive;
	ArchiveViewKind m_Kind;
	std::vector<int> m_Rows;		// the position of each row in the list of the archive: the rows can be sorted
};
