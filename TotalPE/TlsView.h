#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"
#include <CustomSplitterWindow.h>

class CTlsView :
	public CViewBase<CTlsView>,
	public CVirtualListView<CTlsView> {
public:
	CTlsView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CTlsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CTlsView>)
		CHAIN_MSG_MAP(CViewBase<CTlsView>)
		ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CTlsView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	struct DataItem {
		std::wstring Name;
		std::wstring Value;
	};

	void BuildItems();

	CListViewCtrl m_List, m_GenList;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<DWORD> m_Items;
	std::vector<DataItem> m_Data;
	PEFile const& m_PE;
};

