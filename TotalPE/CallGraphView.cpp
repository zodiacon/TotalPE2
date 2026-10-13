#include "pch.h"
#include "CallGraphView.h"
#include "resource.h"
#include <PEFile.h>
#include <WTLHelper.h>

using namespace NodeGraphCtrl;

namespace {
	// the size of the text of the nodes (Consolas, 12 units), and the space around and between the nodes
	constexpr float CharWidth = 6.7f;
	constexpr float LineHeight = 14.5f;
	constexpr float PadX = 8.0f * 2 + 4.0f;
	constexpr float PadY = 6.0f * 2 + 2.0f;
	constexpr float GapX = 30.0f;
	constexpr float GapY = 90.0f;
	constexpr size_t MaxName = 60;
	constexpr size_t PerRow = 6;		// the nodes of a row of callers or callees
	constexpr size_t MaxNodes = 60;		// of each side: the most called (or calling) ones

	constexpr COLORREF FillColor = RGB(34, 36, 42);
	constexpr COLORREF TextColor = RGB(222, 224, 228);
	constexpr COLORREF FunctionBorder = RGB(90, 160, 235);
	constexpr COLORREF CallerBorder = RGB(70, 200, 95);
	constexpr COLORREF CalleeBorder = RGB(95, 100, 112);
	constexpr COLORREF ImportBorder = RGB(225, 165, 60);
	constexpr COLORREF EdgeColor = RGB(125, 150, 205);

	// "ns::C::F(int, char*)": the name without its signature
	std::wstring WithoutSignature(std::wstring name) {
		if (!name.empty() && name.back() == L')') {
			int depth = 0;
			for (size_t i = name.size(); i-- > 0; ) {
				if (name[i] == L')')
					depth++;
				else if (name[i] == L'(' && --depth == 0) {
					if (i > 0)
						name.resize(i);
					break;
				}
			}
		}
		return name;
	}
}

CCallGraphView::CCallGraphView(IMainFrame* frame, PEFile const& pe, uint64_t function, PCWSTR title) :
	CViewBase(frame), m_PE(pe), m_Title(title), m_Function(function) {
}

CString CCallGraphView::GetTitle() const {
	return m_Title;
}

std::wstring CCallGraphView::NameOf(CallGraphNode const& node) const {
	auto name = Frame()->ResolveVa(node.Address);
	if (name.empty() || (!node.Import && name.find(L"+0x") != std::wstring::npos))
		name = std::format(L"sub_{:X}", node.Address);
	name = WithoutSignature(name);
	if (name.size() > MaxName)
		name = name.substr(0, MaxName - 3) + L"...";
	return name;
}

bool CCallGraphView::Build() {
	m_Calls = BuildCallGraph(m_PE, Frame()->GetXrefs(), m_Function);
	auto model = m_Graph.GetModel();
	model->Clear();
	m_Nodes.clear();

	auto addNode = [&](CallGraphNode const& f, std::wstring const& detail, COLORREF border, float x, float y) {
		auto text = NameOf(f) + L"\n" + detail;
		auto width = (float)std::max(NameOf(f).size(), detail.size()) * CharWidth + PadX;
		auto id = model->AddNode(text, x, y, width, 2 * LineHeight + PadY);
		auto node = model->GetNode(id);
		node->Style.Code = true;
		node->Style.CornerRadius = 4.0f;
		node->Style.FillColor = FillColor;
		node->Style.TextColor = TextColor;
		node->Style.BorderColor = border;
		node->Tooltip = f.Import ? std::format(L"An import (its entry in the import address table is at 0x{:X})", f.Address) :
			std::format(L"0x{:X}\nDouble-click to see its call graph; right-click for more", f.Address);
		m_Nodes.push_back({ id, f });
		return id;
	};
	auto plural = [](size_t n, PCWSTR what) { return std::format(L"{} {}{}", n, what, n == 1 ? L"" : L"s"); };

	// rows of nodes, centered, from 'y' upwards (callers) or downwards (callees); the row widths are those of the widest names
	auto rowWidth = [&](std::vector<CallGraphNode> const& nodes, size_t first, size_t count) {
		float width = 0;
		for (size_t i = first; i < first + count; i++)
			width += (float)std::max(NameOf(nodes[i]).size(), (size_t)12) * CharWidth + PadX + GapX;
		return width - GapX;
	};
	auto place = [&](std::vector<CallGraphNode> const& nodes, bool callers, std::vector<NodeId>& ids) {
		auto count = std::min(nodes.size(), MaxNodes);
		for (size_t first = 0; first < count; first += PerRow) {
			auto inRow = std::min(PerRow, count - first);
			auto row = (float)(first / PerRow + 1);
			auto y = callers ? -row * GapY : row * GapY;
			auto x = -rowWidth(nodes, first, inRow) / 2;
			for (size_t i = first; i < first + inRow; i++) {
				auto const& f = nodes[i];
				auto width = (float)std::max(NameOf(f).size(), (size_t)12) * CharWidth + PadX;
				auto detail = plural(f.Sites, f.TailCall ? L"jump" : L"call");
				if (f.Import)
					detail += L", import";
				ids.push_back(addNode(f, detail, f.Import ? ImportBorder : callers ? CallerBorder : CalleeBorder, x + width / 2, y));
				x += width + GapX;
			}
		}
	};

	CallGraphNode self{ m_Function };
	auto center = addNode(self, std::format(L"{}, {}", plural(m_Calls.Callers.size(), L"caller"), plural(m_Calls.Callees.size(), L"callee")),
		FunctionBorder, 0, 0);
	model->GetNode(center)->Style.BorderWidth = 2.5f;

	std::vector<NodeId> callers, callees;
	place(m_Calls.Callers, true, callers);
	place(m_Calls.Callees, false, callees);
	auto addEdge = [&](NodeId from, NodeId to, CallGraphNode const& f) {
		auto id = model->AddEdge(from, to);
		auto edge = model->GetEdge(id);
		edge->Style.Color = EdgeColor;
		edge->Style.Width = 1.5f + std::min<float>((float)f.Sites, 5.0f) * 0.3f;
		edge->Tooltip = std::format(L"{} {}{}", f.Sites, f.TailCall ? L"jump" : L"call", f.Sites == 1 ? L"" : L"s");
	};
	for (size_t i = 0; i < callers.size(); i++)
		addEdge(callers[i], center, m_Calls.Callers[i]);
	for (size_t i = 0; i < callees.size(); i++)
		addEdge(center, callees[i], m_Calls.Callees[i]);

	// the coordinates of the model are not negative
	float left = FLT_MAX, top = FLT_MAX;
	for (auto const& n : model->Nodes()) {
		left = std::min(left, n.X - n.Width / 2);
		top = std::min(top, n.Y - n.Height / 2);
	}
	for (auto const& n : m_Nodes)
		if (auto node = model->GetNode(n.Id)) {
			node->X += 20 - left;
			node->Y += 20 - top;
		}

	m_Status = std::format(L"{}, {}{}{}", plural(m_Calls.Callers.size(), L"caller"), plural(m_Calls.Callees.size(), L"callee"),
		m_Calls.Callers.size() > MaxNodes || m_Calls.Callees.size() > MaxNodes ? std::format(L" (the {} with the most calls are shown)", MaxNodes) : L"",
		m_Calls.Truncated ? L" (the function is bigger than what was followed)" : L"").c_str();
	m_Shown = false;
	ShowAll();
	UpdateUI();
	return true;
}

void CCallGraphView::ShowAll() {
	if (m_Shown || m_Nodes.empty() || !m_Graph.m_hWnd)
		return;
	CRect rc;
	m_Graph.GetClientRect(&rc);
	if (rc.Width() < 50 || rc.Height() < 50)
		return;
	m_Shown = true;
	m_Graph.FitInView();
	if (m_Graph.GetZoom() > 1.0f) {
		// a small graph: at its size, in the middle
		auto center = m_Graph.GetModel()->GetNode(m_Nodes[0].Id);
		m_Graph.CenterOn(center->X, center->Y, 1.0f);
	}
}

void CCallGraphView::UpdateUI(bool) const {
	Frame()->GetUI().UIEnable(ID_ICON_EXPORT, !m_Nodes.empty());
	if (!m_Status.IsEmpty())
		Frame()->SetStatusText(1, m_Status);
}

CCallGraphView::Node const* CCallGraphView::NodeOf(NodeId id) const {
	auto it = std::ranges::find_if(m_Nodes, [&](auto const& n) { return n.Id == id; });
	return it == m_Nodes.end() ? nullptr : &*it;
}

LRESULT CCallGraphView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	m_Graph.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, NGCS_GRID | NGCS_READONLY);
	m_hWndClient = m_Graph;
	m_Graph.SetMinimapVisible(true);
	handled = FALSE;	// the base class registers the view for idle processing and messages
	return 0;
}

LRESULT CCallGraphView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL& handled) {
	if (m_Graph.m_hWnd && LOWORD(lParam) > 0 && HIWORD(lParam) > 0) {
		m_Graph.MoveWindow(0, 0, LOWORD(lParam), HIWORD(lParam));
		ShowAll();
	}
	handled = FALSE;
	return 0;
}

// On a function: what can be done with it (the function of the graph elsewhere); the graph can be saved
LRESULT CCallGraphView::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1)
		::GetCursorPos(&pt);
	CPoint client(pt);
	m_Graph.ScreenToClient(&client);
	auto node = NodeOf(m_Graph.NodeFromPoint(client));
	m_Target = node && !node->Function.Import ? node->Function.Address : m_Function;
	UpdateUI();

	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CMenuHandle functions = menu.GetSubMenu(3);
	functions.DeleteMenu(ID_EDIT_COPY, MF_BYCOMMAND);
	functions.AppendMenu(MF_STRING, ID_ICON_EXPORT, L"&Export as Image (SVG)...");
	if (m_Target == m_Function)
		functions.DeleteMenu(ID_EXPORT_CALLGRAPH, MF_BYCOMMAND);	// this one
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		Frame()->GetUI().UIEnable(id, true);
	return Frame()->ShowContextMenu(functions, 0, pt.x, pt.y);
}

LRESULT CCallGraphView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) {
	auto va = m_Target ? m_Target : m_Function;
	switch (id) {
		case ID_VIEW_DISASSEMBLE: Frame()->DisassembleFunction(va); break;
		case ID_EXPORT_DECOMPILE: Frame()->DecompileFunction(va); break;
		case ID_EXPORT_SIDEBYSIDE: Frame()->ShowSideBySide(va); break;
		case ID_EXPORT_XREFS: Frame()->ShowXrefs(va); break;
		case ID_EXPORT_FLOWGRAPH: Frame()->ShowFlowGraph(va); break;
		case ID_EXPORT_CALLGRAPH: Frame()->ShowCallGraph(va); break;
	}
	m_Target = 0;
	return 0;
}

// The graph as it is drawn, in a file that a browser or a vector editor opens
LRESULT CCallGraphView::OnExport(WORD, WORD, HWND, BOOL&) {
	CString name = m_Title;
	name.Replace(L':', L'-');
	for (PCWSTR bad = L"\\/*?\"<>|"; *bad; bad++)
		name.Remove(*bad);
	CSimpleFileDialog dlg(FALSE, L"svg", name + L".svg", OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_PATHMUSTEXIST,
		L"SVG Image (*.svg)\0*.svg\0All Files\0*.*\0\0", m_hWnd);
	SuspendResumeHook sr;
	if (dlg.DoModal() != IDOK)
		return 0;
	if (!m_Graph.GetModel()->SaveSvg(dlg.m_szFileName))
		AtlMessageBox(m_hWnd, L"The graph could not be saved.", IDR_MAINFRAME, MB_ICONERROR);
	return 0;
}

// Another function's call graph (the function's own: its disassembly)
LRESULT CCallGraphView::OnNodeDoubleClick(int, LPNMHDR pnmh, BOOL& handled) {
	if (pnmh->hwndFrom != m_Graph.m_hWnd)
		return 0;
	handled = TRUE;
	auto node = NodeOf(((NODEGRAPHNOTIFY*)pnmh)->NodeId);
	if (!node || node->Function.Import)
		return 0;
	if (node->Function.Address == m_Function)
		Frame()->DisassembleFunction(m_Function);
	else
		Frame()->ShowCallGraph(node->Function.Address);
	return 0;
}
