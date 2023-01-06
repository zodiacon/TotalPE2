#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "HexView.h"
#include <CustomSplitterWindow.h>

class PEFile;

class CSecurityView :
	public CViewBase<CSecurityView>,
	public CVirtualListView<CSecurityView> {
public:
	CSecurityView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe), m_HexView(frame) {}

	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState);
	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CSecurityView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CSecurityView>)
		CHAIN_MSG_MAP(CViewBase<CSecurityView>)
	ALT_MSG_MAP(1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	void BuildItems();

	CHexView m_HexView;
	CListViewCtrl m_List;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<libpe::PESecurity> m_Items;
	PEFile const& m_PE;
};

