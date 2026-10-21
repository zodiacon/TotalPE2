#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <Capabilities.h>

class PEFile;

// What the program can do (Capabilities.h): a row for each capability, with its namespace, its ATT&CK techniques and what in
// the file told it.
class CCapabilitiesView :
	public CViewBase<CCapabilitiesView>,
	public CVirtualListView<CCapabilitiesView> {
public:
	CCapabilitiesView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CCapabilitiesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CCapabilitiesView>)
		CHAIN_MSG_MAP(CViewBase<CCapabilitiesView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CCapabilitiesView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);

private:
	struct Row {
		std::wstring Name, Namespace, Attack, Evidence;
		size_t Index{};
	};

	CListViewCtrl m_List;
	std::vector<Row> m_Rows;
};
