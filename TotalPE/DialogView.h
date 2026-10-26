#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <CustomSplitterWindow.h>
#include "DialogTemplate.h"

// Scrollable surface that hosts the live preview of a dialog.
class CDialogHost : public CScrollWindowImpl<CDialogHost> {
public:
	DECLARE_WND_CLASS_EX(L"TotalPE.DialogHost", CS_HREDRAW | CS_VREDRAW, COLOR_APPWORKSPACE)

	BEGIN_MSG_MAP(CDialogHost)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_PLACE, OnPlace)
		CHAIN_MSG_MAP(CScrollWindowImpl<CDialogHost>)
	END_MSG_MAP()

	void DoPaint(CDCHandle) {}
	bool ShowDialog(DlgTemplate const& dlg);

private:
	static constexpr UINT WM_PLACE = WM_APP + 1;
	// the dialog with a margin around it, at the DPI of the host
	void Place();
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPlace(UINT, WPARAM, LPARAM, BOOL&);

	HWND m_hDlg{ nullptr };
};

class CDialogView :
	public CViewBase<CDialogView>,
	public CVirtualListView<CDialogView> {
public:
	CDialogView(IMainFrame* frame, PCWSTR title);

	CString GetTitle() const override;
	bool SetData(std::span<const std::byte> data);

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;

	BEGIN_MSG_MAP(CDialogView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CDialogView>)
		CHAIN_MSG_MAP(CViewBase<CDialogView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CDialogView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	struct Row {
		std::wstring Class, Id, Text, X, Y, Width, Height, Style, ExStyle;
	};

	void BuildRows();

	CCustomHorSplitterWindow m_Splitter;
	CDialogHost m_Host;
	CListViewCtrl m_List;
	std::vector<Row> m_Rows;
	DlgTemplate m_Dialog;
	CString m_Title;
};
