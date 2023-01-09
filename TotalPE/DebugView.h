#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "HexView.h"
#include <CustomSplitterWindow.h>

class PEFile;

class CDebugView :
	public CViewBase<CDebugView>,
	public CVirtualListView<CDebugView> {
public:
	CDebugView(IMainFrame* frame, PEFile const& pe);

	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState);
	std::wstring GetDetails(int row) const;

	BEGIN_MSG_MAP(CDebugView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP_MEMBER(m_HexView)
		CHAIN_MSG_MAP(CVirtualListView<CDebugView>)
		CHAIN_MSG_MAP(CViewBase<CDebugView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(BaseFrame, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

private:
	void BuildItems();

	CListViewCtrl m_List;
	CCustomHorSplitterWindow m_Splitter;
	CHexView m_HexView;
	std::unique_ptr<IBufferManager> m_Buffer;
	int m_SelectedIndex{ -1 };
	std::vector<libpe::PEDebug> m_Items;
	PEFile const& m_PE;
};

