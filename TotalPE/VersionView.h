#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>

class CVersionView :
	public CViewBase<CVersionView>,
	public CVirtualListView<CVersionView> {
public:
	CVersionView(IMainFrame* frame, PCWSTR title);

	CString GetTitle() const override;

	void SetData(std::span<const std::byte> data);
	void UpdateUI(bool first);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;

	BEGIN_MSG_MAP(CVersionView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CViewBase<CVersionView>)
		CHAIN_MSG_MAP(CVirtualListView<CVersionView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CVersionView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

private:
	struct Item {
		std::wstring Name;
		std::wstring Value;
	};

	void BuildItems();

	std::span<const std::byte> m_Data;
	CListViewCtrl m_List;
	std::vector<Item> m_Items;
	CString m_Title;
};

