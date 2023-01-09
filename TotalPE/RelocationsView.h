#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"
#include <CustomSplitterWindow.h>

class CRelocationsView :
	public CViewBase<CRelocationsView>,
	public CVirtualListView<CRelocationsView> {
public:
	CRelocationsView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	BEGIN_MSG_MAP(CRelocationsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CRelocationsView>)
		CHAIN_MSG_MAP(CViewBase<CRelocationsView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CRelocationsView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	void BuildItems();

	CListViewCtrl m_List, m_RelocList;
	CCustomSplitterWindow m_Splitter;
	std::vector<libpe::PERelocation> m_Items;
	std::vector<libpe::PERelocData> m_RelocData;
	PEFile const& m_PE;
};

