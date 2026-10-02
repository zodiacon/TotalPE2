#include "pch.h"
#include "HexInspector.h"

namespace {
	// reads sizeof(T) bytes at the start of the data in the requested byte order
	template<typename T>
	bool Read(HexInspectorInput const& in, T& value) {
		if (in.Count < sizeof(T))
			return false;
		uint8_t tmp[sizeof(T)];
		memcpy(tmp, in.Data, sizeof(T));
		if (in.BigEndian)
			std::reverse(tmp, tmp + sizeof(T));
		memcpy(&value, tmp, sizeof(T));
		return true;
	}

	const std::wstring Unavailable = L"\u2014";

	std::wstring Unsigned(uint64_t v) {
		return std::format(L"{} (0x{:X})", v, v);
	}

	std::wstring TimeText(tm const& t, PCWSTR suffix = L" UTC") {
		return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}{}", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
			t.tm_hour, t.tm_min, t.tm_sec, suffix);
	}

	std::wstring FileTimeText(uint64_t ft) {
		if (ft == 0)
			return L"0";
		FILETIME f{ (DWORD)ft, (DWORD)(ft >> 32) };
		SYSTEMTIME st;
		if (!::FileTimeToSystemTime(&f, &st))
			return L"(invalid)";
		return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} UTC", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	}

	std::wstring UnixTimeText(int64_t t) {
		tm tm{};
		if (t < 0 || t > 32503679999LL || _gmtime64_s(&tm, &t))	// up to the year 3000
			return L"(out of range)";
		return TimeText(tm);
	}

	std::wstring Printable(std::wstring s) {
		for (auto& c : s)
			if (c < 0x20 || c == 0x7F)
				c = L'.';
		return s;
	}

	std::wstring Utf8String(HexInspectorInput const& in) {
		size_t len = 0;
		while (len < in.Count && in.Data[len])
			len++;
		if (len == 0)
			return in.Count ? L"(empty)" : Unavailable;
		int n = ::MultiByteToWideChar(CP_UTF8, 0, (PCSTR)in.Data, (int)len, nullptr, 0);
		std::wstring s(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, (PCSTR)in.Data, (int)len, s.data(), n);
		return L"\"" + Printable(s) + (len == in.Count ? L"\u2026\"" : L"\"");
	}

	std::wstring Utf16String(HexInspectorInput const& in) {
		std::wstring s;
		size_t i = 0;
		for (; i + 1 < in.Count; i += 2) {
			wchar_t c = in.BigEndian ? (in.Data[i] << 8 | in.Data[i + 1]) : (in.Data[i + 1] << 8 | in.Data[i]);
			if (c == 0)
				break;
			s += c;
		}
		if (s.empty())
			return in.Count >= 2 ? L"(empty)" : Unavailable;
		return L"\"" + Printable(s) + (i + 1 >= in.Count ? L"\u2026\"" : L"\"");
	}
}

LRESULT CHexInspector::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	const DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
	m_Split.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	m_Values.Create(m_Split, rcDefault, nullptr, style | LVS_NOSORTHEADER, 0, IdValues);
	m_Values.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_Values.InsertColumn(0, L"Type", LVCFMT_LEFT, 110);
	m_Values.InsertColumn(1, L"Value", LVCFMT_LEFT, 200);

	m_RegionList.Create(m_Split, rcDefault, nullptr, style, 0, IdRegions);
	m_RegionList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_RegionList.InsertColumn(0, L"Region", LVCFMT_LEFT, 150);
	m_RegionList.InsertColumn(1, L"Offset", LVCFMT_RIGHT, 80);
	m_RegionList.InsertColumn(2, L"Size", LVCFMT_RIGHT, 80);

	m_Split.SetSplitterPanes(m_Values, m_RegionList);
	m_Split.SetSplitterPosPct(60);
	m_Split.SetSinglePaneMode(SPLIT_PANE_TOP);	// no regions yet
	return 0;
}

LRESULT CHexInspector::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Split)
		m_Split.MoveWindow(0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

void CHexInspector::SetRegions(std::vector<HexInspectorRegion> regions) {
	m_Regions = std::move(regions);
	m_RegionList.SetRedraw(FALSE);
	m_RegionList.DeleteAllItems();
	int i = 0;
	for (auto& r : m_Regions) {
		auto row = m_RegionList.InsertItem(i, r.Name.c_str());
		m_RegionList.SetItemData(row, i);
		m_RegionList.SetItemText(row, 1, std::format(L"0x{:X}", r.DisplayOffset).c_str());
		m_RegionList.SetItemText(row, 2, std::format(L"0x{:X}", r.Length).c_str());
		i++;
	}
	m_RegionList.SetRedraw(TRUE);
	m_Split.SetSinglePaneMode(m_Regions.empty() ? SPLIT_PANE_TOP : SPLIT_PANE_NONE);
}

void CHexInspector::Update(HexInspectorInput const& in) {
	std::vector<std::pair<PCWSTR, std::wstring>> rows;
	auto add = [&](PCWSTR name, std::wstring value) { rows.emplace_back(name, std::move(value)); };

	add(L"Offset", std::format(L"0x{:X} ({})", in.DisplayOffset, in.DisplayOffset));
	add(L"Selection", in.SelectionLength > 0 ? std::format(L"{} bytes (0x{:X})", in.SelectionLength, in.SelectionLength) : L"(none)");
	if (in.HasPE) {
		add(L"RVA", in.Rva.empty() ? L"(not mapped into the image)" : in.Rva);
		add(L"Symbol", in.Symbol.empty() ? Unavailable : in.Symbol);
		add(L"Region", in.Region.empty() ? Unavailable : in.Region);
	}

	uint8_t u8{};
	uint16_t u16{};
	uint32_t u32{};
	uint64_t u64{};
	if (Read(in, u8)) {
		std::wstring bits;
		for (int i = 7; i >= 0; i--)
			bits += (u8 >> i) & 1 ? L'1' : L'0';
		add(L"Binary", bits);
		add(L"Int8", std::to_wstring((int8_t)u8));
		add(L"UInt8", Unsigned(u8));
		add(L"ANSI char", u8 >= 0x20 && u8 < 0x7F ? std::format(L"'{}'", (wchar_t)u8) : L"(non-printable)");
	}
	else {
		for (auto name : { L"Binary", L"Int8", L"UInt8", L"ANSI char" })
			add(name, Unavailable);
	}

	if (Read(in, u16)) {
		add(L"Int16", std::to_wstring((int16_t)u16));
		add(L"UInt16", Unsigned(u16));
		add(L"UTF-16 char", u16 >= 0x20 && !(u16 >= 0xD800 && u16 < 0xE000) ? std::format(L"U+{:04X} '{}'", u16, (wchar_t)u16) : std::format(L"U+{:04X}", u16));
	}
	else {
		for (auto name : { L"Int16", L"UInt16", L"UTF-16 char" })
			add(name, Unavailable);
	}

	if (Read(in, u32)) {
		float f;
		memcpy(&f, &u32, sizeof(f));
		add(L"Int32", std::to_wstring((int32_t)u32));
		add(L"UInt32", Unsigned(u32));
		add(L"Float", std::format(L"{}", f));
		add(L"Unix time (32)", UnixTimeText((int32_t)u32));
	}
	else {
		for (auto name : { L"Int32", L"UInt32", L"Float", L"Unix time (32)" })
			add(name, Unavailable);
	}

	if (Read(in, u64)) {
		double d;
		memcpy(&d, &u64, sizeof(d));
		add(L"Int64", std::to_wstring((int64_t)u64));
		add(L"UInt64", Unsigned(u64));
		add(L"Double", std::format(L"{}", d));
		add(L"FILETIME", FileTimeText(u64));
		add(L"Unix time (64)", UnixTimeText((int64_t)u64));
	}
	else {
		for (auto name : { L"Int64", L"UInt64", L"Double", L"FILETIME", L"Unix time (64)" })
			add(name, Unavailable);
	}

	if (in.Count >= sizeof(GUID)) {
		GUID g;	// a GUID is always stored with little-endian fields
		memcpy(&g, in.Data, sizeof(g));
		add(L"GUID", std::format(L"{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}", g.Data1, g.Data2, g.Data3,
			g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]));
	}
	else {
		add(L"GUID", Unavailable);
	}
	add(L"UTF-8 string", Utf8String(in));
	add(L"UTF-16 string", Utf16String(in));

	m_Values.SetRedraw(FALSE);
	bool rebuild = m_Values.GetItemCount() != (int)rows.size();
	if (!rebuild) {
		// same row count but a different set of rows (PE rows shown/hidden): compare the labels
		CString text;
		for (size_t i = 0; i < rows.size() && !rebuild; i++) {
			m_Values.GetItemText((int)i, 0, text);
			rebuild = text != rows[i].first;
		}
	}
	if (rebuild) {
		m_Values.DeleteAllItems();
		for (size_t i = 0; i < rows.size(); i++)
			m_Values.InsertItem((int)i, rows[i].first);
	}
	for (size_t i = 0; i < rows.size(); i++)
		m_Values.SetItemText((int)i, 1, rows[i].second.c_str());
	m_Values.SetRedraw(TRUE);
}

LRESULT CHexInspector::OnRegionActivated(int, LPNMHDR, BOOL&) {
	auto sel = m_RegionList.GetSelectedIndex();
	if (sel >= 0 && OnRegionClicked) {
		auto index = (size_t)m_RegionList.GetItemData(sel);
		if (index < m_Regions.size())
			OnRegionClicked(m_Regions[index]);
	}
	return 0;
}

LRESULT CHexInspector::OnRegionCustomDraw(int, LPNMHDR hdr, BOOL&) {
	auto cd = (LPNMLVCUSTOMDRAW)hdr;
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;

		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
			// the region name cell is drawn in the same colors as the region in the hex view, which makes it the legend
			if (cd->iSubItem == 0 && (size_t)cd->nmcd.lItemlParam < m_Regions.size()) {
				auto& r = m_Regions[cd->nmcd.lItemlParam];
				cd->clrText = r.Text;
				cd->clrTextBk = r.Back;
			}
			return CDRF_NEWFONT;
	}
	return CDRF_DODEFAULT;
}
