#include "pch.h"
#include "FontView.h"
#include "ResourceContent.h"
#include <deque>
#include <DpiHelper.h>

namespace {
	const int SampleSizes[] = { 9, 12, 16, 20, 28, 36, 48, 72 };
	const wchar_t Sample[] = L"The quick brown fox jumps over the lazy dog. 0123456789";
	const wchar_t Characters[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 .,;:!?\"'()[]{}<>@#$%&*+-=/\\";
	const int Margin = 12;		// at 96 DPI
}

CFontView::CFontView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CFontView::~CFontView() {
	if (m_hFont)
		::RemoveFontMemResourceEx(m_hFont);
}

CString CFontView::GetTitle() const {
	return m_Title;
}

bool CFontView::SetData(std::span<const std::byte> data) {
	m_FaceName = GetFontFaceName(data);
	// the font is private to the process, and is gone when the view is
	m_hFont = ::AddFontMemResourceEx((PVOID)data.data(), (DWORD)data.size(), nullptr, &m_FontCount);
	if (!m_hFont)
		m_Message = L"The font cannot be loaded";
	else if (m_FaceName.empty())
		m_Message = L"The name of the font is not known: it cannot be shown";

	CClientDC dc(m_hWnd);
	SetScrollSize(Render(dc.m_hDC, false));
	Invalidate();
	return m_hFont && !m_FaceName.empty();
}

LRESULT CFontView::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& handled) {
	CClientDC dc(m_hWnd);
	SetScrollSize(Render(dc.m_hDC, false));
	Invalidate();
	handled = FALSE;
	return 0;
}

CSize CFontView::Render(CDCHandle dc, bool draw) {
	// the sizes are at the DPI of the view
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	auto scale = [&](int n) { return DpiHelper::Scale(n, dpi); };
	int margin = scale(Margin), y = margin, width = 0;
	auto line = [&](HFONT hFont, PCWSTR text) {
		auto old = dc.SelectFont(hFont);
		CSize size;
		dc.GetTextExtent(text, -1, &size);
		if (draw)
			dc.TextOut(margin, y, text);
		dc.SelectFont(old);
		y += size.cy + scale(4);
		width = std::max(width, (int)size.cx);
	};
	std::deque<CFont> fonts;	// deleted at the end; a deque does not move its elements
	auto makeFont = [&](PCWSTR face, int points, int weight = FW_NORMAL) -> HFONT {
		auto& font = fonts.emplace_back();
		font.CreateFont(-::MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
		return font;
	};

	auto ui = makeFont(L"Segoe UI", 11);
	auto uiBold = makeFont(L"Segoe UI", 14, FW_BOLD);
	line(uiBold, m_FaceName.empty() ? L"(Unknown Font)" : m_FaceName.c_str());
	if (m_FontCount > 1)
		line(ui, std::format(L"{} fonts in the resource", m_FontCount).c_str());
	if (!m_Message.empty()) {
		line(ui, m_Message.c_str());
		return CSize(width + margin * 2, y + margin);
	}
	y += scale(8);

	auto chars = makeFont(m_FaceName.c_str(), 20);
	line(chars, Characters);
	y += scale(12);
	for (auto points : SampleSizes) {
		line(ui, std::format(L"{} pt", points).c_str());
		auto sample = makeFont(m_FaceName.c_str(), points);
		line(sample, Sample);
		y += scale(6);
	}
	return CSize(width + margin * 2, y + margin);
}

void CFontView::DoPaint(CDCHandle dc) {
	CRect rc;
	GetClientRect(&rc);
	rc.right = std::max<LONG>(rc.right, m_sizeAll.cx);
	rc.bottom = std::max<LONG>(rc.bottom, m_sizeAll.cy);
	dc.FillSolidRect(&rc, ::GetSysColor(COLOR_WINDOW));
	dc.SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
	dc.SetBkMode(TRANSPARENT);
	Render(dc, true);
}
