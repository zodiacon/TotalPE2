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
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;

	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CSectionsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CSectionsView>)
		CHAIN_MSG_MAP(CViewBase<CSectionsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_DATA_SAVE, OnSaveData)
		CHAIN_MSG_MAP_ALT(CViewBase<CSectionsView>, 1)
	END_MSG_MAP()

private:
	enum ColumnType {
		Name, Size, RawData, RawSize, Characteristics, Relocations, LineNumbers, PointerToLines, PointerToReloc, Address, Entropy,
	};

	struct Section : PESectionHeader {
		double Entropy{ -1 };	// of the section's data in the file, 0 to 8; -1 if it has no data in the file
	};

	CString GetTitle() const override;

	void BuildItems();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnSaveData(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CListViewCtrl m_List;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<Section> m_Sections;
	PEFile const& m_PE;
	CHexView m_HexView;
};
