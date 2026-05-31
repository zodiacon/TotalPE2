#include "pch.h"
#include "SimpleHexControl.h"
#include <algorithm>

void CHexControl::DoPaint(CDCHandle dc, RECT& rect) {
	dc.FillSolidRect(&rect, m_Colors.Background);
	if (!m_Buffer)
		return;

	m_EndOffset = m_StartOffset + m_Lines * m_BytesPerLine;
	if (m_EndOffset > m_Buffer->GetSize())
		m_EndOffset = m_Buffer->GetSize();

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	GetScrollInfo(SB_HORZ, &si);
	int xstart = -si.nPos * m_CharWidth;

	si.nPos = (int)(m_StartOffset / m_BytesPerLine);
	SetScrollInfo(SB_VERT, &si);

	dc.SelectFont(m_Font);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(m_Colors.Background);

	WCHAR str[21];
	int i = 0;
	uint8_t data[512];
	std::vector<POLYTEXT> poly(m_Lines + 2);

	const std::wstring addrFormat = L"%0" + std::to_wstring(m_AddressDigits) + (m_DecimalAddresses ? L"llu" : L"X");
	int x = (m_AddressDigits + 1) * m_CharWidth;
	int factor = m_CharWidth * (m_DataSize * 2 + 1);
	int rulerHeight = GetRulerHeight();

	// ruler
	if (m_ShowRuler) {
		dc.SetTextColor(m_Colors.Ruler);
		dc.SetBkColor(m_Colors.Background);
		for (int j = 0; j < (int)m_BytesPerLine; j += m_DataSize) {
			CString col = FormatNumber(j, m_DataSize);
			dc.TextOut(x + xstart + (j / m_DataSize) * factor + GetSeparatorExtraX(j), 0, col + L" ", -1);
		}
		int ax = GetAsciiStartX();
		for (int j = 0; j < (int)m_BytesPerLine; j++) {
			int nibble = j & 0xF;
			WCHAR ch = nibble < 10 ? (L'0' + nibble) : (L'A' + nibble - 10);
			dc.TextOut(ax + xstart + j * m_CharWidth, 0, &ch, 1);
		}
	}

	// data
	m_Text.clear();
	int lines = 0;
	CString number;
	for (int y = 0;; y++) {
		auto offset = m_StartOffset + i;
		uint32_t count = 0;
		if (m_Buffer->GetSize()) {
			count = m_Buffer->GetData(offset, data, m_BytesPerLine);
		}
		if (count == 0)
			break;
		if (m_EditDigits > 0 && m_CaretOffset >= offset && m_CaretOffset < offset + m_BytesPerLine) {
			// overlay in-progress edit
			memcpy(data + m_CaretOffset - offset, &m_CurrentInput, m_DataSize);
		}

		if (count < m_BytesPerLine)
			::memset(data + count, 0, m_BytesPerLine - count);
		lines++;
		int jcount = std::min(m_BytesPerLine, count);
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
			if (m_BigEndian && step > 1) {
				DWORD64 swapped = 0;
				for (uint32_t s = 0; s < step; s++)
					((uint8_t*)&swapped)[step - 1 - s] = ((uint8_t*)&value)[s];
				value = swapped;
			}
			number = FormatNumber(value, step);
			bool selected = m_Selection.IsSelected(offset + j);
			if (selected) {
				dc.SetTextColor(m_Colors.SelectionText);
				dc.SetBkColor(m_Colors.SelectionBackground);
			}
			else if (auto* hl = GetHighlightAt(offset + j)) {
				dc.SetTextColor(hl->TextColor);
				dc.SetBkColor(hl->BkColor);
			}
			else if ((int64_t)m_Modified.size() > offset + j && m_Modified[offset + j]) {
				dc.SetTextColor(m_Colors.Modified);
				dc.SetBkColor(m_Colors.Background);
			}
			else {
				dc.SetTextColor(value ? m_Colors.Text : RGB(128, 128, 128));
				dc.SetBkColor(m_Colors.Background);
			}

			int sepX = GetSeparatorExtraX(j);
			dc.TextOut(x + xstart + j / m_DataSize * factor + x2 + sepX, rulerHeight + y * m_CharHeight, number + L" ", -1);
			if (step < m_DataSize)
				x2 += m_CharWidth * 3;

			if (offset + j > m_Buffer->GetSize())
				break;
		}
		if (rulerHeight + y * m_CharHeight > rect.bottom)
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
		p.y = rulerHeight + y * m_CharHeight;
		p.n = (UINT)text.size();
		m_Text.push_back(std::move(text));
	}
	::PolyTextOut(dc, poly.data(), (int)m_Text.size());

	// ASCII
	dc.SetTextColor(m_Colors.Ascii);
	x = GetAsciiStartX();
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
			else if (auto* hl = GetHighlightAt(offset + j)) {
				dc.SetTextColor(hl->TextColor);
				dc.SetBkColor(hl->BkColor);
			}
			else if ((int64_t)m_Modified.size() > offset + j && m_Modified[offset + j]) {
				dc.SetTextColor(m_Colors.Modified);
				dc.SetBkColor(m_Colors.Background);
			}
			else {
				dc.SetTextColor(m_Colors.Ascii);
				dc.SetBkColor(m_Colors.Background);
			}
			dc.TextOut(x + xstart + j * m_CharWidth, rulerHeight + y * m_CharHeight, text, 1);
		}
		if (rulerHeight + y * m_CharHeight > rect.bottom)
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

LRESULT CHexControl::OnLeftButtonDown(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	SetFocus();
	SetCapture();

	bool shift = (wParam & MK_SHIFT) != 0;
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

	auto offset = GetOffsetFromPoint(pt);
	bool ascii = false;
	if (offset < 0) {
		offset = GetAsciiOffsetFromPoint(pt);
		ascii = true;
	}

	if (offset < 0) {
		if (!shift) {
			m_Selection.Clear();
			RedrawWindow();
		}
		return 0;
	}

	if (shift) {
		m_Selection.SetSimple(std::min(m_CaretOffset, offset), abs(offset - m_CaretOffset));
		SendSelectionChanged();
	}
	else {
		m_Selection.Clear();
	}

	auto oldCaret = m_CaretOffset;
	m_SelectionFromAscii = ascii;
	m_CaretOffset = offset;
	UpdateCaret();
	RedrawWindow();
	SendCaretChanged(oldCaret);
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
	bool alt = ::GetKeyState(VK_MENU) & 0x80;

	bool redraw = false;

	if (shift) {
		if (m_Selection.IsEmpty()) {
			m_Selection.SetAnchor(m_CaretOffset);
		}
	}
	else if (!shift && !ctrl && !m_Selection.IsEmpty()) {
		ClearSelection();
		redraw = true;
	}

	switch (wParam) {
		case VK_TAB:
			m_SelectionFromAscii = !m_SelectionFromAscii;
			UpdateCaret();
			return 0;

		case VK_INSERT:
			SetInsertMode(!m_InsertMode);
			return 0;

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
				m_CaretOffset += m_BytesPerLine - m_CaretOffset % m_BytesPerLine - m_DataSize;
				if (m_CaretOffset > m_Buffer->GetSize() - m_DataSize)
					m_CaretOffset = m_Buffer->GetSize() - m_DataSize;
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
			if (m_CaretOffset > m_Buffer->GetSize() - m_DataSize)
				m_CaretOffset = m_Buffer->GetSize() - m_DataSize;
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
				if (m_StartOffset < 0)
					m_StartOffset = 0;
			}
			else {
				m_StartOffset = 0;
				m_CaretOffset = m_CaretOffset % m_BytesPerLine;
			}
			redraw = true;
			break;

		case 'A':
			if (ctrl) {
				m_Selection.SetSimple(0, m_Buffer->GetSize());
				m_CaretOffset = m_Buffer->GetSize() - m_DataSize;
				SendSelectionChanged();
				redraw = true;
			}
			break;

		case 'C':
			if (ctrl)
				Copy();
			break;

		case 'V':
			if (ctrl) {
				Paste();
				return 0;
			}
			break;

		case 'X':
			if (ctrl) {
				Cut();
				return 0;
			}
			break;

		case 'Z':
			if (ctrl) {
				Undo();
				return 0;
			}
			break;

		case 'Y':
			if (ctrl) {
				Redo();
				return 0;
			}
			break;

		case 'G':
			if (ctrl) {
				NMHexControlNotify n{};
				SendNotify(n, NMHX_GOTO_REQUESTED);
				return 0;
			}
			break;
	}
	if (shift && m_CaretOffset != current) {
		if (ctrl || alt) {
			auto anchor = m_Selection.GetAnchor();
			int anchorRow = (int)(anchor / m_BytesPerLine), anchorCol = (int)(anchor % m_BytesPerLine);
			int caretRow = (int)(m_CaretOffset / m_BytesPerLine), caretCol = (int)(m_CaretOffset % m_BytesPerLine);
			m_Selection.SetBox(
				(int64_t)std::min(anchorRow, caretRow) * m_BytesPerLine + std::min(anchorCol, caretCol),
				m_BytesPerLine,
				abs(caretCol - anchorCol) + m_DataSize,
				abs(caretRow - anchorRow) + 1);
		}
		else {
			m_Selection.SetSimple(std::min(m_CaretOffset, m_Selection.GetAnchor()), abs(m_CaretOffset - m_Selection.GetAnchor()));
		}
		redraw = true;
		SendSelectionChanged();
	}

	m_CaretOffset = NormalizeOffset(m_CaretOffset);
	if (abortEdit) {
		CommitValue(current, m_OldValue);
		RedrawWindow();
	}

	//if (m_SelStart >= 0 && selLength != m_SelLength)
	//	DrawSelection(current);

	if (m_CaretOffset != current) {
		if (!abortEdit && m_EditDigits > 0)
			CommitValue(current, m_CurrentInput);
		SendCaretChanged(current);
	}

	if (redraw) {
		RedrawWindow();
	}
	else {
		UpdateCaret();
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

void CHexControl::SendNotify(NMHDR& hdr, UINT code) {
	hdr.hwndFrom = m_hWnd;
	hdr.idFrom = static_cast<UINT_PTR>(GetWindowLongPtr(GWLP_ID));
	hdr.code = code;
	GetParent().SendMessage(WM_NOTIFY, hdr.idFrom, reinterpret_cast<LPARAM>(&hdr));
}

void CHexControl::SendSelectionChanged() {
	NMHexControlNotify n{};
	SendNotify(n, NMHX_SELECTION_CHANGED);
}

void CHexControl::SendCaretChanged(int64_t oldOffset) {
	if (m_CaretOffset == oldOffset)
		return;
	NMHexControlCaretChanged n{};
	n.OldOffset = oldOffset;
	n.NewOffset = m_CaretOffset;
	SendNotify(n, NMHX_CARET_CHANGED);
}

void CHexControl::RecalcLayout() {
	if (!m_hWnd)
		return;

	CRect rc;
	GetClientRect(&rc);

	SCROLLINFO si;
	si.cbSize = sizeof(si);

	if (m_Buffer == nullptr) {
		si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
		si.nMin = si.nMax = si.nPos = 0;
		si.nPage = 1;
		SetScrollInfo(SB_VERT, &si);
		SetScrollInfo(SB_HORZ, &si);
		m_Lines = 0;
		return;
	}

	auto lines = int((m_Buffer->GetSize() + (m_BytesPerLine - 1)) / m_BytesPerLine);
	int totalLines = int((m_Buffer->GetSize() + m_BytesPerLine - 1) / m_BytesPerLine);
	m_Lines = std::min((rc.Height() - GetRulerHeight()) / m_CharHeight, totalLines);
	if (m_Buffer->GetSize() % m_BytesPerLine == 0) {
		lines++;
		m_Lines++;
	}

	si.fMask = SIF_PAGE | SIF_RANGE;
	si.nMin = 0;
	si.nMax = lines - 1;
	si.nPage = (rc.bottom - GetRulerHeight()) / m_CharHeight;
	SetScrollInfo(SB_VERT, &si);

	bool big = m_Buffer->GetSize() >= 1LL << 32;
	m_AddressDigits = m_DecimalAddresses ? (big ? 20 : 10) : (big ? 16 : 8);
	// base chars + extra half-chars for column separators
	int sepExtra = (m_ColSeparator > 0) ? (int)(m_BytesPerLine / m_ColSeparator) : 0;
	m_Chars = m_AddressDigits + 1 + m_BytesPerLine / m_DataSize * (1 + 2 * m_DataSize) + 1 + m_BytesPerLine + (sepExtra + 1) / 2;

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
	m_Font.CreatePointFont(m_FontPointSize, m_FontFaceName);
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
	pt.y = GetRulerHeight() + line * m_CharHeight;
	pt.x = (b * (m_DataSize * 2 + 1) + m_AddressDigits + 1) * m_CharWidth + GetSeparatorExtraX(b * m_DataSize) - GetHScrollX();
	ATLTRACE(L"GetPointFromOffset %llX: (%d,%d)\n", offset, pt.x, pt.y);

	return pt;
}

int64_t CHexControl::GetOffsetFromPoint(const POINT& pt) const {
	int rulerHeight = GetRulerHeight();
	if (pt.y < rulerHeight)
		return -1;
	uint32_t line = (pt.y - rulerHeight) / m_CharHeight;
	int px = pt.x + GetHScrollX() - (m_AddressDigits + 1) * m_CharWidth;
	if (px < 0)
		return -1;

	int hexCols = m_BytesPerLine / m_DataSize;
	int cellW = m_DataSize * 2 + 1;
	int b = -1;
	for (int col = 0; col < hexCols; col++) {
		int colX = col * cellW * m_CharWidth + GetSeparatorExtraX(col * m_DataSize);
		if (px >= colX && px < colX + cellW * m_CharWidth) {
			b = col;
			break;
		}
	}
	if (b < 0)
		return -1;

	return m_StartOffset + line * m_BytesPerLine + b * m_DataSize;
}

int CHexControl::GetHScrollX() const {
	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	::GetScrollInfo(m_hWnd, SB_HORZ, &si); // original function not declated const
	return si.nPos * m_CharWidth;
}

int CHexControl::GetRulerHeight() const {
	return m_ShowRuler ? m_CharHeight + 4 : 0;
}

int CHexControl::GetSeparatorExtraX(int byteCol) const {
	if (m_ColSeparator == 0 || byteCol == 0)
		return 0;
	// number of separator gaps before this byte column (not counting col 0)
	return (int)(byteCol / m_ColSeparator) * (m_CharWidth / 2);
}

int CHexControl::GetAsciiStartX() const {
	int hexCols = m_BytesPerLine / m_DataSize;
	int base = (m_AddressDigits + 2 + hexCols * (m_DataSize * 2 + 1)) * m_CharWidth;
	// add all separator gaps within the hex area
	return base + GetSeparatorExtraX(m_BytesPerLine);
}

CPoint CHexControl::GetAsciiPointFromOffset(int64_t offset) const {
	if (offset < m_StartOffset || offset > m_EndOffset)
		return CPoint(-1, -1);

	int line = int((offset - m_StartOffset) / m_BytesPerLine);
	int col = int((offset - m_StartOffset) % m_BytesPerLine);

	CPoint pt;
	pt.y = GetRulerHeight() + line * m_CharHeight;
	pt.x = GetAsciiStartX() - GetHScrollX() + col * m_CharWidth;
	return pt;
}

int64_t CHexControl::GetAsciiOffsetFromPoint(const POINT& pt) const {
	int rulerHeight = GetRulerHeight();
	if (pt.y < rulerHeight)
		return -1;
	int col = (pt.x + GetHScrollX() - GetAsciiStartX()) / m_CharWidth;
	if (col < 0 || col >= (int)m_BytesPerLine)
		return -1;
	int64_t line = (pt.y - rulerHeight) / m_CharHeight;
	auto offset = m_StartOffset + line * m_BytesPerLine + col;
	if (offset >= m_Buffer->GetSize())
		return -1;
	return offset;
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
	auto pt = m_SelectionFromAscii
		? GetAsciiPointFromOffset(m_CaretOffset)
		: GetPointFromOffset(m_CaretOffset);
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
	if (m_ReadOnly || !m_Buffer)
		return;

	UndoRecord rec;
	rec.Offset = offset;
	rec.OldData = ReadBytes(offset, m_InsertMode ? 0 : m_DataSize);
	rec.OldModified = ReadModified(offset, m_InsertMode ? 0 : m_DataSize);
	rec.NewData.resize(m_DataSize);
	memcpy(rec.NewData.data(), &value, m_DataSize);

	if (m_InsertMode) {
		rec.Op = UndoRecord::Type::Insert;
		m_Buffer->Insert(offset, (uint8_t*)&value, m_DataSize);
		m_Modified.insert(m_Modified.begin() + offset, m_DataSize, true);
	}
	else {
		rec.Op = UndoRecord::Type::Overwrite;
		m_Buffer->SetData(offset, (uint8_t*)&value, m_DataSize);
		if ((int64_t)m_Modified.size() < m_Buffer->GetSize())
			m_Modified.resize((size_t)m_Buffer->GetSize(), false);
		for (uint32_t i = 0; i < m_DataSize; i++)
			m_Modified[offset + i] = true;
	}
	PushUndo(std::move(rec));
	ResetInput();
}

std::vector<uint8_t> CHexControl::ReadBytes(int64_t offset, int64_t count) const {
	std::vector<uint8_t> buf(count);
	if (count > 0 && m_Buffer)
		m_Buffer->GetData(offset, buf.data(), (uint32_t)count);
	return buf;
}

std::vector<bool> CHexControl::ReadModified(int64_t offset, int64_t count) const {
	std::vector<bool> bits(count, false);
	for (int64_t i = 0; i < count; i++) {
		auto idx = offset + i;
		if (idx < (int64_t)m_Modified.size())
			bits[i] = m_Modified[idx];
	}
	return bits;
}

void CHexControl::PushUndo(UndoRecord record) {
	if (!IsUndoRedoEnabled())
		return;
	m_RedoStack.clear();
	m_UndoStack.push_back(std::move(record));
	if (m_MaxUndoLevels > 0 && m_UndoStack.size() > m_MaxUndoLevels)
		m_UndoStack.erase(m_UndoStack.begin());
}

void CHexControl::ApplyUndo(const UndoRecord& rec) {
	switch (rec.Op) {
		case UndoRecord::Type::Overwrite:
			m_Buffer->SetData(rec.Offset, rec.OldData.data(), (uint32_t)rec.OldData.size());
			for (size_t i = 0; i < rec.OldModified.size(); i++)
				if (rec.Offset + (int64_t)i < (int64_t)m_Modified.size())
					m_Modified[rec.Offset + i] = rec.OldModified[i];
			break;
		case UndoRecord::Type::Insert:
			m_Buffer->Delete(rec.Offset, rec.NewData.size());
			m_Modified.erase(m_Modified.begin() + rec.Offset,
				m_Modified.begin() + rec.Offset + rec.NewData.size());
			RecalcLayout();
			break;
		case UndoRecord::Type::Delete:
			m_Buffer->Insert(rec.Offset, rec.OldData.data(), (uint32_t)rec.OldData.size());
			m_Modified.insert(m_Modified.begin() + rec.Offset, rec.OldModified.begin(), rec.OldModified.end());
			RecalcLayout();
			break;
		case UndoRecord::Type::Compound:
			for (auto it = rec.Children.rbegin(); it != rec.Children.rend(); ++it)
				ApplyUndo(*it);
			break;
	}
}

void CHexControl::ApplyRedo(const UndoRecord& rec) {
	switch (rec.Op) {
		case UndoRecord::Type::Overwrite:
			m_Buffer->SetData(rec.Offset, rec.NewData.data(), (uint32_t)rec.NewData.size());
			if ((int64_t)m_Modified.size() < rec.Offset + (int64_t)rec.NewData.size())
				m_Modified.resize((size_t)(rec.Offset + rec.NewData.size()), false);
			for (size_t i = 0; i < rec.NewData.size(); i++)
				m_Modified[rec.Offset + i] = true;
			break;
		case UndoRecord::Type::Insert:
			m_Buffer->Insert(rec.Offset, rec.NewData.data(), (uint32_t)rec.NewData.size());
			m_Modified.insert(m_Modified.begin() + rec.Offset, rec.NewData.size(), true);
			RecalcLayout();
			break;
		case UndoRecord::Type::Delete:
			m_Buffer->Delete(rec.Offset, rec.OldData.size());
			m_Modified.erase(m_Modified.begin() + rec.Offset,
				m_Modified.begin() + rec.Offset + rec.OldData.size());
			RecalcLayout();
			break;
		case UndoRecord::Type::Compound:
			for (auto& child : rec.Children)
				ApplyRedo(child);
			break;
	}
}

LRESULT CHexControl::OnMouseMove(UINT, WPARAM wp, LPARAM lParam, BOOL&) {
	if (GetCapture() != m_hWnd)
		return 0;

	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

	bool boxSelect = (wp & MK_CONTROL) || (::GetKeyState(VK_MENU) & 0x80);
	if (m_SelectionFromAscii) {
		int col = (pt.x + GetHScrollX() - GetAsciiStartX()) / m_CharWidth;
		col = std::max(0, std::min(col, (int)m_BytesPerLine - 1));
		auto line = std::max<int>(0, pt.y - GetRulerHeight()) / m_CharHeight;
		auto offset = m_StartOffset + line * m_BytesPerLine + col;
		offset = std::max((int64_t)0, std::min(offset, m_Buffer->GetSize() - 1));
		if (boxSelect) {
			int anchorRow = (int)(m_CaretOffset / m_BytesPerLine), anchorCol = (int)(m_CaretOffset % m_BytesPerLine);
			int curRow = (int)(offset / m_BytesPerLine), curCol = (int)(offset % m_BytesPerLine);
			m_Selection.SetBox(
				(int64_t)std::min(anchorRow, curRow) * m_BytesPerLine + std::min(anchorCol, curCol),
				m_BytesPerLine,
				abs(curCol - anchorCol) + 1,
				abs(curRow - anchorRow) + 1);
		}
		else {
			m_Selection.SetSimple(std::min(offset, m_CaretOffset), abs(offset - m_CaretOffset));
		}
	}
	else {
		auto offset = GetOffsetFromPoint(pt);
		if (offset < 0)
			return 0;
		if (offset > m_EndOffset)
			offset = m_EndOffset;

		if (boxSelect) {
			int anchorRow = (int)(m_CaretOffset / m_BytesPerLine), anchorCol = (int)(m_CaretOffset % m_BytesPerLine);
			int curRow = (int)(offset / m_BytesPerLine), curCol = (int)(offset % m_BytesPerLine);
			m_Selection.SetBox(
				(int64_t)std::min(anchorRow, curRow) * m_BytesPerLine + std::min(anchorCol, curCol),
				m_BytesPerLine,
				abs(curCol - anchorCol) + m_DataSize,
				abs(curRow - anchorRow) + 1);
		}
		else {
			auto maxoffset = std::max(offset, m_CaretOffset);
			auto minoffset = std::min(offset, m_CaretOffset);
			m_Selection.SetSimple(minoffset, abs(offset - m_CaretOffset));
		}
	}
	SendSelectionChanged();
	RedrawWindow();

	return 0;
}

LRESULT CHexControl::OnLeftButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	ReleaseCapture();
	return 0;
}

LRESULT CHexControl::OnLeftButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (!m_Buffer)
		return 0;

	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

	if (m_SelectionFromAscii) {
		// In ASCII region: select a run of printable characters (word boundary)
		int64_t offset = GetAsciiOffsetFromPoint(pt);
		if (offset < 0)
			return 0;
		uint8_t ch;
		m_Buffer->GetData(offset, &ch, 1);
		bool isPrint = ch >= 0x20 && ch <= 0x7e;

		int64_t start = offset, end = offset;
		if (isPrint) {
			while (start > 0) {
				m_Buffer->GetData(start - 1, &ch, 1);
				if (ch < 0x20 || ch > 0x7e) break;
				start--;
			}
			while (end + 1 < m_Buffer->GetSize()) {
				m_Buffer->GetData(end + 1, &ch, 1);
				if (ch < 0x20 || ch > 0x7e) break;
				end++;
			}
		}
		SetSelection(start, end - start + 1);
	}
	else {
		// In hex region: select the data-size unit under the cursor
		int64_t offset = GetOffsetFromPoint(pt);
		if (offset < 0)
			return 0;
		SetSelection(offset, m_DataSize);
	}
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

	if (wParam < 32)	// control character — already handled in OnKeyDown
		return 0;

	// ASCII region: write printable characters directly as bytes
	if (m_SelectionFromAscii) {
		if (wParam > 0x7e) {
			::MessageBeep((UINT)-1);
			return 0;
		}
		BYTE buffer[8] = { 0 };
		m_Buffer->GetData(m_CaretOffset, buffer, m_DataSize);
		m_OldValue = *(uint64_t*)buffer;
		uint64_t newVal = (m_OldValue & ~0xFFULL) | (uint8_t)wParam;
		CommitValue(m_CaretOffset, newVal);
		NMHexControlValueChanged nv{};
		nv.Offset = m_CaretOffset;
		nv.OldValue = m_OldValue;
		nv.NewValue = newVal;
		nv.DataSize = m_DataSize;
		SendNotify(nv, NMHX_VALUE_CHANGED);
		auto oldCaret = m_CaretOffset;
		m_CaretOffset = std::min(m_CaretOffset + 1, m_Buffer->GetSize() - 1);
		if (m_InsertMode)
			RecalcLayout();
		RedrawWindow();
		SendCaretChanged(oldCaret);
		return 0;
	}

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
		BYTE buffer[8] = { 0 };
		m_Buffer->GetData(m_CaretOffset, buffer, m_DataSize);
		m_OldValue = *(uint64_t*)buffer;
	}

	m_CurrentInput = (m_CurrentInput << 4) | value;
	m_EditDigits++;

	if (m_EditDigits == m_DataSize * 2) {
		auto oldInput = m_CurrentInput;
		CommitValue(m_CaretOffset, m_CurrentInput);	// also calls ResetInput()

		NMHexControlValueChanged nv{};
		nv.Offset = m_CaretOffset;
		nv.OldValue = m_OldValue;
		nv.NewValue = oldInput;
		nv.DataSize = m_DataSize;
		SendNotify(nv, NMHX_VALUE_CHANGED);

		auto oldCaret = m_CaretOffset;
		m_CaretOffset = std::min(m_CaretOffset + m_DataSize, m_Buffer->GetSize() - m_DataSize);
		if (m_InsertMode)
			RecalcLayout();
		RedrawWindow();
		SendCaretChanged(oldCaret);
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
	m_Modified.assign(mgr ? (size_t)mgr->GetSize() : 0, false);
	ClearUndoHistory();
	ClearSelection();
	RecalcLayout();
	Invalidate();
	NMHexControlNotify n{};
	SendNotify(n, NMHX_BUFFER_CHANGED);
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

void CHexControl::SetInsertMode(bool insert) {
	m_InsertMode = insert;
	DestroyCaret();
	CreateSolidCaret(m_InsertMode ? 2 : m_CharWidth, m_CharHeight);
	UpdateCaret();
}

bool CHexControl::GetInsertMode() const {
	return m_InsertMode;
}

void CHexControl::SetSize(int64_t size) {}

bool CHexControl::SetDataSize(int32_t size) {
	if (size == 0 || (size & (size - 1)) != 0 || size > 8)
		return false;

	NMHexControlDataSizeChanged n{};
	n.OldDataSize = m_DataSize;
	n.NewDataSize = size;
	m_DataSize = size;
	RecalcLayout();
	RedrawWindow();
	SendNotify(n, NMHX_DATA_SIZE_CHANGED);
	return true;
}

int32_t CHexControl::GetDataSize() const {
	return m_DataSize;
}

bool CHexControl::SetBytesPerLine(int32_t bytesPerLine) {
	if (bytesPerLine % 8 != 0 || bytesPerLine == 0 || bytesPerLine > 256)
		return false;

	NMHexControlBytesPerLineChanged n{};
	n.OldBytesPerLine = m_BytesPerLine;
	n.NewBytesPerLine = bytesPerLine;
	m_BytesPerLine = bytesPerLine;
	RecalcLayout();
	RedrawWindow();
	SendNotify(n, NMHX_BPL_CHANGED);
	return true;
}

int32_t CHexControl::GetBytesPerLine() const {
	return m_BytesPerLine;
}

static int ValueDigits(int dataSize, int base) {
	switch (base) {
		case 2:  return dataSize * 8;
		case 8:  return (dataSize * 8 + 2) / 3;
		case 10:
			switch (dataSize) { case 1: return 3; case 2: return 5; case 4: return 10; default: return 20; }
		default: return dataSize * 2;
	}
}

static std::wstring FormatValueInBase(uint64_t value, int dataSize, int base) {
	WCHAR buf[65]{};
	int width = ValueDigits(dataSize, base);
	switch (base) {
		case 2:
			for (int i = 0; i < width; ++i)
				buf[width - 1 - i] = (value >> i) & 1 ? L'1' : L'0';
			break;
		case 8:
			::StringCchPrintf(buf, _countof(buf), L"%0*llo", width, value);
			break;
		case 10:
			::StringCchPrintf(buf, _countof(buf), L"%0*llu", width, value);
			break;
		default:
			::StringCchPrintf(buf, _countof(buf), L"%0*llX", width, value);
			break;
	}
	return buf;
}

bool CHexControl::Copy(int64_t offset, int64_t size, int base) const {
	auto readValue = [&](int64_t pos) {
		uint64_t value = 0;
		m_Buffer->GetData(pos, (uint8_t*)&value, m_DataSize);
		if (m_BigEndian && m_DataSize > 1) {
			uint64_t swapped = 0;
			for (uint32_t s = 0; s < m_DataSize; s++)
				((uint8_t*)&swapped)[m_DataSize - 1 - s] = ((uint8_t*)&value)[s];
			value = swapped;
		}
		return value;
	};

	if (m_Selection.GetSelectionType() == SelectionType::Box && offset < 0) {
		auto boxOffset = m_Selection.GetOffset();
		if (boxOffset < 0)
			return false;
		int width = m_Selection.GetWidth();
		int height = m_Selection.GetHeight();
		int bpl = m_Selection.GetBytesPerLine();
		std::wstring text;
		for (int row = 0; row < height; row++) {
			for (int col = 0; col < width; col += m_DataSize) {
				text += FormatValueInBase(readValue(boxOffset + row * bpl + col), m_DataSize, base);
				text += L' ';
			}
			if (!text.empty() && text.back() == L' ')
				text.back() = L'\n';
		}
		return CopyText(text.c_str());
	}

	if (offset < 0)
		offset = m_Selection.GetOffset();
	if (size == 0)
		size = m_Selection.GetLength();
	if (size == 0 || offset < 0)
		return false;

	std::wstring text;
	for (int64_t i = 0; i < size; i += m_DataSize) {
		text += FormatValueInBase(readValue(offset + i), m_DataSize, base);
		text += L' ';
	}
	if (!text.empty())
		text.pop_back();
	return CopyText(text.c_str());
}

bool CHexControl::Paste(int64_t offset) {
	if (!CanPaste())
		return false;
	if (offset < 0)
		offset = m_CaretOffset;

	if (!::OpenClipboard(m_hWnd))
		return false;

	bool result = false;
	auto hData = ::GetClipboardData(CF_UNICODETEXT);
	if (hData) {
		auto text = static_cast<PCWSTR>(::GlobalLock(hData));
		if (text) {
			std::vector<uint8_t> bytes;
			PCWSTR p = text;
			while (*p) {
				while (*p == L' ' || *p == L'\n' || *p == L'\r' || *p == L'\t')
					p++;
				if (!*p)
					break;
				WCHAR token[17] = {};
				uint32_t n = 0;
				while (n < m_DataSize * 2 && iswxdigit(*p))
					token[n++] = *p++;
				if (n == 0)
					break;
				uint64_t value = wcstoull(token, nullptr, 16);
				for (uint32_t i = 0; i < m_DataSize; i++)
					bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
			}
			::GlobalUnlock(hData);

			if (!bytes.empty()) {
				auto bufSize = m_Buffer->GetSize();
				UndoRecord rec;
				rec.Offset = offset;
				if (m_InsertMode) {
					rec.Op = UndoRecord::Type::Insert;
					rec.NewData = bytes;
					m_Buffer->Insert(offset, bytes.data(), (uint32_t)bytes.size());
					m_Modified.insert(m_Modified.begin() + offset, bytes.size(), true);
					RecalcLayout();
				}
				else {
					auto count = (uint32_t)std::min((int64_t)bytes.size(), bufSize - offset);
					rec.Op = UndoRecord::Type::Overwrite;
					rec.OldData = ReadBytes(offset, count);
					rec.OldModified = ReadModified(offset, count);
					rec.NewData.assign(bytes.begin(), bytes.begin() + count);
					m_Buffer->SetData(offset, bytes.data(), count);
					if ((int64_t)m_Modified.size() < bufSize)
						m_Modified.resize((size_t)bufSize, false);
					for (uint32_t i = 0; i < count; i++)
						m_Modified[offset + i] = true;
				}
				PushUndo(std::move(rec));
				RedrawWindow();
				result = true;
			}
		}
	}
	::CloseClipboard();
	return result;
}

bool CHexControl::CanCopy() const {
	return HasSelection();
}

bool CHexControl::CanPaste() const {
	return !IsReadOnly() && ::IsClipboardFormatAvailable(CF_UNICODETEXT);
}

bool CHexControl::Cut() {
	if (!CanCut())
		return false;
	if (!Copy())
		return false;
	return Delete();
}

bool CHexControl::Delete() {
	if (!CanDelete())
		return false;

	if (m_Selection.GetSelectionType() == SelectionType::Box) {
		int width = m_Selection.GetWidth();
		int height = m_Selection.GetHeight();
		int bpl = m_Selection.GetBytesPerLine();
		auto base = m_Selection.GetOffset();

		if (m_InsertMode) {
			// Remove each row's bytes; iterate bottom-up so offsets stay valid
			UndoRecord compound;
			compound.Op = UndoRecord::Type::Compound;
			for (int row = height - 1; row >= 0; row--) {
				int64_t rowOffset = base + row * bpl;
				UndoRecord rowRec;
				rowRec.Op = UndoRecord::Type::Delete;
				rowRec.Offset = rowOffset;
				rowRec.OldData = ReadBytes(rowOffset, width);
				rowRec.OldModified = ReadModified(rowOffset, width);
				m_Buffer->Delete(rowOffset, width);
				m_Modified.erase(m_Modified.begin() + rowOffset,
					m_Modified.begin() + rowOffset + width);
				compound.Children.insert(compound.Children.begin(), std::move(rowRec));
			}
			PushUndo(std::move(compound));
			RecalcLayout();
		}
		else {
			// Overwrite mode: zero the selected columns in each row
			uint8_t zero = 0;
			FillSelection(&zero, 1);
			// FillSelection already pushed undo
		}
	}
	else {
		int64_t selOffset = m_Selection.GetOffset();
		int64_t selLength = m_Selection.GetLength();

		if (m_InsertMode) {
			UndoRecord rec;
			rec.Op = UndoRecord::Type::Delete;
			rec.Offset = selOffset;
			rec.OldData = ReadBytes(selOffset, selLength);
			rec.OldModified = ReadModified(selOffset, selLength);
			m_Buffer->Delete(selOffset, (size_t)selLength);
			m_Modified.erase(m_Modified.begin() + selOffset,
				m_Modified.begin() + selOffset + selLength);
			PushUndo(std::move(rec));
			m_CaretOffset = std::min(m_CaretOffset, m_Buffer->GetSize() - m_DataSize);
			RecalcLayout();
		}
		else {
			// Overwrite mode: zero the selected range
			uint8_t zero = 0;
			Fill(selOffset, &zero, 1, (uint32_t)selLength);
			// Fill already pushed undo
		}
	}

	ClearSelection();
	RedrawWindow();
	return true;
}

bool CHexControl::CanCut() const {
	return CanDelete();
}

bool CHexControl::CanDelete() const {
	return !IsReadOnly() && !m_Selection.IsEmpty();
}

int64_t CHexControl::GetCaretOffset() const {
	return m_CaretOffset;
}

int64_t CHexControl::GetSelectionOffset() const {
	return m_Selection.GetOffset();
}

int64_t CHexControl::GetSelectionLength() const {
	return m_Selection.GetLength();
}

SelectionType CHexControl::GetSelectionType() const {
	return m_Selection.GetSelectionType();
}

void CHexControl::SetSelection(int64_t offset, int64_t length) {
	if (!m_Buffer || offset < 0 || length < 0)
		return;
	offset = std::min(offset, m_Buffer->GetSize() - 1);
	length = std::min(length, m_Buffer->GetSize() - offset);
	m_Selection.SetSimple(offset, length);
	if (length > 0)
		m_CaretOffset = NormalizeOffset(offset + length - 1);
	SendSelectionChanged();
	RedrawWindow();
}

void CHexControl::SetBoxSelection(int64_t offset, int width, int height) {
	if (!m_Buffer || offset < 0 || width <= 0 || height <= 0)
		return;
	m_Selection.SetBox(offset, m_BytesPerLine, width, height);
	m_CaretOffset = NormalizeOffset(offset);
	SendSelectionChanged();
	RedrawWindow();
}

uint32_t CHexControl::FillSelection(const uint8_t* pattern, uint32_t patternSize) {
	if (!m_Buffer || m_ReadOnly || !pattern || patternSize == 0 || m_Selection.IsEmpty())
		return 0;

	if (m_Selection.GetSelectionType() == SelectionType::Box) {
		int width = m_Selection.GetWidth();
		int height = m_Selection.GetHeight();
		int bpl = m_Selection.GetBytesPerLine();
		auto base = m_Selection.GetOffset();

		UndoRecord compound;
		compound.Op = UndoRecord::Type::Compound;
		uint32_t patPos = 0;
		uint32_t total = 0;
		for (int row = 0; row < height; row++) {
			int64_t rowOffset = base + row * bpl;
			UndoRecord rowRec;
			rowRec.Op = UndoRecord::Type::Overwrite;
			rowRec.Offset = rowOffset;
			rowRec.OldData = ReadBytes(rowOffset, width);
			rowRec.OldModified = ReadModified(rowOffset, width);
			rowRec.NewData.resize(width);
			for (int col = 0; col < width; col++)
				rowRec.NewData[col] = pattern[patPos++ % patternSize];
			m_Buffer->SetData(rowOffset, rowRec.NewData.data(), width);
			if ((int64_t)m_Modified.size() < rowOffset + width)
				m_Modified.resize((size_t)(rowOffset + width), false);
			for (int col = 0; col < width; col++)
				m_Modified[rowOffset + col] = true;
			compound.Children.push_back(std::move(rowRec));
			total += width;
		}
		PushUndo(std::move(compound));
		RedrawWindow();
		return total;
	}

	return Fill(m_Selection.GetOffset(), pattern, patternSize, (uint32_t)m_Selection.GetLength());
}

uint32_t CHexControl::Fill(int64_t offset, const uint8_t* pattern, uint32_t patternSize, uint32_t count) {
	if (!m_Buffer || m_ReadOnly || !pattern || patternSize == 0 || count == 0)
		return 0;
	auto bufSize = m_Buffer->GetSize();
	if (offset < 0 || offset >= bufSize)
		return 0;
	count = (uint32_t)std::min((int64_t)count, bufSize - offset);

	UndoRecord rec;
	rec.Op = UndoRecord::Type::Overwrite;
	rec.Offset = offset;
	rec.OldData = ReadBytes(offset, count);
	rec.OldModified = ReadModified(offset, count);
	rec.NewData.resize(count);
	for (uint32_t i = 0; i < count; i++)
		rec.NewData[i] = pattern[i % patternSize];

	m_Buffer->SetData(offset, rec.NewData.data(), count);
	if ((int64_t)m_Modified.size() < bufSize)
		m_Modified.resize((size_t)bufSize, false);
	for (uint32_t i = 0; i < count; i++)
		m_Modified[offset + i] = true;

	PushUndo(std::move(rec));
	RedrawWindow();
	return count;
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

void CHexControl::SetRuler(bool show) {
	if (m_ShowRuler == show)
		return;
	m_ShowRuler = show;
	RecalcLayout();
	RedrawWindow();
}

bool CHexControl::GetRuler() const {
	return m_ShowRuler;
}

void CHexControl::SetFont(PCWSTR faceName, int pointSizeTenths) {
	if (faceName)
		::StringCchCopy(m_FontFaceName, LF_FACESIZE, faceName);
	if (pointSizeTenths > 0)
		m_FontPointSize = pointSizeTenths;
	if (m_hWnd) {
		InitFontMetrics();
		DestroyCaret();
		CreateSolidCaret(m_InsertMode ? 2 : m_CharWidth, m_CharHeight);
		RecalcLayout();
		RedrawWindow();
	}
}

void CHexControl::SetBigEndian(bool bigEndian) {
	if (m_BigEndian == bigEndian)
		return;
	m_BigEndian = bigEndian;
	RedrawWindow();
}

bool CHexControl::GetBigEndian() const {
	return m_BigEndian;
}

void CHexControl::SetMaxUndoLevels(size_t maxLevels) {
	m_MaxUndoLevels = maxLevels;
	if (m_UndoStack.size() > maxLevels)
		m_UndoStack.erase(m_UndoStack.begin(), m_UndoStack.begin() + (m_UndoStack.size() - maxLevels));
}

size_t CHexControl::GetMaxUndoLevels() const {
	return m_MaxUndoLevels;
}

void CHexControl::SetColumnSeparator(uint32_t everyNBytes) {
	m_ColSeparator = everyNBytes;
	RecalcLayout();
	RedrawWindow();
}

void CHexControl::SetAddressDecimal(bool decimal) {
	if (m_DecimalAddresses == decimal)
		return;
	m_DecimalAddresses = decimal;
	RecalcLayout();
	RedrawWindow();
}

bool CHexControl::GetAddressDecimal() const {
	return m_DecimalAddresses;
}

int CHexControl::AddHighlight(int64_t offset, int64_t length, COLORREF textColor, COLORREF bkColor) {
	if (length <= 0)
		return -1;
	int id = m_NextHighlightId++;
	HexHighlight hl{};
	hl.Offset = offset; hl.Length = length;
	hl.TextColor = textColor; hl.BkColor = bkColor; hl.Id = id;
	m_Highlights.push_back(hl);
	RedrawWindow();
	return id;
}

bool CHexControl::RemoveHighlight(int id) {
	auto it = std::find_if(m_Highlights.begin(), m_Highlights.end(),
		[id](const HexHighlight& h) { return h.Id == id; });
	if (it == m_Highlights.end())
		return false;
	m_Highlights.erase(it);
	RedrawWindow();
	return true;
}

void CHexControl::ClearHighlights() {
	if (m_Highlights.empty())
		return;
	m_Highlights.clear();
	RedrawWindow();
}

const std::vector<HexHighlight>& CHexControl::GetHighlights() const {
	return m_Highlights;
}

const HexHighlight* CHexControl::GetHighlightAt(int64_t offset) const {
	for (auto& h : m_Highlights)
		if (offset >= h.Offset && offset < h.Offset + h.Length)
			return &h;
	return nullptr;
}

void CHexControl::GotoOffset(int64_t offset, bool scrollIntoView) {
	if (!m_Buffer || m_Buffer->GetSize() == 0)
		return;

	auto oldCaret = m_CaretOffset;
	offset = std::max((int64_t)0, std::min(offset, m_Buffer->GetSize() - m_DataSize));
	m_CaretOffset = NormalizeOffset(offset);

	if (scrollIntoView) {
		int64_t endOffset = m_StartOffset + (int64_t)m_Lines * m_BytesPerLine;
		if (m_CaretOffset < m_StartOffset || m_CaretOffset >= endOffset) {
			// Center the target line vertically in the view
			int64_t targetLine = m_CaretOffset / m_BytesPerLine;
			int64_t newStartLine = targetLine - m_Lines / 2;
			if (newStartLine < 0)
				newStartLine = 0;
			m_StartOffset = newStartLine * m_BytesPerLine;

			SCROLLINFO si;
			si.cbSize = sizeof(si);
			si.fMask = SIF_POS;
			si.nPos = (int)(m_StartOffset / m_BytesPerLine);
			SetScrollInfo(SB_VERT, &si);

			RedrawWindow();
			SendCaretChanged(oldCaret);
			return;
		}
	}
	UpdateCaret();
	SendCaretChanged(oldCaret);
}

std::wstring CHexControl::GetText(int64_t offset, int64_t size) const {
	return std::wstring();
}

void CHexControl::Refresh() {
	RecalcLayout();
	RedrawWindow();
}

bool CHexControl::IsModified(int64_t offset) const {
	return offset >= 0 && offset < (int64_t)m_Modified.size() && m_Modified[offset];
}

bool CHexControl::IsModified() const {
	return std::any_of(m_Modified.begin(), m_Modified.end(), [](bool b) { return b; });
}

bool CHexControl::DeleteState(int64_t offset) {
	if (offset < 0 || offset >= (int64_t)m_Modified.size())
		return false;
	return m_Modified[offset] = false;
}

void CHexControl::SetOptions(HexControlOptions options) {
	m_Options = options;
}

HexControlOptions CHexControl::GetOptions() const {
	return m_Options;
}

bool CHexControl::CanUndo() const {
	return IsUndoRedoEnabled() && !m_UndoStack.empty();
}

bool CHexControl::CanRedo() const {
	return IsUndoRedoEnabled() && !m_RedoStack.empty();
}

void CHexControl::ClearUndoHistory() {
	m_UndoStack.clear();
	m_RedoStack.clear();
}

bool CHexControl::IsUndoRedoEnabled() const noexcept {
	return (m_Options & HexControlOptions::DisableUndoRedo) == HexControlOptions::None;
}

int64_t CHexControl::Find(const uint8_t* pattern, uint32_t patternSize, int64_t startOffset, bool forward) {
	if (!m_Buffer || !pattern || patternSize == 0)
		return -1;

	m_FindPattern.assign(pattern, pattern + patternSize);
	m_FindForward = forward;

	auto bufSize = m_Buffer->GetSize();
	if (bufSize < patternSize)
		return -1;

	constexpr uint32_t chunkSize = 4096;
	std::vector<uint8_t> chunk(chunkSize + patternSize - 1);

	auto searchForward = [&](int64_t from) -> int64_t {
		int64_t limit = bufSize - patternSize;
		for (int64_t base = from; base <= limit; ) {
			uint32_t read = m_Buffer->GetData(base, chunk.data(), (uint32_t)std::min((int64_t)chunk.size(), bufSize - base));
			if (read == 0) break;
			for (uint32_t i = 0; i + patternSize <= read && base + i <= limit; i++) {
				if (memcmp(chunk.data() + i, pattern, patternSize) == 0)
					return base + i;
			}
			base += std::max((int64_t)1, (int64_t)(read - patternSize + 1));
		}
		return -1;
	};

	auto searchBackward = [&](int64_t from) -> int64_t {
		int64_t limit = bufSize - patternSize;
		from = std::min(from, limit);
		for (int64_t base = from; base >= 0; ) {
			int64_t chunkStart = std::max((int64_t)0, base - (int64_t)chunkSize + 1);
			uint32_t read = m_Buffer->GetData(chunkStart, chunk.data(), (uint32_t)(base - chunkStart + patternSize));
			if (read == 0) break;
			for (int64_t i = (int64_t)read - patternSize; i >= 0; i--) {
				if (memcmp(chunk.data() + i, pattern, patternSize) == 0)
					return chunkStart + i;
			}
			base = chunkStart - 1;
		}
		return -1;
	};

	int64_t found = forward ? searchForward(startOffset) : searchBackward(startOffset);
	if (found < 0) {
		NMHexControlNotify n{};
		SendNotify(n, NMHX_FIND_NOT_FOUND);
		return -1;
	}

	SetSelection(found, patternSize);
	GotoOffset(found);
	return found;
}

int64_t CHexControl::FindNext() {
	if (m_FindPattern.empty())
		return -1;
	int64_t start = m_FindForward
		? m_Selection.GetOffset() + 1
		: m_Selection.GetOffset() - 1;
	return Find(m_FindPattern.data(), (uint32_t)m_FindPattern.size(), start, m_FindForward);
}

int64_t CHexControl::FindPrev() {
	if (m_FindPattern.empty())
		return -1;
	int64_t start = m_FindForward
		? m_Selection.GetOffset() - 1
		: m_Selection.GetOffset() + 1;
	return Find(m_FindPattern.data(), (uint32_t)m_FindPattern.size(), start, !m_FindForward);
}

bool CHexControl::Undo() {
	if (!CanUndo())
		return false;
	auto rec = std::move(m_UndoStack.back());
	m_UndoStack.pop_back();
	ApplyUndo(rec);
	m_RedoStack.push_back(std::move(rec));
	RedrawWindow();
	NMHexControlNotify n{};
	SendNotify(n, NMHX_UNDO);
	return true;
}

bool CHexControl::Redo() {
	if (!CanRedo())
		return false;
	auto rec = std::move(m_RedoStack.back());
	m_RedoStack.pop_back();
	ApplyRedo(rec);
	m_UndoStack.push_back(std::move(rec));
	RedrawWindow();
	NMHexControlNotify n{};
	SendNotify(n, NMHX_REDO);
	return true;
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
				::GlobalUnlock(hData);
				::SetClipboardData(CF_UNICODETEXT, hData);
			}
		}
		::CloseClipboard();
		if (hData)
			return true;
	}
	return false;
}

LRESULT CHexControl::OnCopy(UINT, WPARAM, LPARAM, BOOL&) {
	Copy();
	return 0;
}

LRESULT CHexControl::OnCut(UINT, WPARAM, LPARAM, BOOL&) {
	Cut();
	return 0;
}

LRESULT CHexControl::OnPaste(UINT, WPARAM, LPARAM, BOOL&) {
	Paste();
	return 0;
}

std::vector<uint8_t> CHexControl::GetSelectedBytes() const {
	if (!m_Buffer || m_Selection.IsEmpty())
		return {};

	if (m_Selection.GetSelectionType() == SelectionType::Box) {
		auto offset = m_Selection.GetOffset();
		auto width  = m_Selection.GetWidth();
		auto height = m_Selection.GetHeight();
		std::vector<uint8_t> result;
		result.reserve((size_t)width * height);
		for (int row = 0; row < height; ++row) {
			auto rowOffset = offset + (int64_t)row * m_BytesPerLine;
			uint8_t buf[512];
			uint32_t got = m_Buffer->GetData(rowOffset, buf, (uint32_t)std::min((int64_t)width, (int64_t)sizeof(buf)));
			result.insert(result.end(), buf, buf + got);
		}
		return result;
	}

	auto offset = m_Selection.GetOffset();
	auto length = m_Selection.GetLength();
	std::vector<uint8_t> result((size_t)length);
	m_Buffer->GetData(offset, result.data(), (uint32_t)length);
	return result;
}

std::vector<std::pair<int64_t, int64_t>> CHexControl::GetModifiedRanges() const {
	std::vector<std::pair<int64_t, int64_t>> ranges;
	int64_t start = -1;
	for (int64_t i = 0; i < (int64_t)m_Modified.size(); ++i) {
		if (m_Modified[i]) {
			if (start < 0)
				start = i;
		}
		else if (start >= 0) {
			ranges.emplace_back(start, i - start);
			start = -1;
		}
	}
	if (start >= 0)
		ranges.emplace_back(start, (int64_t)m_Modified.size() - start);
	return ranges;
}

bool CHexControl::CopyAsText(int64_t offset, int64_t size) const {
	if (!m_Buffer)
		return false;

	if (offset < 0) {
		if (m_Selection.IsEmpty())
			return false;
		offset = m_Selection.GetOffset();
		if (m_Selection.GetSelectionType() == SelectionType::Box) {
			// build text row by row, rows separated by newlines
			auto width = m_Selection.GetWidth();
			auto height = m_Selection.GetHeight();
			std::wstring result;
			result.reserve((size_t)(width + 2) * height);
			for (int row = 0; row < height; ++row) {
				int64_t rowOffset = offset + (int64_t)row * m_BytesPerLine;
				uint8_t buf[512];
				uint32_t got = m_Buffer->GetData(rowOffset, buf, (uint32_t)std::min((int64_t)width, (int64_t)sizeof(buf)));
				for (uint32_t i = 0; i < got; ++i)
					result += (buf[i] >= 0x20 && buf[i] <= 0x7E) ? (WCHAR)buf[i] : L'.';
				if (row + 1 < height)
					result += L'\n';
			}
			return CopyText(result.c_str());
		}
		size = m_Selection.GetLength();
	}

	if (size <= 0)
		return false;

	std::wstring result;
	result.reserve((size_t)size);
	int64_t remaining = size;
	int64_t pos = offset;
	while (remaining > 0) {
		uint8_t buf[4096];
		uint32_t chunk = (uint32_t)std::min(remaining, (int64_t)sizeof(buf));
		uint32_t got = m_Buffer->GetData(pos, buf, chunk);
		if (got == 0) break;
		for (uint32_t i = 0; i < got; ++i)
			result += (buf[i] >= 0x20 && buf[i] <= 0x7E) ? (WCHAR)buf[i] : L'.';
		pos += got;
		remaining -= got;
	}
	return CopyText(result.c_str());
}

uint64_t CHexControl::GetValueAt(int64_t offset, int32_t size) const {
	if (!m_Buffer || size <= 0 || size > 8)
		return 0;
	uint8_t buf[8]{};
	m_Buffer->GetData(offset, buf, (uint32_t)size);
	if (m_BigEndian) {
		for (int i = 0, j = size - 1; i < j; ++i, --j)
			std::swap(buf[i], buf[j]);
	}
	uint64_t value = 0;
	memcpy(&value, buf, size);
	return value;
}
