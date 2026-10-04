#include "pch.h"
#include "GuardTableView.h"
#include "resource.h"
#include <PEFile.h>
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>

namespace {
	enum Column { Index, Rva, Va, Name, Section, Flags };
}

CGuardTableView::CGuardTableView(IMainFrame* frame, PEFile const& pe, GuardTable table) : CViewBase(frame), m_PE(pe), m_Table(std::move(table)) {
}

CString CGuardTableView::GetTitle() const {
	return GuardTableName(m_Table.Kind);
}

CString CGuardTableView::GetColumnText(HWND, int row, int col) const {
	auto const& item = m_Items[row];
	switch (col) {
		case Column::Index: return std::to_wstring(item.Index).c_str();
		case Column::Rva: return std::format(L"0x{:08X}", item.Rva).c_str();
		case Column::Va: return std::format(L"0x{:X}", m_PE.GetImageBase() + item.Rva).c_str();
		case Column::Name: return item.Name.c_str();
		case Column::Section: return item.Section >= 0 ? m_SectionNames[item.Section].c_str() : L"";
		case Column::Flags: return GuardEntryFlagsToString(item.Flags).c_str();
	}
	return CString();
}

void CGuardTableView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;
	auto asc = si->SortAscending;
	std::ranges::stable_sort(m_Items, [&](Item const& a, Item const& b) {
		switch (si->SortColumn) {
			case Column::Index: return SortHelper::Sort(a.Index, b.Index, asc);
			case Column::Rva:
			case Column::Va: return SortHelper::Sort(a.Rva, b.Rva, asc);
			case Column::Name: return SortHelper::Sort(a.Name, b.Name, asc);
			case Column::Section: return SortHelper::Sort(a.Section, b.Section, asc);
			case Column::Flags: return SortHelper::Sort(a.Flags, b.Flags, asc);
		}
		return false;
	});
}

bool CGuardTableView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	return Frame()->GoToVa(m_PE.GetImageBase() + m_Items[row].Rva);
}

void CGuardTableView::OnStateChanged(HWND, int, int, DWORD oldState, DWORD newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CGuardTableView::UpdateUI(bool first) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	if (first) {
		auto text = std::format(L"Entries: {}", m_Items.size());
		if (!m_Table.Error.empty())
			text += L" (" + m_Table.Error + L")";
		Frame()->SetStatusText(1, text.c_str());
	}
}

LRESULT CGuardTableView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"#", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 130);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 400);
	cm->AddColumn(L"Section", LVCFMT_LEFT, 80);
	cm->AddColumn(L"Flags", LVCFMT_LEFT, 250);

	CWaitCursor wait;
	auto const& sections = *m_PE.GetSecHeaders();
	for (auto const& sec : sections) {
		CString name = sec.SectionName.c_str();
		if (name.IsEmpty())
			name = CString((PCSTR)sec.SecHdr.Name, 8);
		m_SectionNames.push_back((PCWSTR)name);
	}
	m_Items.reserve(m_Table.Entries.size());
	int index = 0;
	for (auto const& e : m_Table.Entries) {
		Item item{ e, index++, Frame()->ResolveRva(e.Rva), -1 };
		for (int s = 0; s < (int)sections.size(); s++) {
			auto const& h = sections[s].SecHdr;
			if (e.Rva >= h.VirtualAddress && e.Rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData)) {
				item.Section = s;
				break;
			}
		}
		m_Items.push_back(std::move(item));
	}
	m_List.SetItemCount((int)m_Items.size());
	return 0;
}

LRESULT CGuardTableView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
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

LRESULT CGuardTableView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List));
	return 0;
}
