#pragma once

#include <FrameView.h>
#include "Interfaces.h"
#include <VirtualListView.h>

class PEFile;

class CPEImageView : 
	public CFrameView<CPEImageView, IMainFrame>,
	public CVirtualListView<CPEImageView>,
	public IView {
public:
	CPEImageView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	BEGIN_MSG_MAP(CPEImageView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CPEImageView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	CString GetTitle() const override;
	HWND GetHwnd() const override;

	void BuildItems();
	void UpdateUI();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	struct DataItem {
		std::wstring Name, Value, Details;
	};

	CListViewCtrl m_List;
	std::vector<DataItem> m_Items;
	PEFile const& m_PE;
};
