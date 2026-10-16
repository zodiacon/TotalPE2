#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <RpcInterfaces.h>
#include <VirtualListView.h>

//
// The RPC interfaces of the image (RpcInterfaces.h): those it serves and those it calls, their UUIDs, versions and endpoints;
// below them the procedures of the one selected, by operation number: the routine that serves each and its parameters, as
// the procedure's format describes them. Double-click goes to the interface's structure, or to a routine; the commands of
// functions (Decompile, Disassemble...) are for the routine selected.
//
class CRpcView :
	public CViewBase<CRpcView>,
	public CVirtualListView<CRpcView> {
public:
	CRpcView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	void AnnotationsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CRpcView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CRpcView>)
		CHAIN_MSG_MAP(CViewBase<CRpcView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CRpcView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const;
	// the procedures of the interface selected
	void FillProcedures();
	// the routine of the procedure selected; 0 if none
	DWORD SelectedRoutine() const;

	CSplitterWindowT<false> m_Splitter;
	CListViewCtrl m_Interfaces, m_Procedures;
	PEFile const& m_PE;
	std::vector<RpcInterface const*> m_InterfaceItems;
	std::vector<RpcProcedure const*> m_ProcedureItems;
	RpcInterface const* m_Selected{ nullptr };
};
