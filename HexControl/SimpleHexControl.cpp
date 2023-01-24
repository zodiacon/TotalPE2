#include "pch.h"
#include "SimpleHexControl.h"

void CHexControl::DoPaint(CDCHandle dc, RECT& rect) {
	dc.FillSolidRect(&rect, m_Colors.Background);
	if (!m_Buffer)
		return;

	m_EndOffset = m_StartOffset + m_Lines * m_BytesPerLine;
	if (m_EndOffset > m_Buffer->GetSize())
		m_EndOffset = m_Buffer->GetSize() + m_DataSize;

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	GetScrollInfo(SB_HORZ, &si);
	int xstart = -si.nPos * m_CharWidth;

	si.nPos = m_Buffer->GetSize() == 0 ? 0 : int(m_Buffer->GetSize() * ((float)m_StartOffset / m_Buffer->GetSize()) / m_BytesPerLine);
	SetScrollInfo(SB_VERT, &si);

	dc.SelectFont(m_Font);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(m_Colors.Background);

	WCHAR str[17];
	int i = 0;
	uint8_t data[512];
	POLYTEXT poly[128]{};

	int addrLength = m_Buffer->GetSize() < 1LL << 32 ? 8 : 16;
	const std::wstring addrFormat = L"%0" + std::to_wstring(addrLength) + L"X";
	int x = (addrLength + 1) * m_CharWidth;

	// data
	m_Text.clear();
	int lines = 0;
	CString number;
	int factor = m_CharWidth * (m_DataSize * 2 + 1);
	for (int y = 0;; y++) {
		auto offset = m_StartOffset + i;
		if (m_EditDigits > 0 && m_CaretOffset >= offset && m_CaretOffset < offset + m_BytesPerLine) {
			// changed data
			memcpy(data + m_CaretOffset - offset, &m_CurrentInput, m_DataSize);
		}
		uint32_t count = y == 0 && m_EditDigits > 0 ? m_DataSize : 0;
		if (m_Buffer->GetSize()) {
			count = m_Buffer->GetData(offset, data, m_BytesPerLine);
		}
		if (count == 0)
			break;

		if (count < m_BytesPerLine)
			::memset(data + count, 0, m_BytesPerLine - count);
		lines++;
		int jcount = min(m_BytesPerLine, count);
		auto step = m_DataSize;
		int x2 = 0;
		for (int j = 0; j < jcount; j += step) {
			//if (step > 1) {
			//	auto ds = uint32_t(m_Buffer->GetSize() - offset - j);
			//	ATLASSERT(ds >= 1);
			//	if (ds < m_DataSize)
			//		step = 1;
			//}
			DWORD64 value = 0;
			memcpy(&value, data + j, step);
			number = FormatNumber(value, step);
			bool selected = m_Selection.IsSelected(offset + j);
			dc.SetTextColor(selected ? m_Colors.SelectionText : ((int64_t)m_Modified.size() > offset + j && m_Modified[offset + j] ? m_Colors.Modified : (value ? m_Colors.Text : RGB(128, 128, 128))));
			dc.SetBkColor(selected ? m_Colors.SelectionBackground : m_Colors.Background);

			dc.TextOut(x + xstart + j / m_DataSize * factor + x2, y * m_CharHeight, number + L" ", -1);
			if (step < m_DataSize)
				x2 += m_CharWidth * 3;

			if (offset + j > m_Buffer->GetSize())
				break;
		}
		if (y * m_CharHeight > rect.bottom)
			break;
		i += m_BytesPerLine;
	}

	// offsets
	dc.SetTextColor(m_Colors.Offset);
	dc.SetBkColor(m_Colors.Background);
	x = 0;
	m_Text.clear();
	if (m_Buffer->GetSize() % m_BytesPerLine == 0)
		lines++;

	for (int y = 0; y < lines; y++) {
		std::wstring text;
		::StringCchPrintf(str, _countof(str), addrFormat.c_str(), m_BiasOffset + m_StartOffset + y * m_BytesPerLine);
		text = str;

		auto& p = poly[y];
		p.lpstr = text.c_str();
		p.x = x + xstart;
		p.y = y * m_CharHeight;
		p.n = (UINT)text.size();
		m_Text.push_back(std::move(text));
	}
	::PolyTextOut(dc, poly, (int)m_Text.size());

	// ASCII
	dc.SetTextColor(m_Colors.Ascii);
	x = m_CharWidth * (10 + m_BytesPerLine * (m_DataSize * 2 + 1) / m_DataSize);
	m_Text.clear();
	i = 0;
	WCHAR text[2]{};
	for (int y = 0;; y++) {
		auto count = m_Buffer->GetData(m_StartOffset + i, data, m_BytesPerLine);
		if (count == 0)
			break;
		if (m_EditDigits > 0 && m_CaretOffset >= m_StartOffset + i && m_CaretOffset < m_StartOffset + i + m_BytesPerLine) {
			// changed data
			memcpy(data + m_CaretOffset - m_StartOffset - i, &m_CurrentInput, m_DataSize);
		}
		auto offset = m_StartOffset + i;
		for (uint32_t j = 0; j < count; j++) {
			text[0] = data[j] < 32 || data[j] > 127 ? L'.' : (wchar_t)data[j];
			bool selected = m_Selection.IsSelected(offset + j);
			if (selected) {
				dc.SetTextColor(m_Colors.SelectionText);
				dc.SetBkColor(m_Colors.SelectionBackground);
			}
			else {
				dc.SetTextColor((int64_t)m_Modified.size() > offset + j && m_Modified[offset + j] ? m_Colors.Modified : m_Colors.Ascii);
				dc.SetBkColor(m_Colors.Background);
			}
			dc.TextOut(x + xstart + j * m_CharWidth, y * m_CharHeight, text, 1);
		}
		if (y * m_CharHeight > rect.bottom)
			break;
		i += m_BytesPerLine;
	}

	UpdateCaret();
}

bool CHexControl::HasSelection() const {
	return !m_Selection.IsEmpty();
}

LRESULT CHexControl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateCaret();
	ShowCaret();

	return 0;
}

LRESULT CHexControl::OnKillFocus(UINT, WPARAM, LPARAM, BOOL&) {
	HideCaret();

	return 0;
}

LRESULT CHexControl::OnLeftButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	m_Selection.Clear();

	SetCapture();
	int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
	auto offset = GetOffsetFromPoint(CPoint(x, y));
	if (offset >= 0) {
		m_CaretOffset = offset;
		UpdateCaret();
	}
	RedrawWindow();
	return 0;
}

LRESULT CHexControl::OnGetDialogCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTALLKEYS;
}

LRESULT CHexControl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (m_Buffer == nullptr)
		return 0;

	auto current = m_CaretOffset;
	bool abortEdit = false;
	bool shift = ::GetKeyState(VK_SHIFT) & 0x80;
	bool ctrl = ::GetKeyState(VK_CONTROL) & 0x80;

	bool redraw = false;

	if (shift) {
		if (m_Selection.IsEmpty()) {
			m_Selection.SetAnchor(m_CaretOffset);
		}
	}
	else if (!shift && !m_Selection.IsEmpty()) {
		ClearSelection();
		redraw = true;
	}

	switch (wParam) {
		case VK_ESCAPE:
			if (m_EditDigits > 0) {
				abortEdit = true;
			}
			break;

		case VK_HOME:
			if (ctrl) {
				m_CaretOffset = 0;
				redraw = true;
			}
			else {
				m_CaretOffset -= m_CaretOffset % m_BytesPerLine;
			}
			break;

		case VK_END:
			if (ctrl) {
				m_CaretOffset = m_Buffer->GetSize() - m_DataSize;
				redraw = true;
			}
			else {
				m_CaretOffset += m_BytesPerLine - m_CaretOffset % m_BytesPerLine - 1;
			}
			break;

		case VK_DOWN:
			if (m_CaretOffset + m_BytesPerLine < m_Buffer->GetSize()) {
				m_CaretOffset += m_BytesPerLine;
				if (m_CaretOffset >= m_EndOffset) {
					m_StartOffset += m_BytesPerLine;
					redraw = true;
				}
			}
			break;

		case VK_UP:
			if (m_CaretOffset >= m_BytesPerLine) {
				m_CaretOffset -= m_BytesPerLine;
				if (m_CaretOffset < m_StartOffset) {
					m_StartOffset -= m_BytesPerLine;
					redraw = true;
				}
			}
			break;

		case VK_RIGHT:
			m_CaretOffset += m_DataSize;
			if (m_CaretOffset > m_Buffer->GetSize())
				m_CaretOffset = m_Buffer->GetSize() + m_DataSize - 1;
			if (m_CaretOffset >= m_EndOffset) {
				m_StartOffset += m_BytesPerLine;
				redraw = true;
			}
			break;

		case VK_LEFT:
			m_CaretOffset -= m_DataSize;
			if (m_CaretOffset < 0)
				m_CaretOffset = 0;
			if (m_CaretOffset < m_StartOffset) {
				m_StartOffset -= m_BytesPerLine;
				redraw = true;
			}
			break;

		case VK_NEXT:
			if (m_CaretOffset + m_BytesPerLine * m_Lines < m_Buffer->GetSize()) {
				m_CaretOffset += m_BytesPerLine * m_Lines;
				if (m_CaretOffset > m_EndOffset) {
					m_StartOffset += m_BytesPerLine * m_Lines;
					redraw = true;
				}
			}
			break;

		case VK_PRIOR:
			if (m_CaretOffset >= m_BytesPerLine * m_Lines) {
				m_CaretOffset -= m_BytesPerLine * m_Lines;
				m_StartOffset -= m_BytesPerLine * m_Lines;
			}
			else {
				m_StartOffset = 0;
				m_CaretOffset = m_CaretOffset % m_BytesPerLine;
			}
			redraw = true;
			break;
	}
	if (shift && m_CaretOffset != current) {
		if (ctrl) {
			m_Selection.SetBox(min(m_CaretOffset, m_Selection.GetAnchor()), m_BytesPerLine,
				(m_CaretOffset - m_Selection.GetAnchor()) % m_BytesPerLine, (int)(m_CaretOffset - m_Selection.GetAnchor()) / m_BytesPerLine);
		}
		else {
			m_Selection.SetSimple(min(m_CaretOffset, m_Selection.GetAnchor()), abs(m_CaretOffset - m_Selection.GetAnchor()));
		}
		redraw = true;
		SendSelectionChanged();
	}

	NormalizeOffset(m_CaretOffset);
	if (abortEdit)
		CommitValue(current, m_CurrentInput);

	//if (m_SelStart >= 0 && selLength != m_SelLength)
	//	DrawSelection(current);

	if (m_CaretOffset != current) {
		if (!abortEdit && m_EditDigits > 0)
			CommitValue(current, m_CurrentInput);
	}

	if (redraw) {
		RedrawWindow();
	}
	else {
		auto pt = GetPointFromOffset(m_CaretOffset);
		SetCaretPos(pt.x, pt.y);
	}
	return 0;
}

void CHexControl::ResetInput() {
	m_EditDigits = 0;
	m_CurrentInput = 0;
}

int64_t CHexControl::NormalizeOffset(int64_t offset) const {
	offset -= offset % m_DataSize;
	return offset;
}

void CHexControl::RedrawCaretLine() {
	auto pos = GetPointFromOffset(m_CaretOffset);
	RECT rcClient;
	GetClientRect(&rcClient);
	RECT rc = { pos.x - m_CharWidth, pos.y, rcClient.right, pos.y + m_CharHeight };
	RedrawWindow(&rc);
}

LRESULT CHexControl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	InitFontMetrics();
	CreateSolidCaret(m_InsertMode ? 2 : m_CharWidth, m_CharHeight);
	m_Notify.hwndFrom = m_hWnd;
	return 0;
}

LRESULT CHexControl::OnHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &si);
	auto pos = si.nPos;

	switch (LOWORD(wParam)) {
		case SB_LINELEFT:
			si.nPos--;
			break;

		case SB_LINERIGHT:
			si.nPos++;
			break;

		case SB_PAGELEFT:
			si.nPos -= si.nPage;
			break;

		case SB_PAGERIGHT:
			si.nPos += si.nPage;
			break;

		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			break;

	}
	si.fMask = SIF_POS;
	SetScrollInfo(SB_HORZ, &si);
	GetScrollInfo(SB_HORZ, &si);
	if (si.nPos != pos) {
		RedrawWindow();
	}
	return 0;
}

LRESULT CHexControl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(SB_VERT, &si);
	auto pos = si.nPos;

	switch (LOWORD(wParam)) {
		case SB_TOP:
			si.nPos = si.nMin;
			break;

		case SB_BOTTOM:
			si.nPos = si.nMax;
			break;

		case SB_LINEUP:
			si.nPos--;
			break;

		case SB_LINEDOWN:
			si.nPos++;
			break;

		case SB_PAGEUP:
			si.nPos -= si.nPage;
			break;

		case SB_PAGEDOWN:
			si.nPos += si.nPage;
			break;

		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			break;
	}
	if (si.nPos != pos) {
		si.fMask = SIF_POS;
		SetScrollInfo(SB_VERT, &si);
		GetScrollInfo(SB_VERT, &si);
		auto start = si.nPos * m_BytesPerLine;
		m_StartOffset = start;
		RedrawWindow();
	}
	return 0;
}

void CHexControl::SendSelectionChanged() {
	m_Notify.idFrom = GetWindowLongPtr(GWLP_ID);
	m_Notify.code = NMHX_SELECTION_CHANGED;
	GetParent().SendMessage(WM_NOTIFY, m_Notify.idFrom, reinterpret_cast<LPARAM>(&m_Notify));
}

void CHexControl::RecalcLayout() {
	if (m_Buffer == nullptr) {
		return;
	}

	CRect rc;
	GetClientRect(&rc);

	auto lines = int((m_Buffer->GetSize() + (m_BytesPerLine - 1)) / m_BytesPerLine);
	m_Lines = min(rc.Height() / m_CharHeight, int(m_Buffer->GetSize() / m_BytesPerLine));
	if (m_Buffer->GetSize() % m_BytesPerLine == 0) {
		lines++;
		m_Lines++;
	}

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_PAGE | SIF_RANGE;
	si.nMin = 0;
	si.nMax = lines - 1;
	si.nPage = rc.bottom / m_CharHeight;
	SetScrollInfo(SB_VERT, &si);

	m_AddressDigits = m_Buffer->GetSize() >= 1LL << 32 ? 16 : 8;
	m_Chars = m_AddressDigits + 1 + m_BytesPerLine / m_DataSize * (1 + 2 * m_DataSize) + 1 + m_BytesPerLine;

	si.nMax = static_cast<int>(m_Chars) - 1;
	si.nPage = rc.right / m_CharWidth;
	SetScrollInfo(SB_HORZ, &si);

	si.fMask = SIF_POS;
	GetScrollInfo(SB_VERT, &si);

	m_StartOffset = si.nPos * m_BytesPerLine;
	if (m_StartOffset + m_Lines * m_BytesPerLine >= m_Buffer->GetSize()) {
		m_StartOffset = m_Buffer->GetSize() - (m_Lines - 1) * m_BytesPerLine;
		m_StartOffset = m_StartOffset - m_StartOffset % m_BytesPerLine;
		if (m_StartOffset < 0)
			m_StartOffset = 0;
	}
	m_EndOffset = m_StartOffset + m_Lines * m_BytesPerLine;
	if (m_EndOffset > m_Buffer->GetSize())
		m_EndOffset = m_Buffer->GetSize();
}

void CHexControl::InitFontMetrics() {
	if (m_Font)
		m_Font.DeleteObject();
	m_Font.CreatePointFont(m_FontPointSize, L"Consolas");
	CClientDC dc(*this);
	dc.SelectFont(m_Font);
	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	m_CharHeight = tm.tmHeight;
	m_CharWidth = tm.tmAveCharWidth;
}

CPoint CHexControl::GetPointFromOffset(int64_t offset) const {
	if (offset < m_StartOffset || offset > m_EndOffset)
		return CPoint(-1, -1);

	int line = int((offset - m_StartOffset) / m_BytesPerLine);
	int b = (offset - m_StartOffset) % m_BytesPerLine / m_DataSize;

	CPoint pt;
	pt.y = line * m_CharHeight;
	pt.x = (b * (m_DataSize * 2 + 1) + m_AddressDigits + 1) * m_CharWidth;
	ATLTRACE(L"GetPointFromOffset %llX: (%d,%d)\n", offset, pt.x, pt.y);

	return pt;
}

int64_t CHexControl::GetOffsetFromPoint(const POINT& pt) const {
	uint32_t line = pt.y / m_CharHeight;
	uint32_t b = pt.x / m_CharWidth - (m_AddressDigits + 1);

	b /= m_DataSize * 2 + 1;
	if (b < 0 || b >= m_BytesPerLine)
		return -1;

	return m_StartOffset + line * m_BytesPerLine + b * m_DataSize;
}

void CHexControl::DrawNumber(CDCHandle dc, int64_t offset, uint64_t value, uint32_t digitsChanged) {
	auto pos = GetPointFromOffset(offset);
	CString temp = FormatNumber(value);
	HideCaret();
	bool selected = false;
	dc.SetTextColor(selected ? ::GetSysColor(COLOR_HIGHLIGHTTEXT) : m_Colors.Text);
	dc.SetBkColor(selected ? ::GetSysColor(COLOR_HIGHLIGHT) : ::GetSysColor(COLOR_WINDOW));
	if (digitsChanged < m_DataSize * 2)
		dc.TextOutW(pos.x, pos.y, temp, m_DataSize * 2);
	dc.SetTextColor(RGB(255, 0, 0));
	dc.TextOutW(pos.x + m_CharWidth * (m_DataSize * 2 - digitsChanged), pos.y, temp.Right(digitsChanged), digitsChanged);
	ShowCaret();
}

void CHexControl::UpdateCaret() {
	auto pt = GetPointFromOffset(m_CaretOffset);
	HideCaret();
	if (pt.y >= 0) {
		SetCaretPos(pt.x, pt.y);
	}
	else {
		SetCaretPos(-1000, -1000);
	}
	ShowCaret();
}

void CHexControl::RedrawWindow(RECT* rc) {
	::RedrawWindow(m_hWnd, rc, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_INTERNALPAINT);
}

void CHexControl::ClearSelection() {
	m_Selection.Clear();
	SendSelectionChanged();
}

void CHexControl::CommitValue(int64_t offset, uint64_t value) {
	// NOP - readonly
}

LRESULT CHexControl::OnMouseMove(UINT, WPARAM wp, LPARAM lParam, BOOL&) {
	if (GetCapture() != m_hWnd)
		return 0;

	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

	auto offset = GetOffsetFromPoint(pt);
	if (offset > m_EndOffset)
		offset = m_EndOffset;

	auto maxoffset = max(offset, m_CaretOffset);
	auto minoffset = min(offset, m_CaretOffset);
	if (wp & MK_CONTROL) {
		int width = maxoffset % m_BytesPerLine - minoffset % m_BytesPerLine, height = int(maxoffset - minoffset) / m_BytesPerLine;
		if (offset < m_CaretOffset && width < 0) {
			height++;
			width++;
		}
		if (width < 0) {
			minoffset += width;
			width = -width;
		}
		m_Selection.SetBox(minoffset, m_BytesPerLine, width, height);
	}
	else
		m_Selection.SetSimple(minoffset, abs(offset - m_CaretOffset));
	SendSelectionChanged();
	RedrawWindow();

	return 0;
}

LRESULT CHexControl::OnLeftButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	ReleaseCapture();
	return 0;
}

LRESULT CHexControl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
	int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	if (delta == 0)
		return 0;
	int scroll = WHEEL_DELTA / 2;

	auto keys = GET_KEYSTATE_WPARAM(wParam);
	if (keys == 0) {
		if (delta > 0) {
			m_StartOffset -= m_BytesPerLine * abs(delta) / scroll;
			if (m_StartOffset < 0)
				m_StartOffset = 0;
		}
		else {
			m_StartOffset += m_BytesPerLine * abs(delta) / scroll;
			if (m_StartOffset > m_Buffer->GetSize() - (m_Lines - 1) * m_BytesPerLine)
				m_StartOffset = m_Buffer->GetSize() - (m_Lines - 1) * m_BytesPerLine - m_Buffer->GetSize() % m_BytesPerLine;
		}
		RedrawWindow();
	}
	else if (keys == MK_CONTROL) {
		auto oldSize = m_FontPointSize;
		// change font size
		m_FontPointSize = static_cast<int>(m_FontPointSize * (delta > 0 ? 1.1 : (1 / 1.1)));
		if (m_FontPointSize < 70)
			m_FontPointSize = 70;
		else if (m_FontPointSize > 300)
			m_FontPointSize = 300;

		if (m_FontPointSize != oldSize) {
			InitFontMetrics();
			RecalcLayout();
		}
	}

	return 0;
}
LRESULT CHexControl::OnChar(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (IsReadOnly())
		return 0;

	bool digit = wParam >= '0' && wParam <= '9';
	bool hexdigit = wParam >= 'A' && wParam <= 'F' || wParam >= 'a' && wParam <= 'f';

	if (!digit && !hexdigit) {
		::MessageBeep((UINT)-1);
		return 0;
	}

	uint8_t value;
	if (digit)
		value = static_cast<uint8_t>(wParam) - '0';
	else {
		if (wParam > 0x60)
			wParam -= 0x20;
		value = static_cast<uint8_t>(wParam) - 'A' + 10;
	}

	if (m_EditDigits == 0) {
		// save old value
		BYTE buffer[8] = { 0 };
		m_Buffer->GetData(m_CaretOffset, buffer, m_DataSize);
		m_OldValue = *(uint64_t*)buffer;
	}

	m_CurrentInput = (m_CurrentInput << 4) | value;
	m_EditDigits++;

	auto pos = GetPointFromOffset(m_CaretOffset);
	if (m_EditDigits == m_DataSize * 2) {
		CommitValue(m_CaretOffset, m_CurrentInput);
		// end of number
		m_CaretOffset += m_DataSize;
		if (m_CaretOffset >= m_EndOffset && m_CaretOffset % m_BytesPerLine == 0) {
			// DrawOffset(m_CaretOffset);
		}
		SetCaretPos(pos.x, pos.y);
		RECT rcClient;
		GetClientRect(&rcClient);
		RECT rc = { pos.x - m_CharWidth, pos.y, rcClient.right, pos.y + m_CharHeight };
		RedrawWindow(&rc);
	}
	else {
		CClientDC dc(m_hWnd);
		DrawNumber(dc.m_hDC, m_CaretOffset, m_CurrentInput, m_EditDigits);
	}
	return 0;
}

CString CHexControl::FormatNumber(ULONGLONG number, int size) const {
	if (size == 0)
		size = m_DataSize;
	static PCWSTR formats[] = {
		L"%02X",
		L"%04X",
		L"%08X",
		nullptr,
		L"%016llX"
	};

	CString result;
	result.Format(formats[size >> 1], number);
	return result.Right(size * 2);
}

LRESULT CHexControl::OnContextMenu(UINT, WPARAM, LPARAM, BOOL&) {
	NMHDR hdr;
	hdr.hwndFrom = m_hWnd;
	hdr.code = NM_RCLICK;
	hdr.idFrom = GetWindowLongPtr(GWLP_ID);
	GetParent().SendMessage(WM_NOTIFY, hdr.idFrom, reinterpret_cast<LPARAM>(&hdr));
	return 0;
}

LRESULT CHexControl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	RecalcLayout();

	return 0;
}

HWND CHexControl::GetHwnd() const {
	return m_hWnd;
}

void CHexControl::SetBufferManager(IBufferManager* mgr) {
	m_Buffer = mgr;
	ClearSelection();
	RecalcLayout();
	Invalidate();
}

IBufferManager* CHexControl::GetBufferManager() const {
	return m_Buffer;
}

void CHexControl::SetReadOnly(bool readonly) {
	m_ReadOnly = readonly;
}

bool CHexControl::IsReadOnly() const {
	return m_ReadOnly || (m_Buffer && m_Buffer->IsReadOnly());
}

void CHexControl::SetSize(int64_t size) {
}

bool CHexControl::SetDataSize(int32_t size) {
	if ((m_DataSize & (m_DataSize - 1)) != 0 || size > 8)
		return false;

	m_DataSize = size;
	RecalcLayout();
	RedrawWindow();
	return true;
}

int32_t CHexControl::GetDataSize() const {
	return m_DataSize;
}

bool CHexControl::SetBytesPerLine(int32_t bytesPerLine) {
	if (bytesPerLine % 8 != 0 || bytesPerLine == 0)
		return false;

	m_BytesPerLine = bytesPerLine;
	RecalcLayout();
	RedrawWindow();
	return true;
}

int32_t CHexControl::GetBytesPerLine() const {
	return m_BytesPerLine;
}

bool CHexControl::Copy(int64_t offset, int64_t size) const {
	if (offset < 0)
		offset = m_Selection.GetOffset();
	if (size == 0)
		size = m_Selection.GetLength();
	if (size == 0 || offset < 0)
		return false;

	auto text = std::format(L"Offset: {:X} Data: ", offset + m_BiasOffset);
	CString fmt;
	fmt.Format(L"%%0%dX ", m_DataSize * 2);
	uint64_t value = 0;
	CString item;
	for (int64_t i = 0; i < size; i += m_DataSize) {
		m_Buffer->GetData(offset + i, (uint8_t*)&value, m_DataSize);
		item.Format(fmt, value);
		text += (PCWSTR)item;
	}
	CopyText(text.c_str());
	return true;
}

bool CHexControl::Paste(int64_t offset) {
	return false;
}

bool CHexControl::CanCopy() const {
	return HasSelection();
}

bool CHexControl::CanPaste() const {
	return false;
}

bool CHexControl::Cut(int64_t offset, int64_t size) {
	return false;
}

bool CHexControl::Delete(int64_t offset, int64_t size) {
	return false;
}

bool CHexControl::CanCut() const {
	return CanDelete();
}

bool CHexControl::CanDelete() const {
	return !IsReadOnly() && !m_Selection.IsEmpty();
}

int64_t CHexControl::SetBiasOffset(int64_t offset) {
	auto current = m_BiasOffset;
	m_BiasOffset = offset;
	RedrawWindow();
	return current;
}

int64_t CHexControl::GetBiasOffset() const {
	return m_BiasOffset;
}

HexControlColors& CHexControl::GetColors() {
	return m_Colors;
}

std::wstring CHexControl::GetText(int64_t offset, int64_t size) const {
	return std::wstring();
}

void CHexControl::Refresh() {
	RecalcLayout();
	RedrawWindow();
}

bool CHexControl::DeleteState(int64_t offset) {
	return m_Modified[offset] = false;
}

bool CHexControl::SetHexControlClient(IHexControlClient* client) {
	m_pClient = client;
	return m_pClient != nullptr;
}

bool CHexControl::CopyText(PCWSTR text) const {
	if (::OpenClipboard(m_hWnd)) {
		::EmptyClipboard();
		auto size = (::wcslen(text) + 1) * sizeof(WCHAR);
		auto hData = ::GlobalAlloc(GMEM_MOVEABLE, size);
		if (hData) {
			auto p = ::GlobalLock(hData);
			if (p) {
				::memcpy(p, text, size);
				::GlobalUnlock(p);
				::SetClipboardData(CF_UNICODETEXT, hData);
			}
		}
		::CloseClipboard();
		if (hData)
			return true;
	}
	return false;
}
