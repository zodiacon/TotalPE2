#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <PEFile.h>
#include <CustomSplitterWindow.h>
#include "HexView.h"

class CImportsView :
	public CViewBase<CImportsView>,
	public CVirtualListView<CImportsView> {
public:
	CImportsView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND h, int row, int col) const;
	int GetRowImage(HWND h, int row, int) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CImportsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CImportsView>)
		CHAIN_MSG_MAP(BaseFrame)
		ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	enum ColumnType {
		ModuleName, Size, FunctionCount, Bound, FunctionName, Hint, Ordinal, UndecoratedName,
	};

	CString GetTitle() const override;

	void BuildItems();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	
	CListViewCtrl m_ModList, m_FuncList;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<libpe::PEImportFunction> m_Functions;
	std::vector<libpe::PEImport> m_Modules;
	int m_SelectedModule{ -1 };
	PEFile const& m_PE;
	bool m_Is64;
};
