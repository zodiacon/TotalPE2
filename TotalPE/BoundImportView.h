#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"

class CBoundImportView :
	public CViewBase<CBoundImportView>,
	public CVirtualListView<CBoundImportView> {
public:
	CBoundImportView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CBoundImportView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CBoundImportView>)
		CHAIN_MSG_MAP(CViewBase<CBoundImportView>)
		ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CBoundImportView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	struct Item {
		std::wstring Module;
		bool Forwarder;
		DWORD TimeStamp;
		DWORD NameOffset;	// relative to the start of the directory
		DWORD ForwarderCount;
	};

	void BuildItems();

	CListViewCtrl m_List;
	std::vector<Item> m_Items;
	PEFile const& m_PE;
};
