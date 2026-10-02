#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "CodeAnalysis.h"

// The places in the code that refer to one address. Double-click a row to see the instruction.
class CXrefView :
	public CViewBase<CXrefView>,
	public CVirtualListView<CXrefView> {
public:
	CXrefView(IMainFrame* frame, PEFile const& pe, uint64_t target, std::span<const Xref> refs, PCWSTR title);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;

	BEGIN_MSG_MAP(CXrefView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CXrefView>)
		CHAIN_MSG_MAP(CViewBase<CXrefView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CXrefView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	struct Item {
		Xref Ref;
		mutable bool Resolved{ false };	// the text columns are filled on first use: they need the symbols and the code
		mutable CString Function, Instruction;
	};
	void Resolve(Item const& item) const;
	CString Describe(uint64_t va) const;

	CListViewCtrl m_List;
	PEFile const& m_PE;
	CString m_Title;
	uint64_t m_Target;
	mutable std::vector<Item> m_Items;
};
