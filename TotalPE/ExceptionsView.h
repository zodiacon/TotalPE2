#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <CustomSplitterWindow.h>
#include "PEFile.h"
#include "UnwindInfo.h"
#include "resource.h"

// The functions of the exception directory (x64), with their unwind information. The lower list shows how the unwinder
// undoes the prolog of the selected function, and its exception handler. Double-click to see the code.
class CExceptionsView :
	public CViewBase<CExceptionsView>,
	public CVirtualListView<CExceptionsView> {
public:
	CExceptionsView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND h, int col) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;

	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CExceptionsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CExceptionsView>)
		CHAIN_MSG_MAP(CViewBase<CExceptionsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CExceptionsView>, 1)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	struct Exception : PEException {
		std::wstring FuncName;
		std::wstring UndecoratedName;
		long Disp{ 0 };
		UnwindInfo Unwind;
		std::wstring HandlerName;
	};

	// a row of the lower list: an unwind code, or the handler or chained function
	struct Detail {
		std::wstring Offset, Operation, Details;
		uint32_t Rva{ 0 };	// where a double-click goes
	};

	void BuildItems();
	void BuildDetails();

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CCustomHorSplitterWindow m_Splitter;
	CListViewCtrl m_List, m_Details;
	std::vector<Exception> m_Items;
	std::vector<Detail> m_DetailItems;
	PEFile const& m_PE;
	bool m_IsX64{ false };
};
