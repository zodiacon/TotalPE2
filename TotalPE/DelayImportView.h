#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <PEFile.h>
#include <CustomSplitterWindow.h>

class CDelayImportView :
	public CViewBase<CDelayImportView>,
	public CVirtualListView<CDelayImportView> {
public:
	CDelayImportView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	void DoSort(SortInfo const* si);

	BEGIN_MSG_MAP(CDelayImportView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CDelayImportView>)
		CHAIN_MSG_MAP(CViewBase<CDelayImportView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CDelayImportView>, 1)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	void BuildItems();

	enum class ColumnType {
		Module, Hint, Functions, DllName, IAT, ImportNameTable, ModuleHandle, TimeStamp, Attributes,
		Name, BoundImport, UnloadInfo,
	};

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CListViewCtrl m_List, m_FuncList;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<libpe::PEDelayImport> m_Items;
	std::vector<libpe::PEDelayImportFunc> m_Functions;
	PEFile const& m_PE;
};

