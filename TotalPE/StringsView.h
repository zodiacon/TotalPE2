#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <QuickFindEdit.h>
#include <SortedFilteredVector.h>
#include "FileStrings.h"
#include "resource.h"

class PEFile;

// The ASCII and UTF-16 strings of the whole file: headers, sections and overlay.
// Double-click a row to see the string in the hex view.
class CStringsView :
	public CViewBase<CStringsView>,
	public CVirtualListView<CStringsView> {
public:
	CStringsView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CStringsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnDropDown)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_STRINGS_ASCII, OnToggleEncoding)
		COMMAND_ID_HANDLER(ID_STRINGS_UTF16, OnToggleEncoding)
		COMMAND_RANGE_HANDLER(ID_STRINGS_MINLENGTH_FIRST, ID_STRINGS_MINLENGTH_LAST, OnMinLength)
		CHAIN_MSG_MAP(CVirtualListView<CStringsView>)
		CHAIN_MSG_MAP(CViewBase<CStringsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_STRINGS_HEX, OnShowInHex)
		COMMAND_ID_HANDLER(ID_STRINGS_XREFS, OnXrefs)
		CHAIN_MSG_MAP_ALT(CViewBase<CStringsView>, 1)
	END_MSG_MAP()

private:
	struct Item : FoundString {
		int Section;	// index of the section that contains the string, -1 for the headers, -2 for the data after the sections
		uint32_t Rva;	// 0 if the string is not part of the image
	};

	void Scan();
	void ApplyFilter();
	Item const* GetSelectedItem() const;

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDropDown(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnToggleEncoding(WORD, WORD id, HWND, BOOL&);
	LRESULT OnMinLength(WORD, WORD id, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnShowInHex(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnXrefs(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	CToolBarCtrl m_tb;
	CQuickFindEdit m_Filter;
	PEFile const& m_PE;
	SortedFilteredVector<Item> m_Items;
	std::vector<std::wstring> m_SectionNames;
	StringScanOptions m_Options;
	CString m_FilterText;
};
