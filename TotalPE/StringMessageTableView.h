#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <SortedFilteredVector.h>

class CStringMessageTableView :
	public CViewBase<CStringMessageTableView>,
	public CVirtualListView<CStringMessageTableView> {
public:
	CStringMessageTableView(IMainFrame* frame, PCWSTR title);

	CString GetTitle() const override;
	void UpdateUI(bool first = false);

	void SetMessageTableData(std::span<const std::byte> data);
	void SetStringTableData(std::span<const std::byte> data, UINT id);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	BEGIN_MSG_MAP(CStringMessageTableView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CStringMessageTableView>)
		CHAIN_MSG_MAP(CViewBase<CStringMessageTableView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(BaseFrame, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) const;

private:
	struct Item {
		uint32_t Index;
		uint32_t Id;
		std::wstring Text;
	};

	CListViewCtrl m_List;
	CString m_Title;
	SortedFilteredVector<Item> m_Items;
};

