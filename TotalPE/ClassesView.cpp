#include "pch.h"
#include "ClassesView.h"
#include <SortHelper.h>

CClassesView::CClassesView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CClassesView::GetTitle() const {
	return L"Classes";
}

void CClassesView::AnnotationsChanged() {
	// (the names of the functions are the user's)
	FillFunctions();
}

namespace {
	std::wstring Wide(std::string const& text) {
		return std::wstring(CA2W(text.c_str()));
	}

	std::wstring BasesOf(RttiClass const& c) {
		std::wstring text;
		for (auto const& b : c.Bases)
			text += (text.empty() ? L"" : L", ") + Wide(b);
		return text;
	}

	size_t FunctionCount(RttiClass const& c) {
		size_t count = 0;
		for (auto const& v : c.Vtables)
			count += v.Functions.size();
		return count;
	}
}

CString CClassesView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_Classes) {
		auto const& c = *m_Items[row];
		switch (col) {
			case 0: return Wide(c.Name).c_str();
			case 1: return c.Struct ? L"struct" : L"class";
			case 2: return BasesOf(c).c_str();
			case 3: return std::format(L"{}", FunctionCount(c)).c_str();
			case 4: return std::format(L"{}", c.Vtables.size()).c_str();
			case 5: return std::format(L"0x{:X}", c.Vtables[0].Rva).c_str();
			case 6: return Wide(c.Decorated).c_str();
		}
		return CString();
	}
	auto const& f = m_FunctionItems[row];
	switch (col) {
		case 0: return std::format(L"{}", f.Slot).c_str();
		case 1: return f.Name.c_str();
		case 2: return std::format(L"0x{:X}", m_PE.GetImageBase() + f.Rva).c_str();
		case 3: return std::format(L"0x{:X}", f.Rva).c_str();
		case 4: return f.Offset ? std::format(L"0x{:X}", f.Offset).c_str() : L"0";
		case 5: return f.Inherited ? L"Yes" : L"";
	}
	return CString();
}

void CClassesView::DoSort(SortInfo const* si) {
	if (!si)
		return;
	auto asc = si->SortAscending;
	if (si->hWnd == m_Classes) {
		auto compare = [&](RttiClass const* a, RttiClass const* b) {
			switch (si->SortColumn) {
				case 0: return SortHelper::Sort(a->Name, b->Name, asc);
				case 1: return SortHelper::Sort(a->Struct, b->Struct, asc);
				case 2: return SortHelper::Sort(BasesOf(*a), BasesOf(*b), asc);
				case 3: return SortHelper::Sort(FunctionCount(*a), FunctionCount(*b), asc);
				case 4: return SortHelper::Sort(a->Vtables.size(), b->Vtables.size(), asc);
				case 5: return SortHelper::Sort(a->Vtables[0].Rva, b->Vtables[0].Rva, asc);
				case 6: return SortHelper::Sort(a->Decorated, b->Decorated, asc);
			}
			return false;
		};
		std::stable_sort(m_Items.begin(), m_Items.end(), compare);
		return;
	}
	auto compare = [&](Function const& a, Function const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Slot, b.Slot, asc);
			case 1: return SortHelper::Sort(a.Name, b.Name, asc);
			case 2: case 3: return SortHelper::Sort(a.Rva, b.Rva, asc);
			case 4: return SortHelper::Sort(a.Offset, b.Offset, asc);
			case 5: return SortHelper::Sort(a.Inherited, b.Inherited, asc);
		}
		return false;
	};
	std::stable_sort(m_FunctionItems.begin(), m_FunctionItems.end(), compare);
}

void CClassesView::FillFunctions() {
	m_FunctionItems.clear();
	if (m_Selected) {
		auto prefix = Wide(m_Selected->Name) + L"::";
		for (auto const& v : m_Selected->Vtables)
			for (size_t i = 0; i < v.Functions.size(); i++) {
				auto name = Frame()->ResolveRva(v.Functions[i]);
				// a function whose name is not of the class (a base's, one of no class): the class does not override it
				bool inherited = !name.starts_with(prefix);
				m_FunctionItems.push_back({ (int)i, v.Functions[i], v.Offset, std::move(name), inherited });
			}
	}
	if (m_Functions) {
		m_Functions.SetItemCountEx((int)m_FunctionItems.size(), LVSICF_NOSCROLL);
		Sort(m_Functions);
		m_Functions.RedrawItems(0, m_Functions.GetItemCount());
	}
	UpdateUI();
}

void CClassesView::OnStateChanged(HWND h, int, int, UINT oldState, UINT newState) {
	if (!((newState ^ oldState) & LVIS_SELECTED))
		return;
	if (h == m_Classes) {
		auto row = m_Classes.GetSelectedIndex();
		auto selected = row >= 0 && row < (int)m_Items.size() ? m_Items[row] : nullptr;
		if (selected != m_Selected) {
			m_Selected = selected;
			FillFunctions();
		}
	}
	else
		UpdateUI();
}

void CClassesView::UpdateUI(bool) const {
	auto& ui = Frame()->GetUI();
	bool function = m_Functions && m_Functions.GetSelectedIndex() >= 0;
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		ui.UIEnable(id, function);
	Frame()->SetStatusText(1, std::format(L"Classes: {}", m_Items.size()).c_str());
}

bool CClassesView::OnDoubleClickList(HWND h, int row, int, CPoint const&) const {
	if (h == m_Classes)
		return row >= 0 && row < (int)m_Items.size() && Frame()->GoToVa(m_PE.GetImageBase() + m_Items[row]->Vtables[0].Rva);
	return row >= 0 && row < (int)m_FunctionItems.size() && Frame()->GoToVa(m_PE.GetImageBase() + m_FunctionItems[row].Rva);
}

bool CClassesView::OnRightClickList(HWND h, int row, int, POINT const& pt) const {
	if (h != m_Functions || row < 0)
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

LRESULT CClassesView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const {
	auto row = m_Functions.GetSelectedIndex();
	if (row < 0 || row >= (int)m_FunctionItems.size())
		return 0;
	auto va = m_PE.GetImageBase() + m_FunctionItems[row].Rva;
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

LRESULT CClassesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
	m_Classes.Create(m_Splitter, rcDefault, nullptr, style, 0, 1);
	m_Functions.Create(m_Splitter, rcDefault, nullptr, style, 0, 2);
	for (auto list : { &m_Classes, &m_Functions })
		list->SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_Classes);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 320);
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 60);
	cm->AddColumn(L"Bases", LVCFMT_LEFT, 360);
	cm->AddColumn(L"Virtual Functions", LVCFMT_RIGHT, 110);
	cm->AddColumn(L"Vtables", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Vtable RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Decorated Name", LVCFMT_LEFT, 300);

	cm = GetColumnManager(m_Functions);
	cm->AddColumn(L"Slot", LVCFMT_RIGHT, 50);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 420);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Vtable Offset", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Inherited", LVCFMT_LEFT, 70);

	m_Splitter.SetSplitterPanes(m_Classes, m_Functions);
	m_Splitter.SetSplitterPosPct(60);

	for (auto const& c : Frame()->Classes())
		m_Items.push_back(&c);
	m_Classes.SetItemCountEx((int)m_Items.size(), LVSICF_NOSCROLL);
	FillFunctions();
	return 0;
}
