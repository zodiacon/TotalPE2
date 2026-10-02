#include "pch.h"
#include "FlowGraphView.h"
#include "resource.h"
#include <PEFile.h>
#include <unordered_set>
#include <WTLHelper.h>

using namespace NodeGraphCtrl;

namespace {
	// the size of the text of the nodes (Consolas, 12 units): what a character and a line take, and the space around the text
	constexpr float CharWidth = 6.7f;
	constexpr float LineHeight = 14.5f;
	constexpr float PadX = 8.0f * 2 + 4.0f;
	constexpr float PadY = 6.0f * 2 + 2.0f;
	constexpr size_t MaxComment = 60;	// the longest name shown after an instruction: undecorated C++ names can be very long

	constexpr COLORREF TrueColor = RGB(70, 200, 95);
	constexpr COLORREF FalseColor = RGB(235, 75, 75);
	constexpr COLORREF PlainColor = RGB(125, 150, 205);
	constexpr COLORREF CaseColor = RGB(225, 165, 60);

	// "name+0x1A": an address inside of something, not its start
	bool HasOffset(std::wstring const& name) {
		auto plus = name.rfind(L"+0x");
		return plus != std::wstring::npos && plus + 3 < name.size() &&
			std::all_of(name.begin() + plus + 3, name.end(), [](wchar_t c) { return iswxdigit(c) != 0; });
	}

	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	PCWSTR EndingToString(FlowEnd end) {
		switch (end) {
			case FlowEnd::Fallthrough: return L"continues in the next block";
			case FlowEnd::Jump: return L"jumps to another block";
			case FlowEnd::ConditionalJump: return L"conditional jump";
			case FlowEnd::Switch: return L"jump through a table (switch)";
			case FlowEnd::Return: return L"returns";
			case FlowEnd::TailCall: return L"jumps to another function";
			case FlowEnd::IndirectJump: return L"indirect jump, the target is not known";
			case FlowEnd::Trap: return L"does not return";
			case FlowEnd::Invalid: return L"continues in something that is not code";
		}
		return L"";
	}

	PCWSTR EdgeDescription(FlowEdgeKind kind) {
		switch (kind) {
			case FlowEdgeKind::True: return L"True: the jump is taken";
			case FlowEdgeKind::False: return L"False: the jump is not taken, the code goes on with the next instruction";
			case FlowEdgeKind::Case: return L"A case of the switch";
			default: return L"The code goes on here";
		}
	}

	COLORREF EdgeColor(FlowEdgeKind kind) {
		switch (kind) {
			case FlowEdgeKind::True: return TrueColor;
			case FlowEdgeKind::False: return FalseColor;
			case FlowEdgeKind::Case: return CaseColor;
		}
		return PlainColor;
	}
}

CFlowGraphView::CFlowGraphView(IMainFrame* frame, PEFile const& pe, uint64_t function, PCWSTR title) :
	CViewBase(frame), m_PE(pe), m_Title(title), m_Function(function) {
}

CString CFlowGraphView::GetTitle() const {
	return m_Title;
}

// "address  mnemonic operands  ; name": the name is that of what a call, a memory operand or a jump out of the function refers to
std::wstring CFlowGraphView::BlockText(FlowBlock const& block, std::unordered_set<uint64_t> const& starts) const {
	std::wstring text;
	// the name of the function, or of whatever else the block starts with
	if (auto name = Frame()->ResolveVa(block.Start); !name.empty() && !HasOffset(name)) {
		if (name.size() > MaxComment)
			name = name.substr(0, MaxComment - 3) + L"...";
		text = name + L":";
	}
	for (auto const& i : block.Code) {
		if (!text.empty())
			text += L'\n';
		text += std::format(L"{:X}  {:<7} {}", i.Address, Widen(i.Mnemonic), Widen(i.Operands));

		auto target = i.Branch ? i.Branch : i.Memory;
		if (!target)
			continue;
		if (i.Branch && i.Mnemonic != "call" && starts.contains(*i.Branch))
			continue;	// a jump inside the function: the edge shows where it goes
		if (auto name = Frame()->ResolveVa(*target); !name.empty()) {
			if (name.size() > MaxComment)
				name = name.substr(0, MaxComment - 3) + L"...";
			text += L"   ; " + name;
		}
	}
	return text;
}

bool CFlowGraphView::Build() {
	m_FlowGraph = BuildFlowGraph(m_PE, Frame()->GetXrefs(), m_Function);
	if (m_FlowGraph.Blocks.empty())
		return false;

	auto model = m_Graph.GetModel();
	model->Clear();
	m_Nodes.clear();

	std::unordered_set<uint64_t> starts;
	for (auto const& block : m_FlowGraph.Blocks)
		starts.insert(block.Start);

	std::vector<FlowSize> sizes;
	std::vector<std::wstring> texts;
	for (auto const& block : m_FlowGraph.Blocks) {
		auto text = BlockText(block, starts);
		size_t lines = 1, longest = 0, current = 0;
		for (auto c : text) {
			if (c == L'\n') {
				lines++;
				current = 0;
			}
			else {
				longest = std::max(longest, ++current);
			}
		}
		sizes.push_back({ longest * CharWidth + PadX, lines * LineHeight + PadY });
		texts.push_back(std::move(text));
	}
	auto layout = LayoutFlowGraph(m_FlowGraph, sizes);
	auto const& positions = layout.Blocks;

	for (size_t i = 0; i < m_FlowGraph.Blocks.size(); i++) {
		auto const& block = m_FlowGraph.Blocks[i];
		auto id = model->AddNode(texts[i], positions[i].X, positions[i].Y, sizes[i].Width, sizes[i].Height);
		m_Nodes.push_back(id);

		auto node = model->GetNode(id);
		node->Style.Code = true;
		node->Style.CornerRadius = 4.0f;
		node->Style.FillColor = RGB(34, 36, 42);
		node->Style.BorderColor = RGB(95, 100, 112);
		node->Style.TextColor = RGB(222, 224, 228);
		if (i == m_FlowGraph.EntryBlock) {
			node->Style.BorderColor = RGB(90, 160, 235);
			node->Style.BorderWidth = 2.5f;
		}
		node->Tooltip = std::format(L"Block 0x{:X} - 0x{:X}: {} instruction{}, {}\nDouble-click to see it in the disassembly",
			block.Start, block.End, block.Code.size(), block.Code.size() == 1 ? L"" : L"s", EndingToString(block.Ending));
	}

	for (size_t i = 0; i < m_FlowGraph.Edges.size(); i++) {
		auto const& e = m_FlowGraph.Edges[i];
		auto id = model->AddEdge(m_Nodes[e.From], m_Nodes[e.To]);
		auto edge = model->GetEdge(id);
		edge->Style.Color = EdgeColor(e.Kind);
		edge->Style.Width = 2.0f;
		edge->Tooltip = EdgeDescription(e.Kind);
		for (auto const& bend : layout.Edges[i])
			edge->Waypoints.push_back({ bend.X, bend.Y });
	}

	size_t branches = std::count_if(m_FlowGraph.Blocks.begin(), m_FlowGraph.Blocks.end(), [](FlowBlock const& b) { return b.Ending == FlowEnd::ConditionalJump; });
	size_t instructions = 0;
	for (auto const& b : m_FlowGraph.Blocks)
		instructions += b.Code.size();
	m_Status = std::format(L"{} blocks, {} instructions, {} conditional jumps{}",
		m_FlowGraph.Blocks.size(), instructions, branches, m_FlowGraph.Truncated ? L" (the function is bigger than what is shown)" : L"").c_str();

	m_Shown = false;
	ShowStart();
	UpdateUI();
	return true;
}

// The whole graph if it is big enough to read, otherwise the entry block at its top.
void CFlowGraphView::ShowStart() {
	if (m_Shown || m_Nodes.empty() || !m_Graph.m_hWnd)
		return;
	CRect rc;
	m_Graph.GetClientRect(&rc);
	if (rc.Width() < 50 || rc.Height() < 50)
		return;
	m_Shown = true;

	m_Graph.FitInView();
	if (m_Graph.GetZoom() > 1.0f) {
		// a small graph: at its size and in the middle, not blown up
		float left = FLT_MAX, top = FLT_MAX, right = -FLT_MAX, bottom = -FLT_MAX;
		for (auto const& n : m_Graph.GetModel()->Nodes()) {
			left = std::min(left, n.X - n.Width * 0.5f);
			top = std::min(top, n.Y - n.Height * 0.5f);
			right = std::max(right, n.X + n.Width * 0.5f);
			bottom = std::max(bottom, n.Y + n.Height * 0.5f);
		}
		m_Graph.CenterOn((left + right) * 0.5f, (top + bottom) * 0.5f, 1.0f);
		return;
	}
	const float zoom = 0.8f;
	if (m_Graph.GetZoom() >= zoom)
		return;

	auto node = m_Graph.GetModel()->GetNode(m_Nodes[m_FlowGraph.EntryBlock]);
	m_Graph.CenterOn(node->X, node->Y - node->Height * 0.5f + (rc.Height() * 0.5f - 40.0f) / zoom, zoom);
}

void CFlowGraphView::UpdateUI(bool) const {
	Frame()->GetUI().UIEnable(ID_ICON_EXPORT, !m_Nodes.empty());
	if (!m_Status.IsEmpty())
		Frame()->SetStatusText(1, m_Status);
}

LRESULT CFlowGraphView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	m_Graph.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, NGCS_GRID | NGCS_READONLY);
	m_hWndClient = m_Graph;
	m_Graph.SetMinimapVisible(true);
	handled = FALSE;	// the base class registers the view for idle processing and messages
	return 0;
}

LRESULT CFlowGraphView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL& handled) {
	if (m_Graph.m_hWnd && LOWORD(lParam) > 0 && HIWORD(lParam) > 0) {
		m_Graph.MoveWindow(0, 0, LOWORD(lParam), HIWORD(lParam));
		ShowStart();
	}
	handled = FALSE;
	return 0;
}

LRESULT CFlowGraphView::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1)
		::GetCursorPos(&pt);
	UpdateUI();		// the command is enabled by what the view knows now: the view may have been shown before it had a graph
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(9), 0, pt.x, pt.y);
}

// The graph as it is drawn, in a file that a browser or a vector editor opens
LRESULT CFlowGraphView::OnExport(WORD, WORD, HWND, BOOL&) {
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

LRESULT CFlowGraphView::OnNodeDoubleClick(int, LPNMHDR pnmh, BOOL& handled) {
	if (pnmh->hwndFrom != m_Graph.m_hWnd)
		return 0;
	auto id = ((NODEGRAPHNOTIFY*)pnmh)->NodeId;
	auto it = std::find(m_Nodes.begin(), m_Nodes.end(), id);
	if (it != m_Nodes.end())
		Frame()->GoToVa(m_FlowGraph.Blocks[it - m_Nodes.begin()].Start);
	handled = TRUE;
	return 0;
}
