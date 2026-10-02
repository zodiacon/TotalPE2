#include "pch.h"
#include "resource.h"
#include "AnimatedCursorView.h"
#include <WTLHelper.h>

//
// CAniPreview
//

void CAniPreview::SetData(AniFile const* ani, std::vector<HICON> const* icons) {
	m_Ani = ani;
	m_Icons = icons;
	m_Step = 0;
	Play(m_Ani && m_Ani->StepCount() > 1);
}

void CAniPreview::StartTimer(UINT milliseconds) {
	if (m_hWnd)
		SetTimer(TimerId, std::max<UINT>(milliseconds, 10));	// the system timer does not go below 10 ms anyway
}

void CAniPreview::Play(bool play) {
	play = play && m_Ani && m_Ani->StepCount() > 1;
	if (m_hWnd)
		KillTimer(TimerId);
	m_Playing = play;
	if (play)
		StartTimer(m_Ani->StepMilliseconds(m_Step));
	if (m_hWnd)
		Invalidate(FALSE);
	if (OnPlayingChanged)
		OnPlayingChanged(play);
}

void CAniPreview::ShowStep(size_t step) {
	if (!m_Ani || step >= m_Ani->StepCount())
		return;
	Play(false);
	m_Step = step;
	if (m_hWnd)
		Invalidate(FALSE);
}

LRESULT CAniPreview::OnTimer(UINT, WPARAM id, LPARAM, BOOL&) {
	if (id != TimerId || !m_Playing || !m_Ani)
		return 0;
	KillTimer(TimerId);
	if (!IsWindowVisible()) {
		// on a tab that is not shown: stay where we are and check again later
		StartTimer(250);
		return 0;
	}
	m_Step = (m_Step + 1) % m_Ani->StepCount();
	Invalidate(FALSE);
	StartTimer(m_Ani->StepMilliseconds(m_Step));
	return 0;
}

LRESULT CAniPreview::OnLeftButtonDown(UINT, WPARAM, LPARAM, BOOL&) {
	SetFocus();
	Play(!m_Playing);
	return 0;
}

LRESULT CAniPreview::OnKeyDown(UINT, WPARAM key, LPARAM, BOOL& handled) {
	if (!m_Ani) {
		handled = FALSE;
		return 0;
	}
	switch (key) {
		case VK_SPACE:
			Play(!m_Playing);
			break;
		case VK_RIGHT:
			ShowStep((m_Step + 1) % m_Ani->StepCount());
			break;
		case VK_LEFT:
			ShowStep((m_Step + m_Ani->StepCount() - 1) % m_Ani->StepCount());
			break;
		default:
			handled = FALSE;
	}
	return 0;
}

LRESULT CAniPreview::OnContextMenu(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (OnContext) {
		CPoint pt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		if (pt.x == -1 && pt.y == -1) {	// from the keyboard
			pt = CPoint(10, 10);
			ClientToScreen(&pt);
		}
		OnContext(pt);
	}
	return 0;
}

LRESULT CAniPreview::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	KillTimer(TimerId);
	handled = FALSE;
	return 0;
}

void CAniPreview::DrawInfo(CDCHandle dc, CRect const& rc) const {
	dc.FillSolidRect(&rc, ::GetSysColor(COLOR_WINDOW));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
	dc.SelectFont(AtlGetDefaultGuiFont());

	std::vector<std::wstring> lines;
	auto& a = *m_Ani;
	if (!a.Title.empty())
		lines.push_back(a.Title + (a.Author.empty() ? L"" : L"  (" + a.Author + L")"));
	else if (!a.Author.empty())
		lines.push_back(L"By " + a.Author);
	auto& f = a.StepFrame(m_Step);
	lines.push_back(std::format(L"{} frame(s), {} step(s), {:.2f} seconds per cycle", a.Frames.size(), a.StepCount(), a.TotalMilliseconds() / 1000.0));
	lines.push_back(std::format(L"Step {} of {}: frame {}, {} ms ({} jiffies)", m_Step + 1, a.StepCount(),
		a.Sequence[m_Step], a.StepMilliseconds(m_Step), a.StepJiffies(m_Step)));
	lines.push_back(std::format(L"{} x {}{}{}", f.Width, f.Height, f.BitCount ? std::format(L", {} bit", f.BitCount) : L"",
		f.IsCursor ? std::format(L", hot spot ({}, {})", f.HotspotX, f.HotspotY) : L""));
	lines.push_back(m_Playing ? L"Playing. Click or press Space to pause, right-click for more." : L"Paused. Click or press Space to play, Left and Right step through.");

	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	int y = rc.top + 8;
	for (auto& line : lines) {
		dc.TextOut(rc.left + 10, y, line.c_str(), (int)line.size());
		y += tm.tmHeight + 3;
	}
}

LRESULT CAniPreview::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	CPaintDC paint(m_hWnd);
	CRect rc;
	GetClientRect(&rc);
	CMemoryDC dc(paint, rc);

	if (!m_Ani || !m_Icons || m_Ani->StepCount() == 0) {
		dc.FillSolidRect(&rc, ::GetSysColor(COLOR_WINDOW));
		return 0;
	}

	CRect info(rc.left, rc.top, rc.right, rc.top + 8 + 5 * 20 + 8);
	DrawInfo(dc.m_hDC, info);

	// a checkerboard behind the images shows their transparency
	CRect area(rc.left, info.bottom, rc.right, rc.bottom);
	const int square = 8;
	COLORREF light = RGB(235, 235, 235), dark = RGB(200, 200, 200);
	for (int y = area.top; y < area.bottom; y += square)
		for (int x = area.left; x < area.right; x += square) {
			CRect cell(x, y, std::min<int>(x + square, area.right), std::min<int>(y + square, area.bottom));
			dc.FillSolidRect(&cell, (((x - area.left) / square + (y - area.top) / square) & 1) ? dark : light);
		}

	auto const& frame = m_Ani->StepFrame(m_Step);
	auto index = m_Ani->Sequence[m_Step];
	if (index >= m_Icons->size() || !(*m_Icons)[index])
		return 0;
	auto icon = (*m_Icons)[index];

	// actual size on the left, magnified on the right
	int w = frame.Width, h = frame.Height;
	int margin = 16;
	::DrawIconEx(dc, area.left + margin, area.top + margin, icon, w, h, 0, nullptr, DI_NORMAL);

	int available = std::max(1, std::min(area.Width() - 2 * margin - w - margin, area.Height() - 2 * margin));
	int zoom = std::max(1, std::min(available / std::max(w, 1), available / std::max(h, 1)));
	if (zoom > 1)
		::DrawIconEx(dc, area.left + 2 * margin + w, area.top + margin, icon, w * zoom, h * zoom, 0, nullptr, DI_NORMAL);
	return 0;
}

//
// CAnimatedCursorView
//

CAnimatedCursorView::CAnimatedCursorView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CAnimatedCursorView::~CAnimatedCursorView() {
	for (auto icon : m_Icons)
		if (icon)
			::DestroyIcon(icon);
}

CString CAnimatedCursorView::GetTitle() const {
	return m_Title;
}

bool CAnimatedCursorView::SetData(std::span<const std::byte> data) {
	if (!AniFile::Parse(data, m_Ani))
		return false;

	m_Raw.assign((const uint8_t*)data.data(), (const uint8_t*)data.data() + data.size());
	for (auto& f : m_Ani.Frames) {
		// the image of the frame is the same data an icon resource holds; a cursor is shown as an icon
		m_Icons.push_back(::CreateIconFromResourceEx(f.Data.data() + f.ImageOffset, f.ImageSize, TRUE, 0x00030000,
			f.Width, f.Height, LR_DEFAULTCOLOR));
	}
	FillList();
	m_Preview.SetData(&m_Ani, &m_Icons);
	return true;
}

LRESULT CAnimatedCursorView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Preview.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.InsertColumn(0, L"Step", LVCFMT_RIGHT, 50);
	m_List.InsertColumn(1, L"Frame", LVCFMT_RIGHT, 55);
	m_List.InsertColumn(2, L"Time (ms)", LVCFMT_RIGHT, 75);
	m_List.InsertColumn(3, L"Size", LVCFMT_LEFT, 80);
	m_List.InsertColumn(4, L"Bits", LVCFMT_RIGHT, 45);
	m_List.InsertColumn(5, L"Hot Spot", LVCFMT_LEFT, 80);

	m_Splitter.SetSplitterPanes(m_Preview, m_List);
	m_Splitter.SetSplitterPosPct(62);

	m_Preview.OnContext = [this](CPoint pt) { ShowContextMenu(pt); };
	return 0;
}

void CAnimatedCursorView::FillList() {
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (size_t step = 0; step < m_Ani.StepCount(); step++) {
		auto& f = m_Ani.StepFrame(step);
		auto row = m_List.InsertItem((int)step, std::to_wstring(step + 1).c_str());
		m_List.SetItemText(row, 1, std::to_wstring(m_Ani.Sequence[step]).c_str());
		m_List.SetItemText(row, 2, std::to_wstring(m_Ani.StepMilliseconds(step)).c_str());
		m_List.SetItemText(row, 3, std::format(L"{} x {}", f.Width, f.Height).c_str());
		m_List.SetItemText(row, 4, f.BitCount ? std::to_wstring(f.BitCount).c_str() : L"");
		m_List.SetItemText(row, 5, f.IsCursor ? std::format(L"({}, {})", f.HotspotX, f.HotspotY).c_str() : L"");
	}
	m_List.SetRedraw(TRUE);
}

// Selecting a step in the list does not change the picture: only a double-click (or "Show This Frame" in the context menu) does.
LRESULT CAnimatedCursorView::OnListDoubleClick(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}
	auto activate = (NMITEMACTIVATE*)hdr;
	if (activate->iItem >= 0)
		m_Preview.ShowStep(activate->iItem);
	return 0;
}

// Right-clicking a step offers what can be done with that step's frame.
LRESULT CAnimatedCursorView::OnListRightClick(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}
	auto activate = (NMITEMACTIVATE*)hdr;
	int row = activate->iItem;
	if (row < 0 || row >= (int)m_Ani.StepCount())
		return 0;
	m_List.SelectItem(row);

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_ANI_SHOW_FRAME, L"&Show This Frame");
	menu.AppendMenu(MF_STRING, ID_ANI_EXPORT_FRAME, L"&Export Frame...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_ANI_EXPORT, L"Export &Animation (.ani)...");

	CPoint pt;
	::GetCursorPos(&pt);
	auto cmd = (int)Frame()->ShowContextMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y);
	switch (cmd) {
		case ID_ANI_SHOW_FRAME: m_Preview.ShowStep(row); break;
		case ID_ANI_EXPORT_FRAME: ExportFrame(row); break;
		case ID_ANI_EXPORT: ExportAni(); break;
	}
	return 0;
}

void CAnimatedCursorView::ShowContextMenu(CPoint screen) {
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_ANI_PLAYPAUSE, m_Preview.IsPlaying() ? L"&Pause" : L"&Play");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_ANI_EXPORT, L"&Export Animation (.ani)...");
	menu.AppendMenu(MF_STRING, ID_ANI_EXPORT_FRAME, L"Export Current &Frame...");

	auto cmd = (int)Frame()->ShowContextMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y);
	switch (cmd) {
		case ID_ANI_PLAYPAUSE: m_Preview.Play(!m_Preview.IsPlaying()); break;
		case ID_ANI_EXPORT: ExportAni(); break;
		case ID_ANI_EXPORT_FRAME: ExportFrame(m_Preview.CurrentStep()); break;
	}
}

static bool SaveBytes(PCWSTR path, const void* data, size_t size) {
	wil::unique_hfile file(::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	DWORD written = 0;
	return file && ::WriteFile(file.get(), data, (DWORD)size, &written, nullptr) && written == size;
}

void CAnimatedCursorView::ExportAni() {
	CSimpleFileDialog dlg(FALSE, L"ani", nullptr, OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT,
		L"Animated Cursors (*.ani)\0*.ani\0All Files\0*.*\0", m_hWnd);
	WTLHelper::SuspendHook();
	auto ok = IDOK == dlg.DoModal();
	WTLHelper::ResumeHook();
	if (ok && !SaveBytes(dlg.m_szFileName, m_Raw.data(), m_Raw.size()))
		AtlMessageBox(m_hWnd, L"Failed to save the file", IDR_MAINFRAME, MB_ICONERROR);
}

void CAnimatedCursorView::ExportFrame(size_t step) {
	if (step >= m_Ani.StepCount())
		return;
	auto& frame = m_Ani.StepFrame(step);
	CSimpleFileDialog dlg(FALSE, frame.IsCursor ? L"cur" : L"ico", nullptr, OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT,
		frame.IsCursor ? L"Cursors (*.cur)\0*.cur\0All Files\0*.*\0" : L"Icons (*.ico)\0*.ico\0All Files\0*.*\0", m_hWnd);
	WTLHelper::SuspendHook();
	auto ok = IDOK == dlg.DoModal();
	WTLHelper::ResumeHook();
	if (ok && !SaveBytes(dlg.m_szFileName, frame.Data.data(), frame.Data.size()))
		AtlMessageBox(m_hWnd, L"Failed to save the file", IDR_MAINFRAME, MB_ICONERROR);
}
