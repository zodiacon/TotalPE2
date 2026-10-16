#include "pch.h"
#include "WinRtView.h"
#include <SortHelper.h>

CWinRtView::CWinRtView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CWinRtView::GetTitle() const {
	return L"WinRT";
}

void CWinRtView::AnnotationsChanged() {
	// (the names of the functions, of the symbols)
	m_Classes.RedrawItems(0, m_Classes.GetItemCount());
}

namespace {
	PCWSTR ActivationText(int type) {
		return type == 0 ? L"In-process" : type == 1 ? L"Out-of-process" : L"";
	}

	PCWSTR ThreadingText(int threading) {
		return threading == 0 ? L"Both" : threading == 1 ? L"STA" : threading == 2 ? L"MTA" : L"";
	}

	PCWSTR TrustText(int trust) {
		return trust == 0 ? L"Base" : trust == 1 ? L"Partial" : trust == 2 ? L"Full" : L"";
	}
}

WinRtClassMetadata const& CWinRtView::MetadataOf(WinRtClass const& c) const {
	auto it = m_Metadata.find(c.Name);
	if (it == m_Metadata.end())
		it = m_Metadata.emplace(c.Name, FindWinRtClassMetadata(c.Name)).first;
	return it->second;
}

CString CWinRtView::GetColumnText(HWND h, int row, int col) const {
	auto base = m_PE.GetImageBase();
	if (h == m_Classes) {
		auto const& c = *m_ClassItems[row];
		switch (col) {
			case 0: return c.Name.c_str();
			case 1: return c.Source.c_str();
			case 2: return ActivationText(c.ActivationType);
			case 3: return ThreadingText(c.Threading);
			case 4: return TrustText(c.TrustLevel);
			case 5: return c.Creator ? Frame()->ResolveRva(c.Creator).c_str() : L"";
			case 6: return c.Creator ? std::format(L"0x{:X}", base + c.Creator).c_str() : L"";
			case 7: return c.Entry ? std::format(L"0x{:X}", c.Entry).c_str() : L"";
			case 8: return c.Server.c_str();
		}
		return CString();
	}
	auto const& m = m_MemberItems[row];
	switch (col) {
		case 0: return m.Interface->Name.c_str();
		case 1: return m.Interface->Role.c_str();
		case 2: return m.Method ? std::to_wstring(m.Method->Slot).c_str() : L"";
		case 3: return m.Method ? m.Method->Declaration.c_str() : m.Interface->Name.find(L'<') != std::wstring::npos ? L"(generic)" : L"(not in the metadata)";
		case 4: return m.Interface->Guid.c_str();
	}
	return CString();
}

void CWinRtView::DoSort(SortInfo const* si) {
	if (!si)
		return;
	auto asc = si->SortAscending;
	if (si->hWnd == m_Classes) {
		auto compare = [&](WinRtClass const* a, WinRtClass const* b) {
			switch (si->SortColumn) {
				case 0: return SortHelper::Sort(a->Name, b->Name, asc);
				case 1: return SortHelper::Sort(a->Source, b->Source, asc);
				case 2: return SortHelper::Sort(a->ActivationType, b->ActivationType, asc);
				case 3: return SortHelper::Sort(a->Threading, b->Threading, asc);
				case 4: return SortHelper::Sort(a->TrustLevel, b->TrustLevel, asc);
				case 5: return SortHelper::Sort(Frame()->ResolveRva(a->Creator), Frame()->ResolveRva(b->Creator), asc);
				case 6: return SortHelper::Sort(a->Creator, b->Creator, asc);
				case 7: return SortHelper::Sort(a->Entry, b->Entry, asc);
				case 8: return SortHelper::Sort(a->Server, b->Server, asc);
			}
			return false;
		};
		std::stable_sort(m_ClassItems.begin(), m_ClassItems.end(), compare);
		return;
	}
	auto compare = [&](Member const& a, Member const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Interface->Name, b.Interface->Name, asc);
			case 1: return SortHelper::Sort(a.Interface->Role, b.Interface->Role, asc);
			case 2: return SortHelper::Sort(a.Method ? a.Method->Slot : 0, b.Method ? b.Method->Slot : 0, asc);
			case 3: return SortHelper::Sort(a.Method ? a.Method->Declaration : L"", b.Method ? b.Method->Declaration : L"", asc);
			case 4: return SortHelper::Sort(a.Interface->Guid, b.Interface->Guid, asc);
		}
		return false;
	};
	std::stable_sort(m_MemberItems.begin(), m_MemberItems.end(), compare);
}

void CWinRtView::FillMembers() {
	m_MemberItems.clear();
	m_Selected = SelectedClass();
	if (m_Selected) {
		CWaitCursor wait;
		for (auto const& i : MetadataOf(*m_Selected).Interfaces) {
			if (i.Methods.empty())
				m_MemberItems.push_back({ &i, nullptr });
			for (auto const& m : i.Methods)
				m_MemberItems.push_back({ &i, &m });
		}
	}
	m_Members.SetItemCountEx((int)m_MemberItems.size(), LVSICF_NOSCROLL);
	Sort(m_Members);
	m_Members.RedrawItems(0, m_Members.GetItemCount());
	if (m_Selected) {
		auto const& md = MetadataOf(*m_Selected);
		Frame()->SetStatusText(2, md.File.empty() ? L"Not in the metadata of Windows" :
			std::format(L"{}{}", md.File, md.Activatable ? L"; activatable (ActivateInstance)" : L"").c_str());
	}
}

WinRtClass const* CWinRtView::SelectedClass() const {
	auto row = m_Classes.GetSelectedIndex();
	return row >= 0 && row < (int)m_ClassItems.size() ? m_ClassItems[row] : nullptr;
}

void CWinRtView::OnStateChanged(HWND h, int, int, UINT oldState, UINT newState) {
	if (!((newState ^ oldState) & LVIS_SELECTED))
		return;
	if (h == m_Classes && SelectedClass() != m_Selected)
		FillMembers();
	UpdateUI();
}

void CWinRtView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	auto c = SelectedClass();
	bool function = c && c->Creator;
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		ui.UIEnable(id, function);
	if (first) {
		auto const& all = Frame()->WinRtClasses();
		auto creator = std::ranges::count_if(all, [](auto const& c) { return c.Entry != 0; });
		Frame()->SetStatusText(1, std::format(L"WinRT: {} runtime classes ({} in WRL's creator map, {} of the registry only)", all.size(),
			creator, std::ranges::count_if(all, [](auto const& c) { return c.Entry == 0; })).c_str());
	}
}

bool CWinRtView::OnDoubleClickList(HWND h, int row, int, CPoint const&) const {
	if (row < 0 || h != m_Classes || row >= (int)m_ClassItems.size())
		return false;
	auto const& c = *m_ClassItems[row];
	auto rva = c.Creator ? c.Creator : c.Entry;
	return rva && Frame()->GoToVa(m_PE.GetImageBase() + rva);
}

bool CWinRtView::OnRightClickList(HWND h, int row, int, POINT const& pt) const {
	auto c = SelectedClass();
	if (row < 0 || h != m_Classes || !c || !c->Creator)
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

LRESULT CWinRtView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const {
	auto c = SelectedClass();
	if (!c || !c->Creator)
		return 0;
	auto va = m_PE.GetImageBase() + c->Creator;
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

LRESULT CWinRtView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
	m_Classes.Create(m_Splitter, rcDefault, nullptr, style, 0, 1);
	m_Members.Create(m_Splitter, rcDefault, nullptr, style, 0, 2);
	for (auto list : { &m_Classes, &m_Members })
		list->SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_Classes);
	cm->AddColumn(L"Runtime Class", LVCFMT_LEFT, 380);
	cm->AddColumn(L"Found In", LVCFMT_LEFT, 170);
	cm->AddColumn(L"Activation", LVCFMT_LEFT, 100);
	cm->AddColumn(L"Threading", LVCFMT_LEFT, 70);
	cm->AddColumn(L"Trust", LVCFMT_LEFT, 60);
	cm->AddColumn(L"Factory Creator", LVCFMT_LEFT, 260);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"Entry RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Server", LVCFMT_LEFT, 300);

	cm = GetColumnManager(m_Members);
	cm->AddColumn(L"Interface", LVCFMT_LEFT, 340);
	cm->AddColumn(L"Role", LVCFMT_LEFT, 120);
	cm->AddColumn(L"Slot", LVCFMT_RIGHT, 50);
	cm->AddColumn(L"Method", LVCFMT_LEFT, 640);
	cm->AddColumn(L"IID", LVCFMT_LEFT, 290);

	m_Splitter.SetSplitterPanes(m_Classes, m_Members);
	m_Splitter.SetSplitterPosPct(45);

	for (auto const& c : Frame()->WinRtClasses())
		m_ClassItems.push_back(&c);
	m_Classes.SetItemCountEx((int)m_ClassItems.size(), LVSICF_NOSCROLL);
	if (!m_ClassItems.empty())
		m_Classes.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	FillMembers();
	UpdateUI(true);
	return 0;
}
