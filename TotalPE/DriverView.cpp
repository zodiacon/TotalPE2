#include "pch.h"
#include "DriverView.h"
#include <SortHelper.h>

CDriverView::CDriverView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CDriverView::GetTitle() const {
	return L"Driver";
}

void CDriverView::AnnotationsChanged() {
	// (what was found, and the names of the functions)
	Fill();
}

void CDriverView::Fill() {
	auto const& info = Frame()->Driver();
	m_RoutineItems.clear();
	m_IoctlItems.clear();
	int order = 0;
	auto add = [&](PCWSTR what, DWORD rva) {
		if (rva)
			m_RoutineItems.push_back({ what, rva, order });
		order++;
	};
	add(L"DriverEntry", info.DriverEntry);
	add(L"DriverUnload", info.Unload);
	add(L"DriverStartIo", info.StartIo);
	add(L"AddDevice", info.AddDevice);
	for (int i = 0; i < DriverInfo::MajorCount; i++)
		add(MajorFunctionName(i), info.Major[i]);
	// a minifilter's
	for (auto const& c : info.Callbacks)
		add(c.What.c_str(), c.Rva);
	// a framework's driver's: "EvtIoDeviceControl (WdfIoQueueCreate)"
	for (auto const& c : info.FrameworkCallbacks)
		add((c.Name + L" (" + c.Function + L")").c_str(), c.Rva);
	m_IoctlItems = info.Ioctls;
	for (auto [list, count] : { std::pair{ &m_Routines, m_RoutineItems.size() }, std::pair{ &m_Ioctls, m_IoctlItems.size() } })
		if (*list) {
			list->SetItemCountEx((int)count, LVSICF_NOSCROLL);
			Sort(*list);
			list->RedrawItems(0, list->GetItemCount());
		}
	UpdateUI(true);
}

CString CDriverView::GetColumnText(HWND h, int row, int col) const {
	auto base = m_PE.GetImageBase();
	if (h == m_Routines) {
		auto const& r = m_RoutineItems[row];
		switch (col) {
			case 0: return r.What.c_str();
			case 1: return Frame()->ResolveRva(r.Rva).c_str();
			case 2: return std::format(L"0x{:X}", base + r.Rva).c_str();
			case 3: return std::format(L"0x{:X}", r.Rva).c_str();
		}
		return CString();
	}
	auto const& i = m_IoctlItems[row];
	switch (col) {
		case 0: return std::format(L"0x{:08X}", i.Code).c_str();
		case 1: return IoctlDeviceType(i.Code).c_str();
		case 2: return std::format(L"0x{:X}", (i.Code >> 2) & 0xFFF).c_str();
		case 3: return IoctlMethod(i.Code).c_str();
		case 4: return IoctlAccess(i.Code).c_str();
		case 5: return Frame()->ResolveRva(i.Function).c_str();
	}
	return CString();
}

void CDriverView::DoSort(SortInfo const* si) {
	if (!si) {
		std::ranges::sort(m_RoutineItems, {}, &Routine::Order);
		return;
	}
	auto asc = si->SortAscending;
	if (si->hWnd == m_Routines) {
		auto compare = [&](Routine const& a, Routine const& b) {
			switch (si->SortColumn) {
				case 0: return SortHelper::Sort(a.Order, b.Order, asc);
				case 1: return SortHelper::Sort(Frame()->ResolveRva(a.Rva), Frame()->ResolveRva(b.Rva), asc);
				case 2: case 3: return SortHelper::Sort(a.Rva, b.Rva, asc);
			}
			return false;
		};
		std::stable_sort(m_RoutineItems.begin(), m_RoutineItems.end(), compare);
		return;
	}
	auto compare = [&](DriverInfo::Ioctl const& a, DriverInfo::Ioctl const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Code, b.Code, asc);
			case 1: return SortHelper::Sort(a.Code >> 16, b.Code >> 16, asc);
			case 2: return SortHelper::Sort((a.Code >> 2) & 0xFFF, (b.Code >> 2) & 0xFFF, asc);
			case 3: return SortHelper::Sort(a.Code & 3, b.Code & 3, asc);
			case 4: return SortHelper::Sort((a.Code >> 14) & 3, (b.Code >> 14) & 3, asc);
			case 5: return SortHelper::Sort(Frame()->ResolveRva(a.Function), Frame()->ResolveRva(b.Function), asc);
		}
		return false;
	};
	std::stable_sort(m_IoctlItems.begin(), m_IoctlItems.end(), compare);
}

DWORD CDriverView::SelectedFunction() const {
	auto routine = [&] {
		auto row = m_Routines.GetSelectedIndex();
		return row >= 0 && row < (int)m_RoutineItems.size() ? m_RoutineItems[row].Rva : 0;
	};
	auto ioctl = [&] {
		auto row = m_Ioctls.GetSelectedIndex();
		return row >= 0 && row < (int)m_IoctlItems.size() ? m_IoctlItems[row].Function : 0;
	};
	if (::GetFocus() == m_Ioctls)
		return ioctl();
	if (::GetFocus() == m_Routines)
		return routine();
	auto rva = routine();
	return rva ? rva : ioctl();
}

void CDriverView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) const {
	if ((newState ^ oldState) & LVIS_SELECTED)
		UpdateUI();
}

void CDriverView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	bool function = SelectedFunction() != 0;
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		ui.UIEnable(id, function);
	if (first) {
		auto const& info = Frame()->Driver();
		std::wstring text;
		switch (info.State) {
			case DriverInfo::States::None:
			case DriverInfo::States::Analyzing:
				text = L"Driver: finding its routines (after the cross-references)...";
				break;
			case DriverInfo::States::Failed:
				text = L"Driver: its routines could not be found: " + std::wstring(CA2W(info.Error.c_str()));
				break;
			default:
				text = std::format(L"Driver: {} routines, {} control codes{}{}", m_RoutineItems.size(), m_IoctlItems.size(),
					!info.Kmdf ? L"" : info.Wdf.Table ? std::format(L" (a KMDF {}.{} driver: the framework dispatches its requests to the callbacks it is given)",
						info.Wdf.Major, info.Wdf.Minor) : L" (a KMDF driver: the framework dispatches its requests)",
					info.Minifilter ? L" (a minifilter: its callbacks are registered with the filter manager, FltRegisterFilter)" : L"");
				break;
		}
		Frame()->SetStatusText(1, text.c_str());
	}
}

bool CDriverView::OnDoubleClickList(HWND h, int row, int, CPoint const&) const {
	if (row < 0)
		return false;
	auto rva = h == m_Routines ? (row < (int)m_RoutineItems.size() ? m_RoutineItems[row].Rva : 0) :
		(row < (int)m_IoctlItems.size() ? m_IoctlItems[row].Function : 0);
	return rva && Frame()->GoToVa(m_PE.GetImageBase() + rva);
}

bool CDriverView::OnRightClickList(HWND, int row, int, POINT const& pt) const {
	if (row < 0)
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

LRESULT CDriverView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const {
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

LRESULT CDriverView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
	m_Routines.Create(m_Splitter, rcDefault, nullptr, style, 0, 1);
	m_Ioctls.Create(m_Splitter, rcDefault, nullptr, style, 0, 2);
	for (auto list : { &m_Routines, &m_Ioctls })
		list->SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_Routines);
	cm->AddColumn(L"Routine", LVCFMT_LEFT, 260);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 320);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);

	cm = GetColumnManager(m_Ioctls);
	cm->AddColumn(L"Control Code", LVCFMT_LEFT, 110);
	cm->AddColumn(L"Device Type", LVCFMT_LEFT, 200);
	cm->AddColumn(L"Function", LVCFMT_RIGHT, 70);
	cm->AddColumn(L"Method", LVCFMT_LEFT, 150);
	cm->AddColumn(L"Access", LVCFMT_LEFT, 250);
	cm->AddColumn(L"Compared In", LVCFMT_LEFT, 280);

	m_Splitter.SetSplitterPanes(m_Routines, m_Ioctls);
	m_Splitter.SetSplitterPosPct(55);
	Fill();
	return 0;
}
