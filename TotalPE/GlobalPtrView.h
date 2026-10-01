#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"

// Shows the Global Pointer directory (IMAGE_DIRECTORY_ENTRY_GLOBALPTR). Its "address" is an RVA
// whose value is loaded into the global pointer register (IA64); the size is always zero.
class CGlobalPtrView :
	public CViewBase<CGlobalPtrView>,
	public CVirtualListView<CGlobalPtrView> {
public:
	CGlobalPtrView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CGlobalPtrView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CGlobalPtrView>)
		CHAIN_MSG_MAP(CViewBase<CGlobalPtrView>)
		ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CGlobalPtrView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	struct DataItem {
		std::wstring Name;
		std::wstring Value;
	};

	void BuildItems();

	CListViewCtrl m_List;
	std::vector<DataItem> m_Data;
	PEFile const& m_PE;
};
