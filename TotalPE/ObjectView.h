#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "CoffObject.h"

enum class ObjectViewKind {
	Header, Sections, Symbols, Relocations, LineNumbers,
};

// The lists of a COFF object file (see CoffObject.h): its header, sections, symbols, relocations and line numbers.
// Double-click a section, a symbol, a relocation or a line to see the place in the data of its section.
class CObjectView :
	public CViewBase<CObjectView>,
	public CVirtualListView<CObjectView> {
public:
	// 'member' is the member of the library the object is (-1 for an object file); 'owner' its name for the title
	CObjectView(IMainFrame* frame, CoffObject const& obj, ObjectViewKind kind, int member = -1, PCWSTR owner = nullptr);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CObjectView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CObjectView>)
		CHAIN_MSG_MAP(CViewBase<CObjectView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CObjectView>, 1)
	END_MSG_MAP()

private:
	enum Column {
		Name, Value, Details,																	// header
		Number, SectionName, RawSize, RawOffset, RelocCount, RelocOffset, Characteristics,		// sections
		Index, SymbolName, Undecorated, SymbolValue, Section, Type, StorageClass, Aux, SymbolDetails,	// symbols
		RelocSection, RelocOffsetInSection, RelocType, RelocSymbol, RelocSymbolIndex,			// relocations
		LineSection, LineFunction, LineNumber, LineOffset,										// line numbers
	};

	struct HeaderItem {
		std::wstring Name, Value, Details;
	};

	void BuildHeader();
	std::wstring SectionText(int32_t number) const;
	std::wstring SymbolNameAt(uint32_t index) const;

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	CoffObject const& m_Object;
	ObjectViewKind m_Kind;
	std::vector<HeaderItem> m_Header;
	std::vector<std::wstring> m_LineFunctions;	// for each line number, the function it is in
	int m_Member;
	CString m_Owner;
	std::vector<int> m_Rows;		// the position of each row in the list of the object: the rows can be sorted
};
