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
	std::wstring GetTarget(PERelocData const& reloc) const;

	CListViewCtrl m_List, m_RelocList;
	CCustomSplitterWindow m_Splitter;
	std::vector<PERelocation> m_Items;
	std::vector<PERelocData> m_RelocData;
	DWORD m_BlockRva{};	// page RVA of the block whose entries are listed in m_RelocList
	PEFile const& m_PE;
};

