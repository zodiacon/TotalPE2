#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"

class CExportsView :
	public CViewBase<CExportsView>,
	public CVirtualListView<CExportsView> {
public:
	CExportsView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);
	int GetRowImage(HWND, int row, int) const;
	int GetSaveColumnRange(HWND, int&) const;

	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CPEImageView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CExportsView>)
		CHAIN_MSG_MAP(CViewBase<CExportsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	enum class ColumnType {
		Name, Ordinal, RVA, NameRVA, ForwardedName, UndecoratedName
	};

	void BuildItems();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CListViewCtrl m_List;
	std::vector<libpe::PEExportFunction> m_Exports;
	PEFile const& m_PE;
};
