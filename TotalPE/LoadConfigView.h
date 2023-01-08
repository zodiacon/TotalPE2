#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>

class PEFile;

class CLoadConfigView :
	public CViewBase<CLoadConfigView>,
	public CVirtualListView<CLoadConfigView> {
public:
	CLoadConfigView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CLoadConfigView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CLoadConfigView>)
		CHAIN_MSG_MAP(CViewBase<CLoadConfigView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	void BuildItems();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	struct DataItem {
		std::wstring Name;
		std::wstring Value;
		std::wstring Details;
	};

	CListViewCtrl m_List;
	std::vector<DataItem> m_Items;
	PEFile const& m_PE;
};
