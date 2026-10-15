#include "pch.h"
#include "ComView.h"
#include <SortHelper.h>
#include <set>

CComView::CComView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CComView::GetTitle() const {
	return L"COM";
}

void CComView::XrefsChanged() {
	Fill();
}

void CComView::AnnotationsChanged() {
	// (the names of the functions are the user's)
	if (m_List)
		m_List.RedrawItems(0, m_List.GetItemCount());
}

void CComView::Fill() {
	m_Items = Frame()->ComUses();
	m_ClassItems = Frame()->ComServerClasses();
	for (auto [list, count] : { std::pair{ &m_List, m_Items.size() }, std::pair{ &m_Classes, m_ClassItems.size() } })
		if (*list) {
			list->SetItemCountEx((int)count, LVSICF_NOSCROLL);
			Sort(*list);
			list->RedrawItems(0, list->GetItemCount());
		}
	// (the classes are there for a server only)
	if (m_Splitter)
		m_Splitter.SetSinglePaneMode(m_ClassItems.empty() ? SPLIT_PANE_BOTTOM : SPLIT_PANE_NONE);
	UpdateUI(true);
}

DWORD CComView::SelectedFunction() const {
	auto ofClass = [&] {
		auto row = m_Classes.GetSelectedIndex();
		return row >= 0 && row < (int)m_ClassItems.size() ? m_ClassItems[row].Creator : 0;
	};
	auto ofCall = [&] {
		auto row = m_List.GetSelectedIndex();
		return row >= 0 && row < (int)m_Items.size() ? m_Items[row].Function : 0;
	};
	if (::GetFocus() == m_Classes)
		return ofClass();
	if (::GetFocus() == m_List)
		return ofCall();
	auto rva = ofCall();
	return rva ? rva : ofClass();
}

std::wstring CComView::Names(std::vector<DWORD> const& guids) const {
	std::wstring text;
	for (auto rva : guids) {
		auto name = Frame()->ResolveRva(rva);
		text += (text.empty() ? L"" : L", ") + (name.empty() ? std::format(L"0x{:X}", m_PE.GetImageBase() + rva) : name);
	}
	return text;
}

CString CComView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_Classes) {
		auto const& c = m_ClassItems[row];
		uint8_t bytes[16]{};
		auto offset = m_PE.GetOffsetFromRVA(c.Guid);
		if (offset && offset + 16 <= m_PE.GetFileSize())
			memcpy(bytes, m_PE.GetSpan((uint32_t)offset, 16).data(), 16);
		switch (col) {
			case 0: return GuidText(bytes).c_str();
			case 1: return Frame()->ResolveRva(c.Guid).c_str();
			case 2: return c.Registered.c_str();
			case 3:
			{
				if (!c.Creator)
					return L"";
				auto name = Frame()->ShortFunctionName(c.Creator);
				return name.empty() ? std::format(L"0x{:X}", m_PE.GetImageBase() + c.Creator).c_str() : name.c_str();
			}
			case 4: return c.Source.c_str();
			case 5: return c.Server.c_str();
			case 6: return std::format(L"0x{:X}", m_PE.GetImageBase() + c.Guid).c_str();
		}
		return CString();
	}
	auto const& u = m_Items[row];
	switch (col) {
		case 0:
		{
			auto name = Frame()->ShortFunctionName(u.Function);
			return name.empty() ? std::format(L"0x{:X}", m_PE.GetImageBase() + u.Function).c_str() : name.c_str();
		}
		case 1: return std::format(L"0x{:X}", u.Call).c_str();
		case 2: return u.Callee.c_str();
		case 3: return Names(u.Classes).c_str();
		case 4: return Names(u.Interfaces).c_str();
		case 5: return Names(u.Others).c_str();
	}
	return CString();
}

void CComView::DoSort(SortInfo const* si) {
	if (!si)
		return;
	auto asc = si->SortAscending;
	if (si->hWnd == m_Classes) {
		auto compare = [&](ComServerClass const& a, ComServerClass const& b) {
			switch (si->SortColumn) {
				case 0: case 6: return SortHelper::Sort(a.Guid, b.Guid, asc);
				case 1: return SortHelper::Sort(Frame()->ResolveRva(a.Guid), Frame()->ResolveRva(b.Guid), asc);
				case 2: return SortHelper::Sort(a.Registered, b.Registered, asc);
				case 3: return SortHelper::Sort(Frame()->ShortFunctionName(a.Creator), Frame()->ShortFunctionName(b.Creator), asc);
				case 4: return SortHelper::Sort(a.Source, b.Source, asc);
				case 5: return SortHelper::Sort(a.Server, b.Server, asc);
			}
			return false;
		};
		std::stable_sort(m_ClassItems.begin(), m_ClassItems.end(), compare);
		return;
	}
	auto compare = [&](ComUse const& a, ComUse const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(Frame()->ShortFunctionName(a.Function), Frame()->ShortFunctionName(b.Function), asc);
			case 1: return SortHelper::Sort(a.Call, b.Call, asc);
			case 2: return SortHelper::Sort(a.Callee, b.Callee, asc);
			case 3: return SortHelper::Sort(Names(a.Classes), Names(b.Classes), asc);
			case 4: return SortHelper::Sort(Names(a.Interfaces), Names(b.Interfaces), asc);
			case 5: return SortHelper::Sort(Names(a.Others), Names(b.Others), asc);
		}
		return false;
	};
	std::stable_sort(m_Items.begin(), m_Items.end(), compare);
}

void CComView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) const {
	if ((newState ^ oldState) & LVIS_SELECTED)
		UpdateUI();
}

void CComView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	bool selected = m_List && SelectedFunction() != 0;
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		ui.UIEnable(id, selected);
	if (first) {
		std::wstring text;
		if (!Frame()->XrefsReady())
			text = L"COM: finding the calls (after the cross-references)...";
		else {
			std::set<DWORD> classes, interfaces;
			for (auto const& u : m_Items) {
				classes.insert(u.Classes.begin(), u.Classes.end());
				interfaces.insert(u.Interfaces.begin(), u.Interfaces.end());
			}
			text = std::format(L"COM: {} calls, {} classes, {} interfaces", m_Items.size(), classes.size(), interfaces.size());
			if (!m_ClassItems.empty())
				text = std::format(L"COM: it serves {} classes; ", m_ClassItems.size()) + text.substr(5);
		}
		Frame()->SetStatusText(1, text.c_str());
	}
}

bool CComView::OnDoubleClickList(HWND h, int row, int, CPoint const&) const {
	if (h == m_Classes)
		return row >= 0 && row < (int)m_ClassItems.size() && Frame()->GoToVa(m_PE.GetImageBase() + m_ClassItems[row].Guid);
	return row >= 0 && row < (int)m_Items.size() && Frame()->GoToVa(m_Items[row].Call);
}

bool CComView::OnRightClickList(HWND, int row, int, POINT const& pt) const {
	if (row < 0)
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

LRESULT CComView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const {
	auto rva = SelectedFunction();
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

LRESULT CComView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
	m_Classes.Create(m_Splitter, rcDefault, nullptr, style, 0, 1);
	m_List.Create(m_Splitter, rcDefault, nullptr, style, 0, 2);
	for (auto list : { &m_Classes, &m_List })
		list->SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cc = GetColumnManager(m_Classes);
	cc->AddColumn(L"CLSID", LVCFMT_LEFT, 290);
	cc->AddColumn(L"Name", LVCFMT_LEFT, 240);
	cc->AddColumn(L"Registered As", LVCFMT_LEFT, 220);
	cc->AddColumn(L"Made By", LVCFMT_LEFT, 280);
	cc->AddColumn(L"Found In", LVCFMT_LEFT, 130);
	cc->AddColumn(L"Server (Registry)", LVCFMT_LEFT, 300);
	cc->AddColumn(L"Address", LVCFMT_RIGHT, 140);
	m_Splitter.SetSplitterPanes(m_Classes, m_List);
	m_Splitter.SetSplitterPosPct(40);
	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 280);
	cm->AddColumn(L"Call", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"Calls", LVCFMT_LEFT, 200);
	cm->AddColumn(L"Class", LVCFMT_LEFT, 260);
	cm->AddColumn(L"Interface", LVCFMT_LEFT, 300);
	cm->AddColumn(L"Other GUIDs", LVCFMT_LEFT, 240);
	Fill();
	return 0;
}
