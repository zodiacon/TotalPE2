#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>

class CAcceleratorTableView :
	public CViewBase<CAcceleratorTableView>,
	public CVirtualListView<CAcceleratorTableView> {
public:
	CAcceleratorTableView(IMainFrame* frame, PCWSTR title);

	CString GetTitle() const override;

	void AddAccelTable(std::span<const std::byte> data);
	void UpdateUI(bool first);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CAcceleratorTableView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CAcceleratorTableView>)
		CHAIN_MSG_MAP(CViewBase<CAcceleratorTableView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(BaseFrame, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

private:
	static std::wstring KeyFlagsToString(DWORD flags);

	struct Accelerator {
		WORD Flags;
		WORD Key;
		WORD Id;
		WORD _padding;
	};

	CListViewCtrl m_List;
	std::vector<Accelerator> m_Items;
	CString m_Title;
};


