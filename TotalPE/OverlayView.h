#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "Overlay.h"

class PEFile;

// What follows the last section of the file: where it starts, how big it is, what it looks like and its hashes.
// Double-click a row to see the data in the hex view.
class COverlayView :
	public CViewBase<COverlayView>,
	public CVirtualListView<COverlayView> {
public:
	COverlayView(IMainFrame* frame, PEFile const& pe, OverlayInfo const& info);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	bool IsSortable(HWND, int col) const { return false; }
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(COverlayView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<COverlayView>)
		CHAIN_MSG_MAP(CViewBase<COverlayView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<COverlayView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

private:
	struct Item {
		std::wstring Name, Value, Details;
		int64_t Offset{ -1 };	// where a double-click goes
	};
	void BuildItems();

	CListViewCtrl m_List;
	PEFile const& m_PE;
	OverlayInfo m_Info;
	std::vector<Item> m_Items;
};
