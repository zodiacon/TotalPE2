#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <VirtualListView.h>
#include "SecuritySummary.h"

class PEFile;

// The protections of the file in one list: ASLR, DEP, Control Flow Guard, CET, SafeSEH, /GS, its signature and more (see
// SecuritySummary.h), each with whether the file has it and why.
class CSecuritySummaryView :
	public CViewBase<CSecuritySummaryView>,
	public CVirtualListView<CSecuritySummaryView> {
public:
	CSecuritySummaryView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {}
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int, int, UINT oldState, UINT newState);
	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CSecuritySummaryView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CSecuritySummaryView>)
		CHAIN_MSG_MAP(CViewBase<CSecuritySummaryView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CSecuritySummaryView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCustomDraw(int, LPNMHDR, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	PEFile const& m_PE;
	CListViewCtrl m_List;
	std::vector<SecurityFeature> m_Items;
};
