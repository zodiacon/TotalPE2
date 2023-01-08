#include "pch.h"
#include "AcceleratorTableView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>

CString CAcceleratorTableView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return std::format(L"0x{:X}", item.Id).c_str();
		case 1: return std::format(L"0x{:X} ({})", item.Key, KeyFlagsToString(item.Flags)).c_str();
	}
	return CString();
}

void CAcceleratorTableView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto compare = [&](auto const& item1, auto const& item2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(item1.Id, item2.Id, si->SortAscending);
			case 1: return SortHelper::Sort(item1.Key, item2.Key, si->SortAscending);
		}
		return false;
	};
	std::ranges::sort(m_Items, compare);
}

LRESULT CAcceleratorTableView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"dummy", 0, 10);
	cm->AddColumn(L"ID", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Key", LVCFMT_LEFT, 200);
	cm->UpdateColumns();
	cm->DeleteColumn(0);

	return 0;
}

std::wstring CAcceleratorTableView::KeyFlagsToString(DWORD flags) {
	std::wstring s;
	if (flags & FVIRTKEY)
		s += L"VIRTKEY, ";
	if (flags & FSHIFT)
		s += L"SHIFT, ";
	if (flags & FCONTROL)
		s += L"CONTROL, ";
	if (flags & FALT)
		s += L"ALT, ";

	return s.empty() ? s : s.substr(0, s.length() - 2);
}

CAcceleratorTableView::CAcceleratorTableView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CAcceleratorTableView::GetTitle() const {
	return m_Title;
}

void CAcceleratorTableView::UpdateUI(bool first) {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

void CAcceleratorTableView::AddAccelTable(std::span<const std::byte> data) {
	ATLASSERT(data.size() % sizeof(Accelerator) == 0);
	auto count = data.size() / sizeof(Accelerator);
	auto start = m_Items.size();
	m_Items.resize(start + count);
	memcpy(m_Items.data() + start, data.data(), data.size());
	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CAcceleratorTableView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List, L",");
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}
