#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <DiaHelper.h>

class CSymbolsView :
	public CViewBase<CSymbolsView>,
	public CVirtualListView<CSymbolsView> {
public:
	CSymbolsView(IMainFrame* frame, DiaSession const& session, SymViewType type);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CSymbolsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CSymbolsView>)
		CHAIN_MSG_MAP(CViewBase<CSymbolsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CSymbolsView>, 1)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	void BuildItems();

	enum ColumnType {
		Name, Type, Value, UndecoratedName, Location, Class,
	};

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CListViewCtrl m_List;
	std::vector<DiaSymbol> m_Items;
	DiaSession const& m_Session;
	SymViewType m_Type;
};
