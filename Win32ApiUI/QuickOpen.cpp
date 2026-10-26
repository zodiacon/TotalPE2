#include "pch.h"
#include "QuickOpen.h"
#include "Images.h"
#include <ListViewhelper.h>

namespace {
	constexpr int Width = 640, Height = 420, Gap = 4, MaxResults = 100;	// (the sizes at 96 DPI)

	// the first letters of the words of a name, in lowercase: "CreateFileW": "cfw", "SECURITY_ATTRIBUTES": "sa"
	std::string Initials(std::string const& name) {
		std::string initials;
		for (size_t i = 0; i < name.size(); i++) {
			auto c = (unsigned char)name[i];
			auto previous = i ? (unsigned char)name[i - 1] : '_';
			bool start = previous == '_' || (isupper(c) && !isupper(previous)) || (isdigit(c) && !isdigit(previous)) ||
				(isupper(c) && isupper(previous) && i + 1 < name.size() && islower((unsigned char)name[i + 1]));
			if (start && c != '_')
				initials += (char)tolower(c);
		}
		return initials;
	}

	// how well a name matches (higher is better); INT_MIN: it does not
	int Score(std::string const& lower, std::string const& initials, std::string const& query) {
		auto length = (int)lower.size();
		if (lower == query)
			return 100000;
		if (lower.starts_with(query))
			return 80000 - length;
		if (initials.starts_with(query))
			return 70000 - length;
		if (auto at = lower.find(query); at != std::string::npos)
			return 50000 - (int)at * 16 - length;
		if (initials.find(query) != std::string::npos)
			return 40000 - length;
		// the letters in order: the fewer the letters between them, the better
		size_t matched = 0;
		int gaps = 0, last = -1;
		for (int k = 0; k < length && matched < query.size(); k++)
			if (lower[k] == query[matched]) {
				if (last >= 0)
					gaps += k - last - 1;
				last = k;
				matched++;
			}
		return matched == query.size() ? 10000 - gaps * 8 - length : INT_MIN;
	}
}

void CQuickOpen::SetApi(Win32Api& api) {
	m_Api = &api;
	m_Lower.clear();
	m_Initials.clear();
	m_Results.clear();
	if (m_List)
		m_List.SetItemCount(0);
}

void CQuickOpen::Index() {
	if (!m_Lower.empty())
		return;
	auto const& entries = m_Api->Entries();
	m_Lower.reserve(entries.size());
	m_Initials.reserve(entries.size());
	for (auto const& e : entries) {
		std::string lower(e.Name);
		for (auto& c : lower)
			c = (char)tolower((unsigned char)c);
		m_Lower.push_back(std::move(lower));
		m_Initials.push_back(Initials(e.Name));
	}
}

void CQuickOpen::SetImageList(HIMAGELIST images) {
	m_List.SetImageList(images, LVSIL_SMALL);
}

void CQuickOpen::SetUIFont(HFONT font) {
	m_Font = font;
	m_Edit.SetFont(font);
	m_List.SetFont(font);
	if (!m_OwnFont.IsNull())
		m_OwnFont.DeleteObject();
	UpdateLayout();
}

// the default GUI font at the window's DPI (until the host gives one): it is of 96 DPI, whatever the DPI
void CQuickOpen::CreateOwnFont() {
	CFont font(DpiHelper::ScaleFont(AtlGetDefaultGuiFont(), DpiHelper::GetDpi(m_hWnd), USER_DEFAULT_SCREEN_DPI));
	m_Font = font;
	m_Edit.SetFont(font);
	m_List.SetFont(font);
	if (!m_OwnFont.IsNull())
		m_OwnFont.DeleteObject();
	m_OwnFont.Attach(font.Detach());
}

LRESULT CQuickOpen::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Edit.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
	m_Edit.SetCueBannerText(L"Go to a function, type or constant by name (CFW: CreateFileW)", TRUE);
	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
		LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
	CreateOwnFont();
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, DpiHelper::Scale(330, dpi));
	m_List.InsertColumn(1, L"Kind", LVCFMT_LEFT, DpiHelper::Scale(80, dpi));
	m_List.InsertColumn(2, L"Where", LVCFMT_LEFT, DpiHelper::Scale(200, dpi));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	return 0;
}

// (a popup: it is of the DPI of the monitor it is shown on; Show places it)
LRESULT CQuickOpen::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
	if (!m_OwnFont.IsNull())
		CreateOwnFont();
	UpdateLayout();
	return 0;
}

void CQuickOpen::UpdateLayout() {
	if (!m_Edit)
		return;
	CRect rc;
	GetClientRect(&rc);
	CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_Font);
	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	dc.SelectFont(old);
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	auto gap = DpiHelper::Scale(Gap, dpi);
	auto height = tm.tmHeight + DpiHelper::Scale(10, dpi);
	m_Edit.SetWindowPos(nullptr, gap, gap, rc.Width() - 2 * gap, height, SWP_NOZORDER | SWP_NOACTIVATE);
	auto top = height + 2 * gap;
	m_List.SetWindowPos(nullptr, gap, (int)top, rc.Width() - 2 * gap, std::max(0, (int)(rc.Height() - top - gap)), SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CQuickOpen::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateLayout();
	return 0;
}

LRESULT CQuickOpen::OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL&) {
	CDCHandle dc((HDC)wp);
	CRect rc;
	GetClientRect(&rc);
	dc.FillRect(&rc, COLOR_WINDOW);
	return 1;
}

void CQuickOpen::Show() {
	// at the top of the owner's client area, in its middle
	auto owner = GetWindow(GW_OWNER);
	CRect client;
	::GetClientRect(owner, &client);
	::MapWindowPoints(owner, nullptr, (POINT*)&client, 2);
	// (of the owner's DPI: of the monitor it is shown on)
	auto dpi = DpiHelper::GetDpi(owner);
	auto width = std::min(DpiHelper::Scale(Width, dpi), client.Width() - DpiHelper::Scale(20, dpi));
	SetWindowPos(HWND_TOP, client.left + (client.Width() - width) / 2, client.top + DpiHelper::Scale(40, dpi), width,
		DpiHelper::Scale(Height, dpi), SWP_SHOWWINDOW);
	m_Edit.SetWindowText(L"");
	m_Results.clear();
	m_List.SetItemCount(0);
	m_Edit.SetFocus();
}

LRESULT CQuickOpen::OnActivate(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	// (another window was activated: it is not wanted any more)
	if (LOWORD(wp) == WA_INACTIVE)
		ShowWindow(SW_HIDE);
	handled = FALSE;
	return 0;
}

LRESULT CQuickOpen::OnTextChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Edit) {
		handled = FALSE;
		return 0;
	}
	Search();
	return 0;
}

void CQuickOpen::Search() {
	CString text;
	m_Edit.GetWindowText(text);
	std::string query;
	for (auto c : std::wstring(text))
		if (c != L' ')
			query += (char)tolower((int)c);
	m_Results.clear();
	if (!query.empty()) {
		Index();
		std::vector<std::pair<int, size_t>> matches;
		for (size_t i = 0; i < m_Lower.size(); i++)
			if (auto score = Score(m_Lower[i], m_Initials[i], query); score != INT_MIN)
				matches.emplace_back(score, i);
		// (the best ones; of those as good, by name, as the entries are)
		auto count = std::min<size_t>(matches.size(), MaxResults);
		std::partial_sort(matches.begin(), matches.begin() + count, matches.end(), [](auto const& a, auto const& b) {
			return a.first != b.first ? a.first > b.first : a.second < b.second;
		});
		for (size_t i = 0; i < count; i++)
			m_Results.push_back(matches[i].second);
	}
	m_List.SetItemCountEx((int)m_Results.size(), 0);
	if (!m_Results.empty()) {
		m_List.SelectItem(0);
		m_List.EnsureVisible(0, FALSE);
	}
	m_List.Invalidate();
}

LRESULT CQuickOpen::OnGetDispInfo(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	auto& item = ((NMLVDISPINFO*)hdr)->item;
	if (item.iItem < 0 || item.iItem >= (int)m_Results.size())
		return 0;
	auto const& e = m_Api->Entries()[m_Results[item.iItem]];
	if (item.mask & LVIF_TEXT) {
		CString text = item.iSubItem == 0 ? CString(e.Name.c_str()) : item.iSubItem == 1 ? CString(Win32Api::KindName(e.Kind)) : CString(e.Where.c_str());
		::StringCchCopy(item.pszText, item.cchTextMax, text);
	}
	if (item.mask & LVIF_IMAGE)
		item.iImage = Images::Of(e.Kind);
	return 0;
}

LRESULT CQuickOpen::OnDoubleClick(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	Accept();
	return 0;
}

void CQuickOpen::Accept() {
	auto row = m_List.GetSelectedIndex();
	if (row < 0 || row >= (int)m_Results.size())
		return;
	auto entry = m_Results[row];
	ShowWindow(SW_HIDE);
	if (OnOpen)
		OnOpen(entry);
}

BOOL CQuickOpen::PreTranslateMessage(MSG* pMsg) {
	if (!m_hWnd || !IsWindowVisible() || pMsg->message != WM_KEYDOWN || (pMsg->hwnd != m_Edit && pMsg->hwnd != m_List))
		return FALSE;
	switch (pMsg->wParam) {
		case VK_ESCAPE:
			ShowWindow(SW_HIDE);
			return TRUE;
		case VK_RETURN:
			Accept();
			return TRUE;
		case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
			// (from the box: to the list, which moves its selection)
			if (pMsg->hwnd == m_Edit && !m_Results.empty()) {
				m_List.SendMessage(WM_KEYDOWN, pMsg->wParam, pMsg->lParam);
				return TRUE;
			}
			break;
	}
	return FALSE;
}
