#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "VirusTotal.h"

class PEFile;

class CPEImageView : 
	public CViewBase<CPEImageView>,
	public CVirtualListView<CPEImageView> {
public:
	CPEImageView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void SetVirusTotalStatus(vt::Status const& status) override;

	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CPEImageView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CPEImageView>)
		CHAIN_MSG_MAP(CViewBase<CPEImageView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CPEImageView>, 1)
	END_MSG_MAP()

private:
	CString GetTitle() const override;

	void BuildItems();
	void ComputeHashes();
	void AppendVirusTotal();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	struct DataItem {
		std::wstring Name;
		std::wstring Value;
		std::wstring Details;
	};

	CListViewCtrl m_List;
	std::vector<DataItem> m_Items;
	vt::Status m_Vt;
	PEFile const& m_PE;
	// of the whole file: computed once, as the items are rebuilt when the VirusTotal scan progresses
	std::wstring m_Md5, m_Sha1, m_Sha256;
	uint32_t m_FileChecksum{ 0 };
};
