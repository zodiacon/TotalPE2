#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEAnomalies.h"

// Lists what looks unusual in the file (see PEAnomalies.h). Double-click a row to see the place in the hex view.
class CAnomalyView :
	public CViewBase<CAnomalyView>,
	public CVirtualListView<CAnomalyView> {
public:
	CAnomalyView(IMainFrame* frame, std::vector<Anomaly> const& anomalies);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;

	BEGIN_MSG_MAP(CAnomalyView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CAnomalyView>)
		CHAIN_MSG_MAP(CViewBase<CAnomalyView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CAnomalyView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	CListViewCtrl m_List;
	std::vector<Anomaly> m_Items;
};
