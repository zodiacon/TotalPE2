#pragma once

#include "ViewBase.h"
#include "resource.h"
#include "Driver.h"
#include <VirtualListView.h>

//
// What a kernel driver sets up (DriverInfo): its routines (DriverEntry, DriverUnload, DriverStartIo, the dispatch routine of
// each IRP_MJ_...), and below them the I/O control codes its device control routine handles, decoded. Double-click goes to
// a routine (or to the function that compares the code); the commands of functions (Decompile, Disassemble...) are for the
// one selected. Until the decompiler has found them, it says so.
//
class CDriverView :
	public CViewBase<CDriverView>,
	public CVirtualListView<CDriverView> {
public:
	CDriverView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	void AnnotationsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CDriverView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CDriverView>)
		CHAIN_MSG_MAP(CViewBase<CDriverView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CDriverView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const;
	void Fill();
	// the function of the row selected (in the list that has the focus, or the one with a selection); 0 if none
	DWORD SelectedFunction() const;

	struct Routine {
		std::wstring What;		// "IRP_MJ_CREATE", "DriverUnload"
		DWORD Rva;
		int Order;
	};
	CSplitterWindowT<false> m_Splitter;
	CListViewCtrl m_Routines, m_Ioctls;
	PEFile const& m_PE;
	std::vector<Routine> m_RoutineItems;
	std::vector<DriverInfo::Ioctl> m_IoctlItems;
};
