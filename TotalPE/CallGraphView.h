#pragma once

#include "ViewBase.h"
#include "FlowGraph.h"
#include "resource.h"
#include <NodeGraphControl.h>

// The functions a function calls (below it) and the functions that call it (above it), with the number of calls on the
// edges. Double-click a function to see its own call graph; right-click it to disassemble or decompile it.
class CCallGraphView :
	public CViewBase<CCallGraphView> {
public:
	CCallGraphView(IMainFrame* frame, PEFile const& pe, uint64_t function, PCWSTR title);
	CString GetTitle() const override;

	// Finds the calls and creates the nodes. False if there is no code at the address.
	bool Build();
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CCallGraphView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(NGCN_NODEDBLCLICK, OnNodeDoubleClick)
		CHAIN_MSG_MAP(CViewBase<CCallGraphView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ICON_EXPORT, OnExport)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnExport)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CCallGraphView>, 1)
	END_MSG_MAP()

	bool CanSave() const override {
		return true;	// as SVG
	}

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExport(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFunctionCommand(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnNodeDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// a node of the graph: the function, a caller or a callee
	struct Node {
		NodeGraphCtrl::NodeId Id;
		CallGraphNode Function;
	};
	Node const* NodeOf(NodeGraphCtrl::NodeId id) const;
	std::wstring NameOf(CallGraphNode const& node) const;
	void ShowAll();

	NodeGraphCtrl::CNodeGraphControl m_Graph;
	PEFile const& m_PE;
	CString m_Title;
	uint64_t m_Function;
	CallGraph m_Calls;
	std::vector<Node> m_Nodes;
	uint64_t m_Target{ 0 };		// the function of the context menu
	CString m_Status;
	bool m_Shown{ false };
};
