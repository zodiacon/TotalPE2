#include "pch.h"
#include "TabStrip.h"
#include <WTLHelper.h>
#include <DpiHelper.h>

namespace {
	// (at 96 DPI)
	constexpr int PadX = 10;
	constexpr int ImageSize = 16, ImageGap = 5;
	constexpr int CloseSize = 16, CloseGap = 6;
	constexpr int MinWidth = 70, MaxWidth = 260;
	constexpr int AccentHeight = 2;
	constexpr int ListWidth = 22;

	struct Colors {
		COLORREF Strip, Tab, Selected, Hot, Text, SelectedText, Accent, Border, CloseHot;
	};

	Colors CurrentColors() {
		if (WTLHelper::IsDarkMode())
			return { RGB(37, 37, 38), RGB(45, 45, 48), RGB(30, 30, 30), RGB(62, 62, 64), RGB(170, 170, 170), RGB(255, 255, 255),
				RGB(0, 122, 204), RGB(63, 63, 70), RGB(80, 80, 84) };
		return { RGB(238, 238, 242), RGB(228, 228, 232), RGB(255, 255, 255), RGB(212, 212, 218), RGB(70, 70, 70), RGB(0, 0, 0),
			RGB(0, 122, 204), RGB(204, 206, 219), RGB(196, 196, 204) };
	}
}

void CTabStrip::SetClosable(bool closable) {
	m_Closable = closable;
	Layout();
}

void CTabStrip::SetListButton(bool show) {
	m_ListButton = show;
	Layout();
}

int CTabStrip::Scale(int value) const {
	return DpiHelper::Scale(value, m_hWnd);
}

int CTabStrip::ImageWidth() const {
	int cx = 0, cy;
	return m_Images && ::ImageList_GetIconSize(m_Images, &cx, &cy) ? cx : 0;
}

int CTabStrip::TabsWidth() const {
	CRect rc;
	GetClientRect(&rc);
	return std::max(0, rc.Width() - (m_ListButton ? Scale(ListWidth) : 0));
}

CRect CTabStrip::ListButtonRect() const {
	CRect rc;
	GetClientRect(&rc);
	return m_ListButton ? CRect(rc.right - Scale(ListWidth), rc.top, rc.right, rc.bottom) : CRect();
}

// a menu of the tabs: the one chosen selected
void CTabStrip::ShowList() {
	if (m_Tabs.empty())
		return;
	CMenu menu;
	menu.CreatePopupMenu();
	for (int i = 0; i < (int)m_Tabs.size(); i++)
		menu.AppendMenu(MF_STRING | (i == m_Selected ? MF_CHECKED : 0), i + 1, m_Tabs[i].Text.c_str());
	auto button = ListButtonRect();
	CPoint pt(button.right, button.bottom);
	ClientToScreen(&pt);
	auto chosen = (int)menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTALIGN, pt.x, pt.y, m_hWnd) - 1;
	if (chosen >= 0 && chosen != m_Selected) {
		Select(chosen);
		if (OnSelected)
			OnSelected(chosen);
	}
}

void CTabStrip::SetImageList(HIMAGELIST images) {
	m_Images = images;
	Layout();
}

int CTabStrip::Add(std::wstring text, int image, bool italic) {
	return Insert((int)m_Tabs.size(), std::move(text), image, italic);
}

int CTabStrip::Insert(int index, std::wstring text, int image, bool italic) {
	index = std::clamp(index, 0, (int)m_Tabs.size());
	m_Tabs.insert(m_Tabs.begin() + index, Tab{ std::move(text), image, italic });
	if (m_Selected >= index)
		m_Selected++;
	Layout();
	return index;
}

void CTabStrip::Remove(int index) {
	if (index < 0 || index >= (int)m_Tabs.size())
		return;
	m_Tabs.erase(m_Tabs.begin() + index);
	if (index < m_Selected)
		m_Selected--;
	else if (index == m_Selected)
		m_Selected = std::min(m_Selected, (int)m_Tabs.size() - 1);
	m_Hot = -1;
	m_Pressed = -1;
	Layout();
}

void CTabStrip::RemoveAll() {
	m_Tabs.clear();
	m_Selected = m_Hot = m_Pressed = -1;
	Layout();
}

int CTabStrip::GetCount() const {
	return (int)m_Tabs.size();
}

int CTabStrip::GetSelected() const {
	return m_Selected;
}

void CTabStrip::Select(int index) {
	if (index < -1 || index >= (int)m_Tabs.size())
		return;
	m_Selected = index;
	Layout();
	EnsureVisible(index);
}

void CTabStrip::SetText(int index, std::wstring text) {
	if (index < 0 || index >= (int)m_Tabs.size() || m_Tabs[index].Text == text)
		return;
	m_Tabs[index].Text = std::move(text);
	Layout();
}

std::wstring const& CTabStrip::GetText(int index) const {
	return m_Tabs[index].Text;
}

void CTabStrip::SetItalic(int index, bool italic) {
	if (index < 0 || index >= (int)m_Tabs.size() || m_Tabs[index].Italic == italic)
		return;
	m_Tabs[index].Italic = italic;
	Layout();
}

bool CTabStrip::IsItalic(int index) const {
	return index >= 0 && index < (int)m_Tabs.size() && m_Tabs[index].Italic;
}

void CTabStrip::SetImage(int index, int image) {
	if (index < 0 || index >= (int)m_Tabs.size())
		return;
	m_Tabs[index].Image = image;
	Layout();
}

int CTabStrip::GetIdealHeight() const {
	return std::max(m_FontHeight, m_Images ? ImageWidth() : Scale(ImageSize)) + Scale(12);
}

void CTabStrip::UpdateDpi() {
	if (!m_hWnd)
		return;
	CreateFonts();
	Layout();
}

void CTabStrip::UpdateColors() {
	if (m_hWnd)
		Invalidate();
}

void CTabStrip::SetFont(LOGFONT const* font) {
	if (font)
		m_CustomFont = *font;
	else
		m_CustomFont.reset();
	if (m_hWnd) {
		CreateFonts();
		Layout();
	}
}

// the fonts at the strip's DPI
void CTabStrip::CreateFonts() {
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	LOGFONT font;
	if (m_CustomFont) {
		font = *m_CustomFont;
		font.lfHeight = DpiHelper::Scale(font.lfHeight, dpi, USER_DEFAULT_SCREEN_DPI);
		font.lfWidth = DpiHelper::Scale(font.lfWidth, dpi, USER_DEFAULT_SCREEN_DPI);
	}
	else
		font = DpiHelper::GetNonClientMetrics(dpi).lfMessageFont;
	if (!m_Font.IsNull())
		m_Font.DeleteObject();
	if (!m_ItalicFont.IsNull())
		m_ItalicFont.DeleteObject();
	m_Font.CreateFontIndirect(&font);
	font.lfItalic = TRUE;
	m_ItalicFont.CreateFontIndirect(&font);
	// (the height of its characters: lfHeight may be of the cell)
	CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_Font);
	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	dc.SelectFont(old);
	m_FontHeight = tm.tmHeight + Scale(2);
}

// The tabs' widths: as their texts need (within limits), narrower if they do not fit (to the minimum: then they scroll)
void CTabStrip::Layout() {
	if (!m_hWnd)
		return;
	CClientDC dc(m_hWnd);
	CRect rc(0, 0, TabsWidth(), 0);
	auto extra = Scale(2 * PadX) + (m_Images ? ImageWidth() + Scale(ImageGap) : 0) + (m_Closable ? Scale(CloseSize + CloseGap) : 0);
	auto minWidth = Scale(MinWidth), maxWidth = Scale(MaxWidth);
	int total = 0;
	for (auto& tab : m_Tabs) {
		auto font = dc.SelectFont(tab.Italic ? m_ItalicFont : m_Font);
		CSize size;
		dc.GetTextExtent(tab.Text.c_str(), (int)tab.Text.size(), &size);
		dc.SelectFont(font);
		tab.Width = std::clamp((int)size.cx + extra, minWidth, maxWidth);
		total += tab.Width;
	}
	if (total > rc.Width() && !m_Tabs.empty()) {
		auto each = std::max(minWidth, rc.Width() / (int)m_Tabs.size());
		total = 0;
		for (auto& tab : m_Tabs) {
			tab.Width = std::min(tab.Width, each);
			total += tab.Width;
		}
	}
	int left = 0;
	for (auto& tab : m_Tabs) {
		tab.Left = left;
		left += tab.Width;
	}
	m_TotalWidth = total;
	m_Scroll = std::clamp(m_Scroll, 0, std::max(0, total - rc.Width()));
	Invalidate();
}

void CTabStrip::EnsureVisible(int index) {
	if (index < 0 || index >= (int)m_Tabs.size() || !m_hWnd)
		return;
	CRect rc(0, 0, TabsWidth(), 0);
	auto const& tab = m_Tabs[index];
	if (tab.Left < m_Scroll)
		m_Scroll = tab.Left;
	else if (tab.Left + tab.Width > m_Scroll + rc.Width())
		m_Scroll = tab.Left + tab.Width - rc.Width();
	m_Scroll = std::clamp(m_Scroll, 0, std::max(0, m_TotalWidth - rc.Width()));
	Invalidate();
}

CRect CTabStrip::TabRect(int index) const {
	CRect rc;
	GetClientRect(&rc);
	auto const& tab = m_Tabs[index];
	return CRect(tab.Left - m_Scroll, rc.top, tab.Left - m_Scroll + tab.Width, rc.bottom);
}

CRect CTabStrip::CloseRect(int index) const {
	auto rc = TabRect(index);
	int size = Scale(CloseSize), right = rc.right - Scale(PadX - 4);
	auto top = rc.top + (rc.Height() - size) / 2;
	return CRect(right - size, top, right, top + size);
}

bool CTabStrip::HasCloseButton(int index) const {
	return m_Closable && index >= 0 && (index == m_Selected || index == m_Hot);
}

int CTabStrip::HitTest(CPoint pt, bool* onClose) const {
	if (onClose)
		*onClose = false;
	if (pt.x >= TabsWidth())
		return -1;
	for (int i = 0; i < (int)m_Tabs.size(); i++) {
		if (!TabRect(i).PtInRect(pt))
			continue;
		if (onClose && m_Closable)
			*onClose = CloseRect(i).PtInRect(pt);
		return i;
	}
	return -1;
}

LRESULT CTabStrip::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	CreateFonts();
	return 0;
}

LRESULT CTabStrip::OnSettingChange(UINT, WPARAM, LPARAM, BOOL& handled) {
	CreateFonts();
	Layout();
	handled = FALSE;
	return 0;
}

LRESULT CTabStrip::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& handled) {
	UpdateDpi();
	handled = FALSE;
	return 0;
}

LRESULT CTabStrip::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	EnsureVisible(m_Selected);
	return 0;
}

LRESULT CTabStrip::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	CPaintDC paint(m_hWnd);
	CRect client;
	GetClientRect(&client);
	CMemoryDC dc(paint, client);
	auto colors = CurrentColors();
	dc.FillSolidRect(&client, colors.Strip);
	dc.SetBkMode(TRANSPARENT);
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	auto scale = [&](int value) { return DpiHelper::Scale(value, dpi); };
	auto image = ImageWidth();
	auto line = std::max(1, scale(1));	// (the width of the glyphs' strokes)

	CPen border;
	border.CreatePen(PS_SOLID, 1, colors.Border);
	auto oldPen = dc.SelectPen(border);
	// (the line under the tabs: the selected one is open to what it shows)
	dc.MoveTo(client.left, client.bottom - 1);
	dc.LineTo(client.right, client.bottom - 1);

	// (the tabs: not over the list button)
	CRect tabs(client.left, client.top, TabsWidth(), client.bottom);
	dc.SaveDC();
	dc.IntersectClipRect(&tabs);
	for (int i = 0; i < (int)m_Tabs.size(); i++) {
		auto rc = TabRect(i);
		if (rc.right <= tabs.left || rc.left >= tabs.right)
			continue;
		auto const& tab = m_Tabs[i];
		bool selected = i == m_Selected;
		auto back = selected ? colors.Selected : i == m_Hot ? colors.Hot : colors.Tab;
		CRect body(rc);
		body.right--;
		dc.FillSolidRect(&body, back);
		if (selected) {
			CRect accent(body.left, body.top, body.right, body.top + scale(AccentHeight));
			dc.FillSolidRect(&accent, colors.Accent);
		}
		else {
			dc.MoveTo(body.left, body.bottom - 1);
			dc.LineTo(body.right + 1, body.bottom - 1);
		}
		dc.MoveTo(body.right, body.top);
		dc.LineTo(body.right, body.bottom);

		auto x = rc.left + scale(PadX);
		if (m_Images) {
			if (tab.Image >= 0)
				::ImageList_Draw(m_Images, tab.Image, dc, x, rc.top + (rc.Height() - image) / 2, ILD_TRANSPARENT);
			x += image + scale(ImageGap);
		}
		CRect text(x, rc.top, rc.right - scale(PadX) - (m_Closable ? scale(CloseSize + CloseGap - 4) : 0), rc.bottom);
		auto font = dc.SelectFont(tab.Italic ? m_ItalicFont : m_Font);
		dc.SetTextColor(selected ? colors.SelectedText : colors.Text);
		dc.DrawText(tab.Text.c_str(), (int)tab.Text.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		dc.SelectFont(font);

		if (HasCloseButton(i)) {
			auto close = CloseRect(i);
			if (i == m_Hot && m_HotClose)
				dc.FillSolidRect(&close, colors.CloseHot);
			CPen pen;
			pen.CreatePen(PS_SOLID, 1, selected ? colors.SelectedText : colors.Text);
			auto old = dc.SelectPen(pen);
			close.DeflateRect(scale(4), scale(4));
			// (each stroke as wide as a pixel at 96 DPI: lines side by side)
			for (int i = 0; i < line; i++) {
				dc.MoveTo(close.left + i, close.top);
				dc.LineTo(close.right + i, close.bottom);
				dc.MoveTo(close.right - 1 + i, close.top);
				dc.LineTo(close.left - 1 + i, close.bottom);
			}
			dc.SelectPen(old);
		}
	}
	dc.RestoreDC(-1);
	// the list button: a chevron
	if (m_ListButton) {
		auto button = ListButtonRect();
		CPen pen;
		pen.CreatePen(PS_SOLID, 1, colors.Text);
		auto old = dc.SelectPen(pen);
		auto cx = button.left + button.Width() / 2, cy = button.top + button.Height() / 2;
		for (int row = 0; row < 2 * line; row++) {
			dc.MoveTo(cx - scale(4), cy - scale(2) + row);
			dc.LineTo(cx, cy + scale(2) + row);
			dc.LineTo(cx + scale(5), cy - scale(3) + row);
		}
		dc.SelectPen(old);
	}
	dc.SelectPen(oldPen);
	return 0;
}

LRESULT CTabStrip::OnLButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	CPoint pt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
	if (m_ListButton && ListButtonRect().PtInRect(pt)) {
		ShowList();
		return 0;
	}
	bool onClose;
	auto index = HitTest(pt, &onClose);
	if (index < 0)
		return 0;
	if (onClose && HasCloseButton(index)) {
		m_Pressed = index;
		SetCapture();
		return 0;
	}
	if (index != m_Selected) {
		m_Selected = index;
		EnsureVisible(index);
		Invalidate();
		if (OnSelected)
			OnSelected(index);
	}
	// (it may be dragged)
	if (OnMoved && m_Tabs.size() > 1) {
		m_Dragged = index;
		m_DragStart = pt;
		m_Dragging = false;
		SetCapture();
	}
	return 0;
}

LRESULT CTabStrip::OnLButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Dragged >= 0) {
		m_Dragged = -1;
		m_Dragging = false;
		::ReleaseCapture();
		return 0;
	}
	if (m_Pressed < 0)
		return 0;
	::ReleaseCapture();
	bool onClose;
	auto index = HitTest(CPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)), &onClose);
	auto pressed = m_Pressed;
	m_Pressed = -1;
	if (index == pressed && onClose && OnCloseRequested)
		OnCloseRequested(index);
	return 0;
}

LRESULT CTabStrip::OnLButtonDblClk(UINT, WPARAM, LPARAM lp, BOOL&) {
	bool onClose;
	auto index = HitTest(CPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)), &onClose);
	if (index >= 0 && !onClose && OnDoubleClicked)
		OnDoubleClicked(index);
	return 0;
}

LRESULT CTabStrip::OnMButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	auto index = HitTest(CPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
	if (index >= 0 && m_Closable && OnCloseRequested)
		OnCloseRequested(index);
	return 0;
}

LRESULT CTabStrip::OnRButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	CPoint pt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
	auto index = HitTest(pt);
	ClientToScreen(&pt);
	if (OnContextMenu)
		OnContextMenu(index, pt);
	return 0;
}

LRESULT CTabStrip::OnMouseMove(UINT, WPARAM wp, LPARAM lp, BOOL&) {
	// a tab dragged: where the mouse is (among the others)
	if (m_Dragged >= 0 && (wp & MK_LBUTTON)) {
		CPoint pt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		if (!m_Dragging && std::abs(pt.x - m_DragStart.x) >= DpiHelper::GetSystemMetrics(SM_CXDRAG, m_hWnd))
			m_Dragging = true;
		if (!m_Dragging)
			return 0;
		auto target = HitTest(CPoint(std::clamp((int)pt.x, 0, TabsWidth() - 1), TabRect(0).CenterPoint().y));
		if (target < 0)
			target = pt.x < 0 ? 0 : (int)m_Tabs.size() - 1;
		if (target != m_Dragged) {
			auto tab = std::move(m_Tabs[m_Dragged]);
			m_Tabs.erase(m_Tabs.begin() + m_Dragged);
			m_Tabs.insert(m_Tabs.begin() + target, std::move(tab));
			if (m_Selected == m_Dragged)
				m_Selected = target;
			auto from = m_Dragged;
			m_Dragged = target;
			Layout();
			EnsureVisible(target);
			OnMoved(from, target);
		}
		return 0;
	}
	if (!m_Tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_Tracking = ::TrackMouseEvent(&tme);
	}
	bool onClose;
	auto index = HitTest(CPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)), &onClose);
	if (index != m_Hot || onClose != m_HotClose) {
		m_Hot = index;
		m_HotClose = onClose;
		Invalidate();
	}
	return 0;
}

LRESULT CTabStrip::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_Tracking = false;
	if (m_Hot >= 0) {
		m_Hot = -1;
		m_HotClose = false;
		Invalidate();
	}
	return 0;
}

LRESULT CTabStrip::OnMouseWheel(UINT, WPARAM wp, LPARAM, BOOL&) {
	CRect rc;
	GetClientRect(&rc);
	auto delta = GET_WHEEL_DELTA_WPARAM(wp);
	m_Scroll = std::clamp(m_Scroll - delta * Scale(60) / WHEEL_DELTA, 0, std::max(0, m_TotalWidth - TabsWidth()));
	Invalidate();
	return 0;
}
