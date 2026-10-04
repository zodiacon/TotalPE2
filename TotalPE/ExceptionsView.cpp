#include "pch.h"
#include "ExceptionsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>
#include "PEStrings.h"

namespace {
	enum Column { Begin, End, UnwindAddress, Function, Prolog, Codes, Frame, Flags, Handler };
}

CExceptionsView::CExceptionsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CExceptionsView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_Details) {
		auto const& d = m_DetailItems[row];
		switch (col) {
			case 0: return d.Offset.c_str();
			case 1: return d.Operation.c_str();
			case 2: return d.Details.c_str();
		}
		return CString();
	}

	auto& item = m_Items[row];
	auto const& u = item.Unwind;
	bool decoded = m_IsX64 && u.Valid();
	switch (col) {
		case Column::Begin: return std::format(L"0x{:08X}", item.RuntimeFuncEntry.BeginAddress).c_str();
		case Column::End: return std::format(L"0x{:08X}", item.RuntimeFuncEntry.EndAddress).c_str();
		case Column::UnwindAddress: return std::format(L"0x{:08X}", item.RuntimeFuncEntry.UnwindInfoAddress).c_str();
		case Column::Function: return item.Disp ? std::format(L"{} + 0x{:X}", item.UndecoratedName, item.Disp).c_str() : item.UndecoratedName.c_str();
		case Column::Prolog: return decoded ? std::format(L"0x{:X}", u.SizeOfProlog).c_str() : L"";
		case Column::Codes: return decoded ? std::to_wstring(u.Codes.size()).c_str() : L"";
		case Column::Frame: return decoded && u.FrameRegister ? X64RegisterName(u.FrameRegister) : L"";
		case Column::Flags: return decoded ? UnwindFlagsToString(u.Flags).c_str() : m_IsX64 ? u.Error.c_str() : L"";
		case Column::Handler:
			if (!decoded || !u.HasHandler())
				return L"";
			return item.HandlerName.empty() ? std::format(L"0x{:X}", u.HandlerRva).c_str() : item.HandlerName.c_str();
	}
	return CString();
}

bool CExceptionsView::IsSortable(HWND h, int) const {
	return h == m_List;
}

void CExceptionsView::DoSort(SortInfo const* si) {
	if (si == nullptr || si->hWnd != m_List)
		return;

	auto asc = si->SortAscending;
	auto compare = [&](Exception const& item1, Exception const& item2) {
		auto const& u1 = item1.Unwind;
		auto const& u2 = item2.Unwind;
		switch (si->SortColumn) {
			case Column::Begin: return SortHelper::Sort(item1.RuntimeFuncEntry.BeginAddress, item2.RuntimeFuncEntry.BeginAddress, asc);
			case Column::End: return SortHelper::Sort(item1.RuntimeFuncEntry.EndAddress, item2.RuntimeFuncEntry.EndAddress, asc);
			case Column::UnwindAddress: return SortHelper::Sort(item1.RuntimeFuncEntry.UnwindInfoAddress, item2.RuntimeFuncEntry.UnwindInfoAddress, asc);
			case Column::Function: return SortHelper::Sort(item1.UndecoratedName, item2.UndecoratedName, asc);
			case Column::Prolog: return SortHelper::Sort(u1.SizeOfProlog, u2.SizeOfProlog, asc);
			case Column::Codes: return SortHelper::Sort(u1.Codes.size(), u2.Codes.size(), asc);
			case Column::Frame: return SortHelper::Sort(u1.FrameRegister, u2.FrameRegister, asc);
			case Column::Flags: return SortHelper::Sort(u1.Flags, u2.Flags, asc);
			case Column::Handler: return SortHelper::Sort(item1.HandlerName, item2.HandlerName, asc);
		}
		return false;
	};
	std::ranges::stable_sort(m_Items, compare);
}

void CExceptionsView::OnStateChanged(HWND h, int, int, DWORD oldState, DWORD newState) {
	if (!(newState & LVIS_SELECTED) && !(oldState & LVIS_SELECTED))
		return;
	if (h == m_List)
		BuildDetails();
	UpdateUI();
}

// the function in the upper list; a code of its prolog, its handler or the function it continues in the lower one
bool CExceptionsView::OnDoubleClickList(HWND h, int row, int, CPoint const&) const {
	if (row < 0)
		return false;
	uint32_t rva;
	if (h == m_Details) {
		if (row >= (int)m_DetailItems.size() || m_DetailItems[row].Rva == 0)
			return false;
		rva = m_DetailItems[row].Rva;
	}
	else {
		if (row >= (int)m_Items.size())
			return false;
		rva = m_Items[row].RuntimeFuncEntry.BeginAddress;
	}
	return Frame()->GoToVa(m_PE.GetImageBase() + rva);
}

void CExceptionsView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	auto focus = ::GetFocus();
	auto const& list = focus == m_Details.m_hWnd ? m_Details : m_List;
	ui.UIEnable(ID_EDIT_COPY, list.GetSelectedCount() > 0);
	if (first)
		Frame()->SetStatusText(1, std::format(L"Functions: {}", m_Items.size()).c_str());
}

CString CExceptionsView::GetTitle() const {
	return L"Exceptions";
}

void CExceptionsView::BuildItems() {
	CWaitCursor wait;
	// the unwind information of other machines (ARM64, for one) has a different layout
	m_IsX64 = m_PE.GetNTHeader()->NTHdr64.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;

	m_Items.reserve(m_PE.GetExceptions()->size());
	auto& symbols = Frame()->GetSymbols();
	for (auto const& ex : *m_PE.GetExceptions()) {
		Exception e{ ex };
		if (symbols) {
			auto sym = symbols.GetSymbolByRVA(ex.RuntimeFuncEntry.BeginAddress, SymbolTag::Null, &e.Disp);
			if (sym) {
				e.FuncName = sym.Name();
				if (!e.FuncName.empty()) {
					e.UndecoratedName = sym.UndecoratedName();
					if (e.UndecoratedName.empty())
						e.UndecoratedName = PEStrings::UndecorateName(e.FuncName.c_str());
				}
			}
			else
				e.Disp = 0;
		}
		if (m_IsX64) {
			e.Unwind = DecodeUnwindInfo(m_PE, ex.RuntimeFuncEntry.UnwindInfoAddress);
			if (e.Unwind.HasHandler())
				e.HandlerName = Frame()->ResolveRva(e.Unwind.HandlerRva);
		}
		m_Items.emplace_back(std::move(e));
	}
	m_List.SetItemCount((int)m_Items.size());
}

void CExceptionsView::BuildDetails() {
	m_DetailItems.clear();
	if (m_List.GetSelectedCount() == 1) {
		auto const& item = m_Items[m_List.GetNextItem(-1, LVNI_SELECTED)];
		auto const& u = item.Unwind;
		auto begin = item.RuntimeFuncEntry.BeginAddress;
		if (!m_IsX64)
			m_DetailItems.push_back({ L"", L"", L"The unwind information of this machine is not decoded (x64 only)" });
		else {
			m_DetailItems.push_back({ L"", L"Version", std::to_wstring(u.Version) });
			if (!u.Valid())
				m_DetailItems.push_back({ L"", L"Error", u.Error });
			if (u.FrameRegister)
				m_DetailItems.push_back({ L"", L"Frame", std::format(L"{} = rsp + 0x{:X}", X64RegisterName(u.FrameRegister), u.FrameOffset * 16) });
			// the codes are stored from the end of the prolog to its start
			for (auto const& code : u.Codes)
				m_DetailItems.push_back({ std::format(L"+0x{:02X}", code.CodeOffset), code.Operation, code.Details, code.Op == 6 ? 0 : begin + code.CodeOffset });
			if (u.HasHandler()) {
				m_DetailItems.push_back({ L"", L"Handler", item.HandlerName.empty() ? std::format(L"0x{:X}", u.HandlerRva) :
					std::format(L"{} (0x{:X})", item.HandlerName, u.HandlerRva), u.HandlerRva });
				m_DetailItems.push_back({ L"", L"Handler Data", std::format(L"0x{:X}", u.HandlerDataRva) });
			}
			if (u.ChainedBegin) {
				auto name = Frame()->ResolveRva(*u.ChainedBegin);
				m_DetailItems.push_back({ L"", L"Chained To", std::format(L"0x{:X} - 0x{:X}{} (unwind info at 0x{:X})", *u.ChainedBegin, *u.ChainedEnd,
					name.empty() ? L"" : L" " + name, *u.ChainedUnwindInfo), *u.ChainedBegin });
			}
		}
	}
	m_Details.SetItemCountEx((int)m_DetailItems.size(), 0);
	m_Details.Invalidate();
}

LRESULT CExceptionsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Dummy", 0, 10);
	cm->AddColumn(L"Begin Address", LVCFMT_RIGHT, 110);
	cm->AddColumn(L"End Address", LVCFMT_RIGHT, 110);
	cm->AddColumn(L"Unwind Info", LVCFMT_RIGHT, 110);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 320);
	cm->AddColumn(L"Prolog", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Codes", LVCFMT_RIGHT, 50);
	cm->AddColumn(L"Frame", LVCFMT_LEFT, 50);
	cm->AddColumn(L"Flags", LVCFMT_LEFT, 140);
	cm->AddColumn(L"Handler", LVCFMT_LEFT, 250);
	cm->UpdateColumns();
	cm->DeleteColumn(0);

	m_Details.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_Details.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	cm = GetColumnManager(m_Details);
	cm->AddColumn(L"Prolog Offset", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Operation", LVCFMT_LEFT, 140);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 500);

	m_Splitter.SetSplitterPanes(m_List, m_Details);
	m_Splitter.SetSplitterPosPct(70);

	BuildItems();

	return 0;
}

LRESULT CExceptionsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto const& list = ::GetFocus() == m_Details.m_hWnd ? m_Details : m_List;
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(list));
	return 0;
}

LRESULT CExceptionsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr || m_List.GetItemCount() == 0)
		return 0;
	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());

	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else {
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	}
	return 0;
}
