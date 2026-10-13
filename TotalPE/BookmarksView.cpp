#include "pch.h"
#include "BookmarksView.h"
#include "InputDlg.h"
#include <Annotations.h>
#include <SortHelper.h>

CBookmarksView::CBookmarksView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CBookmarksView::GetTitle() const {
	return L"Bookmarks";
}

void CBookmarksView::AnnotationsChanged() {
	Fill();
}

void CBookmarksView::Fill() {
	auto const& annotations = Frame()->GetAnnotations();
	auto base = m_PE.GetImageBase();
	m_Items.clear();
	int order = 0;
	for (auto const& b : annotations.Bookmarks) {
		Item item{ b.Rva, b.Name, Frame()->ResolveVa(base + b.Rva) };
		if (auto it = annotations.Comments.find(b.Rva); it != annotations.Comments.end())
			item.Comment = it->second;
		item.Order = order++;
		m_Items.push_back(std::move(item));
	}
	if (m_List) {
		m_List.SetItemCountEx((int)m_Items.size(), LVSICF_NOSCROLL);
		Sort(m_List);
		m_List.RedrawItems(0, m_List.GetItemCount());
	}
}

CString CBookmarksView::GetColumnText(HWND, int row, int col) const {
	auto const& item = m_Items[row];
	switch (col) {
		case 0: return item.Name.c_str();
		case 1: return std::format(L"0x{:X}", m_PE.GetImageBase() + item.Rva).c_str();
		case 2: return std::format(L"0x{:X}", item.Rva).c_str();
		case 3: return item.Function.c_str();
		case 4: return item.Comment.c_str();
	}
	return CString();
}

void CBookmarksView::DoSort(SortInfo const* si) {
	if (si == nullptr) {
		std::ranges::sort(m_Items, {}, &Item::Order);
		return;
	}
	auto asc = si->SortAscending;
	auto compare = [&](Item const& a, Item const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Name, b.Name, asc);
			case 1: case 2: return SortHelper::Sort(a.Rva, b.Rva, asc);
			case 3: return SortHelper::Sort(a.Function, b.Function, asc);
			case 4: return SortHelper::Sort(a.Comment, b.Comment, asc);
		}
		return false;
	};
	std::stable_sort(m_Items.begin(), m_Items.end(), compare);
}

int CBookmarksView::Selected() const {
	auto row = m_List.GetSelectedIndex();
	return row >= 0 && row < (int)m_Items.size() ? row : -1;
}

bool CBookmarksView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	return Frame()->GoToVa(m_PE.GetImageBase() + m_Items[row].Rva);
}

bool CBookmarksView::OnRightClickList(HWND, int row, int, CPoint const& pt) {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_BOOKMARK_RENAME, L"&Rename...\tF2");
	menu.AppendMenu(MF_STRING, ID_BOOKMARK_DELETE, L"&Delete\tDel");
	return Frame()->ShowContextMenu(menu, 0, pt.x, pt.y);
}

LRESULT CBookmarksView::OnListKeyDown(int, LPNMHDR hdr, BOOL& handled) {
	auto key = (NMLVKEYDOWN*)hdr;
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	BOOL dummy;
	switch (key->wVKey) {
		case VK_F2: OnRename(0, ID_BOOKMARK_RENAME, nullptr, dummy); break;
		case VK_DELETE: OnDelete(0, ID_BOOKMARK_DELETE, nullptr, dummy); break;
		case VK_RETURN:
			if (auto row = Selected(); row >= 0)
				OnDoubleClickList(m_List, row, 0, CPoint());
			break;
		default:
			handled = FALSE;
	}
	return 0;
}

LRESULT CBookmarksView::OnRename(WORD, WORD, HWND, BOOL&) {
	auto row = Selected();
	if (row < 0)
		return 0;
	auto text = m_Items[row].Name;
	CInputDlg dlg(L"Bookmark", L"&Name of the bookmark:", nullptr, text);
	if (dlg.DoModal() == IDOK && text != m_Items[row].Name)
		Frame()->SetBookmark(m_PE.GetImageBase() + m_Items[row].Rva, text);
	return 0;
}

LRESULT CBookmarksView::OnDelete(WORD, WORD, HWND, BOOL&) {
	if (auto row = Selected(); row >= 0)
		Frame()->RemoveBookmark(m_PE.GetImageBase() + m_Items[row].Rva);
	return 0;
}

LRESULT CBookmarksView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 220);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 260);
	cm->AddColumn(L"Comment", LVCFMT_LEFT, 360);
	Fill();
	return 0;
}
