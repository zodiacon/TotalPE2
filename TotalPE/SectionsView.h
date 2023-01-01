#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <PEFile.h>
#include <CustomSplitterWindow.h>
#include "HexView.h"

class CSectionsView :
	public CViewBase<CSectionsView>,
	public CVirtualListView<CSectionsView> {
public:
	CSectionsView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);

	void UpdateUI();

	BEGIN_MSG_MAP(CSectionsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CSectionsView>)
		CHAIN_MSG_MAP(BaseFrame)
		ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	enum ColumnType {
		Name, Size, RawData, RawSize, Characteristics, Relocations, LineNumbers, PointerToLines, PointerToReloc, Address,
	};

	CString GetTitle() const override;

	struct SectionData : libpe::PESectionHeader {
		CString Name;
	};

	void BuildItems();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<SectionData> m_Sections;
	PEFile const& m_PE;
	CHexView m_HexView;
};
