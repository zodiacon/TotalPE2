#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "PEFile.h"
#include "resource.h"

class CExportsView :
	public CViewBase<CExportsView>,
	public CVirtualListView<CExportsView> {
public:
	CExportsView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col);
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);
	int GetRowImage(HWND, int row, int) const;
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;

	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CExportsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CExportsView>)
		CHAIN_MSG_MAP(CViewBase<CExportsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnDissassemble)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	enum class ColumnType {
		Name, Ordinal, RVA, NameRVA, ForwardedName, UndecoratedName, Detials
	};

	struct Export : libpe::PEExportFunction {
		std::wstring Name;
		bool FromSymbols{ false };
	};

	void BuildItems();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDissassemble(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	std::vector<Export> m_Exports;
	PEFile const& m_PE;
};

