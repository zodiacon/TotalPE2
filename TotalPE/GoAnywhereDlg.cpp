#include "pch.h"
#include "GoAnywhereDlg.h"

namespace {
	enum Column { Name, Kind, Details };
	constexpr UINT_PTR TimerUpdate = 1;
}

CString CGoAnywhereDlg::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Results[row];
	switch (col) {
		case Column::Name: return item.Name.c_str();
		case Column::Kind: return SearchIndex::KindName(item.Kind);
		case Column::Details:
			if (item.Location.empty())
				return item.Details.c_str();
			return item.Details.empty() ? item.Location.c_str() : (item.Location + L"  " + item.Details).c_str();
	}
	return CString();
}

int CGoAnywhereDlg::GetRowImage(HWND, int row, int) const {
	return m_Results[row].Image;
}

bool CGoAnywhereDlg::OnDoubleClickList(HWND, int row, int, CPoint const&) {
	if (row < 0 || row >= (int)m_Results.size())
		return false;
	m_Selected = m_Results[row];
	EndDialog(IDOK);
	return true;
}

LRESULT CGoAnywhereDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	m_Edit.SubclassWindow(GetDlgItem(IDC_GOANY_TEXT));
	m_Edit.SetCueBannerText(L"An address, a view, a function, an import, a type, a string...");
	m_Status.Attach(GetDlgItem(IDC_GOANY_STATUS));
	m_List.Attach(GetDlgItem(IDC_GOANY_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(m_Host->GetImageList(), LVSIL_SMALL);	// the tree's (LVS_SHAREIMAGELISTS)

	// near the top of the window it goes over, half as wide
	CRect owner, rc;
	GetParent().GetWindowRect(&owner);
	GetWindowRect(&rc);
	auto width = std::max(rc.Width(), owner.Width() / 2);
	SetWindowPos(nullptr, owner.left + (owner.Width() - width) / 2, owner.top + 80, width, rc.Height(), SWP_NOZORDER);
	// the controls follow the width
	CRect client, edit, list, status;
	GetClientRect(&client);
	auto resize = [&](HWND h, CRect& r) {
		::GetWindowRect(h, &r);
		ScreenToClient(&r);
		::SetWindowPos(h, nullptr, 0, 0, client.Width() - 2 * r.left, r.Height(), SWP_NOMOVE | SWP_NOZORDER);
	};
	resize(m_Edit, edit);
	resize(m_List, list);
	resize(m_Status, status);

	auto cx = client.Width() - 2 * list.left - ::GetSystemMetrics(SM_CXVSCROLL) - 4;
	m_List.InsertColumn(Column::Name, L"Name", LVCFMT_LEFT, cx * 45 / 100);
	m_List.InsertColumn(Column::Kind, L"Kind", LVCFMT_LEFT, cx * 13 / 100);
	m_List.InsertColumn(Column::Details, L"Details", LVCFMT_LEFT, cx - cx * 45 / 100 - cx * 13 / 100);

	{
		CWaitCursor wait;	// the index is built the first time
		m_Host->GetSearchIndex();
	}
	m_Status.SetWindowText(std::format(L"{} items", m_Host->GetSearchIndex().Size()).c_str());
	m_Edit.SetFocus();
	return FALSE;	// the focus is set
}

LRESULT CGoAnywhereDlg::OnTextChanged(WORD, WORD, HWND, BOOL&) {
	// after a pause in the typing: the index can be large
	SetTimer(TimerUpdate, 100);
	return 0;
}

LRESULT CGoAnywhereDlg::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
	if (id != TimerUpdate) {
		handled = FALSE;
		return 0;
	}
	KillTimer(TimerUpdate);
	Update();
	return 0;
}

void CGoAnywhereDlg::Update() {
	CString text;
	m_Edit.GetWindowText(text);
	text.Trim();
	m_Results.clear();
	if (!text.IsEmpty()) {
		m_Results = m_Host->AddressItems((PCWSTR)text);
		auto& index = m_Host->GetSearchIndex();
		auto kinds = AllSearchKinds & ~SearchKindBit(SearchKind::Address);
		for (auto i : index.FindBest((PCWSTR)text, kinds, MaxResults))
			m_Results.push_back(index[i]);
	}
	m_List.SetItemCountEx((int)m_Results.size(), 0);
	if (!m_Results.empty()) {
		m_List.SelectItem(0);
		m_List.EnsureVisible(0, FALSE);
	}
	m_Status.SetWindowText(text.IsEmpty() ? L"" : m_Results.empty() ? L"Nothing matches" :
		m_Results.size() >= MaxResults ? std::format(L"The first {} matches", m_Results.size()).c_str() :
		std::format(L"{} matches", m_Results.size()).c_str());
}

// Enter: the selected result (the text that was typed last is searched first)
LRESULT CGoAnywhereDlg::OnOK(WORD, WORD, HWND, BOOL&) {
	if (KillTimer(TimerUpdate))
		Update();
	auto row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row < 0 && !m_Results.empty())
		row = 0;
	if (row < 0) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	m_Selected = m_Results[row];
	EndDialog(IDOK);
	return 0;
}

LRESULT CGoAnywhereDlg::OnCancel(WORD, WORD, HWND, BOOL&) {
	EndDialog(IDCANCEL);
	return 0;
}

LRESULT CGoAnywhereDlg::OnEditKeyDown(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
	switch (wp) {
		case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
			if (KillTimer(TimerUpdate))
				Update();
			m_List.SendMessage(msg, wp, lp);
			return 0;
	}
	handled = FALSE;
	return 0;
}
