#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"

class CRichHeaderView :
	public CViewBase<CRichHeaderView>,
	public CVirtualListView<CRichHeaderView> {
public:
	CRichHeaderView(IMainFrame* frame, PEFile const& pe);

	CString GetTitle() const override;
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CRichHeaderView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CRichHeaderView>)
		CHAIN_MSG_MAP(CViewBase<CRichHeaderView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CRichHeaderView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

private:
	static std::wstring ToolName(WORD productId);
	void BuildItems();

	struct Item : PERichEntry {
		std::wstring Tool;
	};

	CListViewCtrl m_List;
	std::vector<Item> m_Items;
	PEFile const& m_PE;
};
