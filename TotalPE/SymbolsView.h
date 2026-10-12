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
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const;
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	int GetSaveColumnRange(HWND, int&) const {
		return 1;
	}

	void UpdateUI(bool first = false) const;

	// Whether the list of the symbols of a kind would have any
	static bool HasSymbols(DiaSession const& session, SymViewType type);

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
	static std::vector<SymbolTag> Tags(SymViewType type);
	static bool Includes(SymViewType type, SymbolTag tag, DiaSymbol const& sym);

	enum ColumnType {
		Name, SymType, Type, RVA, Size, Location, DataKind, UndecoratedName,
	};

	// what is cheap to get is read up front; the rest when first shown (or sorted on)
	struct Item {
		DiaSymbol Symbol;
		std::wstring Name;
		SymbolTag Tag;
		LocationKind Location;
		uint32_t Rva;
		uint64_t Size;
		mutable std::wstring TypeName, Undecorated;
		mutable bool HasDetails{ false };
	};
	void GetDetails(Item const& item) const;
	static PCWSTR DataKindToString(DataItemKind kind);

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CListViewCtrl m_List;
	std::vector<Item> m_Items;
	DiaSession const& m_Session;
	SymViewType m_Type;
};
