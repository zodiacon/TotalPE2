#include "pch.h"
#include "RpcView.h"
#include <SortHelper.h>

CRpcView::CRpcView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CRpcView::GetTitle() const {
	return L"RPC";
}

void CRpcView::AnnotationsChanged() {
	// (the names of the routines, and of the interfaces, of the symbols)
	for (auto list : { &m_Interfaces, &m_Procedures })
		list->RedrawItems(0, list->GetItemCount());
}

namespace {
	std::wstring Wide(std::string const& text) {
		return std::wstring(text.begin(), text.end());
	}

	std::wstring EndpointsText(RpcInterface const& rpc) {
		std::wstring text;
		for (auto const& [protseq, endpoint] : rpc.Endpoints)
			text += (text.empty() ? L"" : L"; ") + Wide(protseq) + L":" + Wide(endpoint);
		return text;
	}

	std::wstring ParametersText(RpcProcedure const& p) {
		if (!p.Described)
			return L"(its format is not known)";
		std::wstring text = p.Async ? L"[async]" : L"";
		for (auto const& parameter : p.Parameters)
			text += (text.empty() ? L"" : L", ") + Wide(RpcParameterText(parameter));
		return text.empty() ? L"(none)" : text;
	}
}

CString CRpcView::GetColumnText(HWND h, int row, int col) const {
	auto base = m_PE.GetImageBase();
	if (h == m_Interfaces) {
		auto const& rpc = *m_InterfaceItems[row];
		switch (col) {
			case 0: return Frame()->RpcInterfaceName(rpc).c_str();
			case 1: return Wide(RpcUuidText(rpc.Uuid)).c_str();
			case 2: return std::format(L"{}.{}", rpc.Major, rpc.Minor).c_str();
			case 3: return rpc.Server ? L"Server" : L"Client";
			case 4: return rpc.Server ? std::to_wstring(rpc.DispatchCount).c_str() : L"";
			case 5: return rpc.Ndr64 ? L"NDR64" : L"NDR";
			case 6: return EndpointsText(rpc).c_str();
			case 7: return std::format(L"0x{:08X}", rpc.Flags).c_str();
			case 8: return std::format(L"0x{:X}", rpc.Rva).c_str();
		}
		return CString();
	}
	auto const& p = *m_ProcedureItems[row];
	switch (col) {
		case 0: return std::to_wstring(p.Opnum).c_str();
		case 1: return p.Routine ? Frame()->ResolveRva(p.Routine).c_str() : L"";
		case 2: return ParametersText(p).c_str();
		case 3: return p.HasResult ? Wide(p.Result.Type).c_str() : L"";
		case 4: return p.Routine ? std::format(L"0x{:X}", base + p.Routine).c_str() : L"";
		case 5: return p.Routine ? std::format(L"0x{:X}", p.Routine).c_str() : L"";
	}
	return CString();
}

void CRpcView::DoSort(SortInfo const* si) {
	if (!si)
		return;
	auto asc = si->SortAscending;
	if (si->hWnd == m_Interfaces) {
		auto compare = [&](RpcInterface const* a, RpcInterface const* b) {
			switch (si->SortColumn) {
				case 0: return SortHelper::Sort(Frame()->RpcInterfaceName(*a), Frame()->RpcInterfaceName(*b), asc);
				case 1: return SortHelper::Sort(RpcUuidText(a->Uuid), RpcUuidText(b->Uuid), asc);
				case 2: return SortHelper::Sort(a->Major * 0x10000 + a->Minor, b->Major * 0x10000 + b->Minor, asc);
				case 3: return SortHelper::Sort(a->Server, b->Server, asc);
				case 4: return SortHelper::Sort(a->DispatchCount, b->DispatchCount, asc);
				case 5: return SortHelper::Sort(a->Ndr64, b->Ndr64, asc);
				case 6: return SortHelper::Sort(EndpointsText(*a), EndpointsText(*b), asc);
				case 7: return SortHelper::Sort(a->Flags, b->Flags, asc);
				case 8: return SortHelper::Sort(a->Rva, b->Rva, asc);
			}
			return false;
		};
		std::stable_sort(m_InterfaceItems.begin(), m_InterfaceItems.end(), compare);
		return;
	}
	auto compare = [&](RpcProcedure const* a, RpcProcedure const* b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a->Opnum, b->Opnum, asc);
			case 1: return SortHelper::Sort(Frame()->ResolveRva(a->Routine), Frame()->ResolveRva(b->Routine), asc);
			case 2: return SortHelper::Sort(ParametersText(*a), ParametersText(*b), asc);
			case 3: return SortHelper::Sort(a->Result.Type, b->Result.Type, asc);
			case 4: case 5: return SortHelper::Sort(a->Routine, b->Routine, asc);
		}
		return false;
	};
	std::stable_sort(m_ProcedureItems.begin(), m_ProcedureItems.end(), compare);
}

void CRpcView::FillProcedures() {
	m_ProcedureItems.clear();
	auto row = m_Interfaces.GetSelectedIndex();
	m_Selected = row >= 0 && row < (int)m_InterfaceItems.size() ? m_InterfaceItems[row] : nullptr;
	if (m_Selected)
		for (auto const& p : m_Selected->Procedures)
			m_ProcedureItems.push_back(&p);
	m_Procedures.SetItemCountEx((int)m_ProcedureItems.size(), LVSICF_NOSCROLL);
	Sort(m_Procedures);
	m_Procedures.RedrawItems(0, m_Procedures.GetItemCount());
}

DWORD CRpcView::SelectedRoutine() const {
	auto row = m_Procedures.GetSelectedIndex();
	return row >= 0 && row < (int)m_ProcedureItems.size() ? m_ProcedureItems[row]->Routine : 0;
}

void CRpcView::OnStateChanged(HWND h, int, int, UINT oldState, UINT newState) {
	if (!((newState ^ oldState) & LVIS_SELECTED))
		return;
	if (h == m_Interfaces) {
		auto row = m_Interfaces.GetSelectedIndex();
		if ((row >= 0 && row < (int)m_InterfaceItems.size() ? m_InterfaceItems[row] : nullptr) != m_Selected)
			FillProcedures();
	}
	UpdateUI();
}

void CRpcView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	bool routine = SelectedRoutine() != 0;
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		ui.UIEnable(id, routine);
	if (first) {
		auto const& all = Frame()->RpcInterfaces();
		size_t servers = 0, procedures = 0;
		for (auto const& rpc : all)
			if (rpc.Server) {
				servers++;
				procedures += rpc.DispatchCount;
			}
		Frame()->SetStatusText(1, std::format(L"RPC: it serves {} interfaces ({} procedures), it calls {}", servers, procedures,
			all.size() - servers).c_str());
	}
}

bool CRpcView::OnDoubleClickList(HWND h, int row, int, CPoint const&) const {
	if (row < 0)
		return false;
	if (h == m_Interfaces)
		return row < (int)m_InterfaceItems.size() && Frame()->GoToVa(m_PE.GetImageBase() + m_InterfaceItems[row]->Rva);
	auto rva = row < (int)m_ProcedureItems.size() ? m_ProcedureItems[row]->Routine : 0;
	return rva && Frame()->GoToVa(m_PE.GetImageBase() + rva);
}

bool CRpcView::OnRightClickList(HWND h, int row, int, POINT const& pt) const {
	if (row < 0 || h != m_Procedures || !SelectedRoutine())
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

LRESULT CRpcView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const {
	auto rva = SelectedRoutine();
	if (!rva)
		return 0;
	auto va = m_PE.GetImageBase() + rva;
	switch (id) {
		case ID_VIEW_DISASSEMBLE: Frame()->DisassembleFunction(va); break;
		case ID_EXPORT_DECOMPILE: Frame()->DecompileFunction(va); break;
		case ID_EXPORT_SIDEBYSIDE: Frame()->ShowSideBySide(va); break;
		case ID_EXPORT_XREFS: Frame()->ShowXrefs(va); break;
		case ID_EXPORT_FLOWGRAPH: Frame()->ShowFlowGraph(va); break;
		case ID_EXPORT_CALLGRAPH: Frame()->ShowCallGraph(va); break;
	}
	return 0;
}

LRESULT CRpcView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
	m_Interfaces.Create(m_Splitter, rcDefault, nullptr, style, 0, 1);
	m_Procedures.Create(m_Splitter, rcDefault, nullptr, style, 0, 2);
	for (auto list : { &m_Interfaces, &m_Procedures })
		list->SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_Interfaces);
	cm->AddColumn(L"Interface", LVCFMT_LEFT, 200);
	cm->AddColumn(L"UUID", LVCFMT_LEFT, 290);
	cm->AddColumn(L"Version", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 60);
	cm->AddColumn(L"Procedures", LVCFMT_RIGHT, 80);
	cm->AddColumn(L"Transfer Syntax", LVCFMT_LEFT, 100);
	cm->AddColumn(L"Endpoints", LVCFMT_LEFT, 220);
	cm->AddColumn(L"Flags", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);

	cm = GetColumnManager(m_Procedures);
	cm->AddColumn(L"Opnum", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Routine", LVCFMT_LEFT, 280);
	cm->AddColumn(L"Parameters", LVCFMT_LEFT, 560);
	cm->AddColumn(L"Returns", LVCFMT_LEFT, 110);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);

	m_Splitter.SetSplitterPanes(m_Interfaces, m_Procedures);
	m_Splitter.SetSplitterPosPct(40);

	for (auto const& rpc : Frame()->RpcInterfaces())
		m_InterfaceItems.push_back(&rpc);
	m_Interfaces.SetItemCountEx((int)m_InterfaceItems.size(), LVSICF_NOSCROLL);
	// (the first one that serves, selected)
	auto server = std::ranges::find_if(m_InterfaceItems, [](auto rpc) { return rpc->Server; });
	auto first = server == m_InterfaceItems.end() ? 0 : (int)(server - m_InterfaceItems.begin());
	if (!m_InterfaceItems.empty())
		m_Interfaces.SetItemState(first, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	FillProcedures();
	UpdateUI(true);
	return 0;
}
