#pragma once

#include "ViewBase.h"
#include "FlowGraph.h"
#include "resource.h"
#include <NodeGraphControl.h>
#include <unordered_set>

// The flow of a function as a graph: the blocks of code are the nodes, with their instructions inside, and the edges go from a
// block to the blocks that it can continue in. A conditional jump has a green edge to where it jumps to (true) and a red edge
// to the next instruction (false). Double-click a block to see it in the disassembly.
class CFlowGraphView :
	public CViewBase<CFlowGraphView> {
public:
	CFlowGraphView(IMainFrame* frame, PEFile const& pe, uint64_t function, PCWSTR title);
	CString GetTitle() const override;

	// Finds the blocks and creates the nodes. False if there is no code at the address.
	bool Build();

	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CFlowGraphView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(NGCN_NODEDBLCLICK, OnNodeDoubleClick)
		CHAIN_MSG_MAP(CViewBase<CFlowGraphView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ICON_EXPORT, OnExport)
		CHAIN_MSG_MAP_ALT(CViewBase<CFlowGraphView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExport(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnNodeDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

private:
	std::wstring BlockText(FlowBlock const& block, std::unordered_set<uint64_t> const& starts) const;
	void ShowStart();

	NodeGraphCtrl::CNodeGraphControl m_Graph;
	PEFile const& m_PE;
	CString m_Title;
	uint64_t m_Function;
	FlowGraph m_FlowGraph;
	std::vector<NodeGraphCtrl::NodeId> m_Nodes;		// by block
	CString m_Status;
	bool m_Shown{ false };		// the view was fit to the window once the window had a size
};
