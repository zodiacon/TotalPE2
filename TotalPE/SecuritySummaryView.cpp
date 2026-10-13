#include "pch.h"
#include "SecuritySummaryView.h"
#include <PEFile.h>
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>

namespace {
	enum Column { Feature, Status, Details };
}

CString CSecuritySummaryView::GetTitle() const {
	return L"Security Summary";
}

CString CSecuritySummaryView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case Column::Feature: return item.Name.c_str();
		case Column::Status: return SecurityStateToString(item.State);
		case Column::Details: return item.Details.c_str();
	}
	return CString();
}

void CSecuritySummaryView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;
	auto asc = si->SortAscending;
	std::stable_sort(m_Items.begin(), m_Items.end(), [&](auto& a, auto& b) {
		switch (si->SortColumn) {
			case Column::Feature: return SortHelper::Sort(a.Name, b.Name, asc);
			case Column::Status: return SortHelper::Sort((int)a.State, (int)b.State, asc);
			case Column::Details: return SortHelper::Sort(a.Details, b.Details, asc);
		}
		return false;
	});
}

bool CSecuritySummaryView::OnRightClickList(HWND, int row, int, POINT const& pt) const {
	if (row < 0)
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	return Frame()->ShowContextMenu(menu, 0, pt.x, pt.y);
}

void CSecuritySummaryView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CSecuritySummaryView::UpdateUI(bool) {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	auto on = std::count_if(m_Items.begin(), m_Items.end(), [](auto& f) { return f.State == SecurityState::Enabled; });
	auto off = std::count_if(m_Items.begin(), m_Items.end(), [](auto& f) { return f.State == SecurityState::Disabled || f.State == SecurityState::Partial; });
	Frame()->SetStatusText(1, std::format(L"{} protections on, {} off or partial", on, off).c_str());
}

LRESULT CSecuritySummaryView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Feature", LVCFMT_LEFT, 180);
	cm->AddColumn(L"Status", LVCFMT_LEFT, 100);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 800);

	{
		CWaitCursor wait;	// the signature is checked by the system
		m_Items = AnalyzeSecurity(m_PE, true);
	}
	m_List.SetItemCount((int)m_Items.size());
	return 0;
}

// the status in a color: on, partly, off
LRESULT CSecuritySummaryView::OnCustomDraw(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	auto cd = (NMLVCUSTOMDRAW*)hdr;
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;
		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		{
			auto row = (int)cd->nmcd.dwItemSpec;
			// the other columns in the default color (the list keeps the color of the previous column)
			cd->clrText = CLR_DEFAULT;
			if (cd->iSubItem != Column::Status || row < 0 || row >= (int)m_Items.size())
				return CDRF_NEWFONT;
			auto dark = WTLHelper::IsDarkMode();
			switch (m_Items[row].State) {
				case SecurityState::Enabled: cd->clrText = dark ? RGB(100, 220, 100) : RGB(0, 128, 0); break;
				case SecurityState::Partial: cd->clrText = dark ? RGB(255, 190, 80) : RGB(200, 110, 0); break;
				case SecurityState::Disabled: cd->clrText = dark ? RGB(255, 100, 100) : RGB(200, 0, 0); break;
				default: break;
			}
			return CDRF_NEWFONT;
		}
	}
	return CDRF_DODEFAULT;
}

// the selected rows, a line each
LRESULT CSecuritySummaryView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	std::wstring text;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED)) {
		auto& item = m_Items[i];
		text += std::format(L"{}\t{}\t{}\r\n", item.Name, SecurityStateToString(item.State), item.Details);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
