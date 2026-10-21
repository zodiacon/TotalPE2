#include "pch.h"
#include "CapabilitiesView.h"
#include <PEFile.h>
#include <SortHelper.h>

namespace {
	std::wstring Wide(std::string const& s) {
		if (s.empty())
			return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	std::wstring Join(std::vector<std::string> const& items, PCWSTR separator) {
		std::wstring text;
		for (auto const& i : items)
			text += (text.empty() ? L"" : separator) + Wide(i);
		return text;
	}
}

CCapabilitiesView::CCapabilitiesView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame) {
	CWaitCursor wait;
	for (auto const& m : CapabilityRules::BuiltIn().Match(pe))
		m_Rows.push_back({ Wide(m.Name), Wide(m.Namespace), Join(m.Attack, L", "), Join(m.Evidence, L"; "), m_Rows.size() });
}

CString CCapabilitiesView::GetTitle() const {
	return L"Capabilities";
}

CString CCapabilitiesView::GetColumnText(HWND, int row, int col) const {
	auto& r = m_Rows[row];
	switch (col) {
		case 0: return r.Name.c_str();
		case 1: return r.Namespace.c_str();
		case 2: return r.Attack.c_str();
		case 3: return r.Evidence.c_str();
	}
	return CString();
}

void CCapabilitiesView::DoSort(SortInfo const* si) {
	if (si == nullptr) {
		std::ranges::sort(m_Rows, {}, &Row::Index);
		return;
	}
	auto asc = si->SortAscending;
	auto compare = [&](Row const& a, Row const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Name, b.Name, asc);
			case 1: return SortHelper::Sort(a.Namespace, b.Namespace, asc);
			case 2: return SortHelper::Sort(a.Attack, b.Attack, asc);
			case 3: return SortHelper::Sort(a.Evidence, b.Evidence, asc);
		}
		return false;
	};
	std::stable_sort(m_Rows.begin(), m_Rows.end(), compare);
}

LRESULT CCapabilitiesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Capability", LVCFMT_LEFT, 280);
	cm->AddColumn(L"Namespace", LVCFMT_LEFT, 250);
	cm->AddColumn(L"ATT&&CK", LVCFMT_LEFT, 90);	// (& alone would underline the C)
	cm->AddColumn(L"Evidence", LVCFMT_LEFT, 700);
	cm->UpdateColumns();

	m_List.SetItemCount((int)m_Rows.size());
	return 0;
}
