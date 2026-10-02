#include "pch.h"
#include "AnomalyView.h"
#include <SortHelper.h>

CAnomalyView::CAnomalyView(IMainFrame* frame, std::vector<Anomaly> const& anomalies) : CViewBase(frame), m_Items(anomalies) {
}

CString CAnomalyView::GetTitle() const {
	return L"Anomalies";
}

CString CAnomalyView::GetColumnText(HWND, int row, int col) const {
	auto& a = m_Items[row];
	switch (col) {
		case 0: return AnomalySeverityToString(a.Severity);
		case 1: return a.Category.c_str();
		case 2: return a.Message.c_str();
		case 3: return a.FileOffset >= 0 ? std::format(L"0x{:X}", a.FileOffset).c_str() : L"";
	}
	return CString();
}

void CAnomalyView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	auto compare = [&](Anomaly const& a, Anomaly const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort((int)a.Severity, (int)b.Severity, asc);
			case 1: return SortHelper::Sort(a.Category, b.Category, asc);
			case 2: return SortHelper::Sort(a.Message, b.Message, asc);
			case 3: return SortHelper::Sort(a.FileOffset, b.FileOffset, asc);
		}
		return false;
	};
	std::stable_sort(m_Items.begin(), m_Items.end(), compare);
}

bool CAnomalyView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size() || m_Items[row].FileOffset < 0)
		return false;
	return Frame()->GoToFileOffset(m_Items[row].FileOffset, false);
}

LRESULT CAnomalyView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Severity", LVCFMT_LEFT, 80);
	cm->AddColumn(L"Category", LVCFMT_LEFT, 110);
	cm->AddColumn(L"Description", LVCFMT_LEFT, 700);
	cm->AddColumn(L"Offset", LVCFMT_RIGHT, 100);

	m_List.SetItemCount((int)m_Items.size());
	return 0;
}
