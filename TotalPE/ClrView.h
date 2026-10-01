#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"
#include "ClrMetadata.h"
#include <CustomSplitterWindow.h>

class CClrView :
	public CViewBase<CClrView>,
	public CVirtualListView<CClrView> {
public:
	CClrView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CClrView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CClrView>)
		CHAIN_MSG_MAP(CViewBase<CClrView>)
		ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CClrView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	struct DataItem {
		std::wstring Name;
		std::wstring Value;
	};
	struct DetailItem {
		std::wstring Category;
		std::wstring Name;
		std::wstring Value;
	};

	void BuildItems();

	CListViewCtrl m_GenList, m_Details;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<DataItem> m_Data;
	std::vector<DetailItem> m_Items;
	ClrMetadata m_Clr;
	PEFile const& m_PE;
};
