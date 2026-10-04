#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "GuardTables.h"

class PEFile;

// The entries of one of the Control Flow Guard tables (see GuardTables.h), with the names of what they point to.
// Double-click a row to see the code (or the import slot).
class CGuardTableView :
	public CViewBase<CGuardTableView>,
	public CVirtualListView<CGuardTableView> {
public:
	CGuardTableView(IMainFrame* frame, PEFile const& pe, GuardTable table);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CGuardTableView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CGuardTableView>)
		CHAIN_MSG_MAP(CViewBase<CGuardTableView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CGuardTableView>, 1)
	END_MSG_MAP()

private:
	struct Item : GuardEntry {
		int Index;
		std::wstring Name;	// what the RVA is: a function, an import slot...
		int Section;		// -1 if the RVA is not in a section
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	PEFile const& m_PE;
	GuardTable m_Table;
	std::vector<Item> m_Items;
	std::vector<std::wstring> m_SectionNames;
};
