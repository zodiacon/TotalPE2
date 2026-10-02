#include "pch.h"
#include "HexView.h"
#include "PEFile.h"
#include "MemoryBuffer.h"
#include "HexFormat.h"
#include "ClipboardHelper.h"
#include <numeric>
#include <ToolbarHelper.h>
#include <WTLHelper.h>

CHexView::CHexView(IMainFrame* frame, CString const& title) : CViewBase(frame), m_Title(title) {
}

void CHexView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_EDIT_COPY, m_Hex.CanCopy());
	ui.UIEnable(ID_ICON_EXPORT, m_Hex.CanCopy());
}

CHexControl& CHexView::Hex() {
	return m_Hex;
}

bool CHexView::SetData(PEFile const& pe, uint32_t offset, uint32_t size) {
	m_Buffer = std::make_unique<MemoryBuffer>(pe.GetData() + offset, size, false);
	m_Hex.SetBufferManager(m_Buffer.get());
	m_Hex.SetBiasOffset(offset);
	m_Bookmarks.clear();
	if (m_PE != &pe) {
		// the bytes are part of the file: the inspector can show the RVA, symbol and region (no coloring)
		m_PE = &pe;
		m_Regions = BuildPERegions(pe);
	}
	RebuildHighlights();
	UpdateInspector();

	return true;
}

bool CHexView::SetData(std::span<const std::byte> data) {
	m_Buffer = std::make_unique<MemoryBuffer>((const uint8_t*)data.data(), (uint32_t)data.size(), false);
	m_Hex.SetBufferManager(m_Buffer.get());
	m_Bookmarks.clear();
	RebuildHighlights();
	UpdateInspector();
	return true;
}

bool CHexView::SetData(PVOID address, uint32_t size, bool copy) {
	m_Buffer = std::make_unique<MemoryBuffer>((const uint8_t*)address, size, copy);
	m_Hex.SetBufferManager(m_Buffer.get());
	m_Bookmarks.clear();
	RebuildHighlights();
	UpdateInspector();
	return true;
}

void CHexView::ClearData() {
	m_Hex.SetBufferManager(nullptr);
	m_Bookmarks.clear();
	m_Hex.ClearHighlights();
	UpdateInspector();
}

int64_t CHexView::GetNavigationPosition() const {
	return m_Hex.GetCaretOffset() + m_Hex.GetBiasOffset();
}

void CHexView::SetNavigationPosition(int64_t position) {
	if (!m_Buffer)
		return;
	auto offset = position - m_Hex.GetBiasOffset();
	if (offset < 0 || offset >= m_Buffer->GetSize())
		return;
	m_Hex.SetSelection(offset, 1);
	m_Hex.GotoOffset(offset);
	m_Hex.SetFocus();
}

void CHexView::SetPE(PEFile const& pe, std::vector<HexRegion> regions) {
	m_PE = &pe;
	m_Regions = std::move(regions);
	m_ColorRegions = true;
	RebuildHighlights();
	UpdateInspector();
}

void CHexView::ShowInspector(bool show) {
	m_InspectorVisible = show;
	if (m_Splitter) {
		m_Splitter.SetSinglePaneMode(show ? SPLIT_PANE_NONE : SPLIT_PANE_LEFT);
		if (show)
			m_Splitter.SetSplitterPosPct(70);
	}
	UISetCheck(ID_HEX_INSPECTOR, show);
	UpdateInspector();
}

LRESULT CHexView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	bHandled = FALSE;
	return 1;
}

LRESULT CHexView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	ToolBarButtonInfo const buttons[] = {
		{ ID_DATASIZE_1BYTE, IDI_NUM1, BTNS_CHECKGROUP },
		{ ID_DATASIZE_2BYTES, IDI_NUM2, BTNS_CHECKGROUP },
		{ ID_DATASIZE_4BYTES, IDI_NUM4, BTNS_CHECKGROUP },
		{ ID_DATASIZE_8BYTES, IDI_NUM8, BTNS_CHECKGROUP },
		{ 0 },
		{ ID_EXPORT, IDI_SAVE, BTNS_BUTTON, L"Export" },
		{ 0 },
		{ ID_BYTES_PER_LINE, 0, BTNS_DROPDOWN | BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT, L"Bytes Per Line" },
		{ ID_HEX_COPYAS, IDI_COPY, BTNS_DROPDOWN | BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT, L"Copy As" },
		{ ID_HEX_BOOKMARKS, IDI_BOOKMARK, BTNS_DROPDOWN | BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT, L"Bookmarks" },
		{ 0 },
		{ ID_HEX_BIGENDIAN, 0, BTNS_CHECK | BTNS_SHOWTEXT, L"Big Endian" },
		{ ID_HEX_INSPECTOR, IDI_LOOK, BTNS_CHECK | BTNS_SHOWTEXT, L"Inspector" },
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 24);

	AddSimpleReBarBand(m_tb);
	UIAddToolBar(m_tb);
	UISetRadioMenuItem(ID_BYTESPERLINE_32, ID_BYTESPERLINE_8, ID_BYTESPERLINE_64);
	UISetCheck(ID_DATASIZE_1BYTE, true);

	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Hex.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
	m_Inspector.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
	m_Inspector.OnRegionClicked = [this](HexInspectorRegion const& region) {
		m_Hex.SetSelection(region.Offset, region.Length);
		m_Hex.GotoOffset(region.Offset);
		m_Hex.SetFocus();
	};
	m_Splitter.SetSplitterPanes(m_Hex, m_Inspector);
	m_Splitter.SetSplitterPosPct(70);
	m_Splitter.SetSinglePaneMode(SPLIT_PANE_LEFT);	// the inspector is hidden until requested
	UpdateColors();

	bHandled = FALSE;
	return 0;
}

LRESULT CHexView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CPoint pt;
	::GetCursorPos(&pt);
	UpdateUI();

	CMenuHandle sub = menu.GetSubMenu(5);
	sub.AppendMenu(MF_SEPARATOR);
	CMenu copyAs;
	copyAs.CreatePopupMenu();
	BuildCopyAsMenu(copyAs.m_hMenu);
	sub.AppendMenu(MF_POPUP, (UINT_PTR)copyAs.Detach(), L"Copy &As");	// the submenu is destroyed with its parent
	sub.AppendMenu(MF_SEPARATOR);
	sub.AppendMenu(MF_STRING, ID_EDIT_FIND, L"&Find...\tCtrl+F");
	sub.AppendMenu(MF_STRING, ID_HEX_BOOKMARK_TOGGLE, L"Toggle &Bookmark\tCtrl+B");
	return Frame()->ShowContextMenu(sub, 0, pt.x, pt.y);
}

LRESULT CHexView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ATLASSERT(m_Hex.HasSelection());
	m_Hex.Copy();

	return 0;
}

LRESULT CHexView::OnChangeDataSize(WORD, WORD id, HWND, BOOL&) {
	m_Hex.SetDataSize(1 << (id - ID_DATASIZE_1BYTE));
	return 0;
}

LRESULT CHexView::OnChangeBytesPerLine(WORD, WORD id, HWND, BOOL&) {
	auto index = id - ID_BYTESPERLINE_8;
	int bytes[] = { 8, 16, 24, 32, 48, 64 };
	m_Hex.SetBytesPerLine(bytes[index]);
	UISetRadioMenuItem(id, ID_BYTESPERLINE_8, ID_BYTESPERLINE_64);

	return 0;
}

LRESULT CHexView::OnSelectionChanged(int, LPNMHDR hdr, BOOL&) {
	UpdateUI();
	UpdateInspector();
	return 0;
}

LRESULT CHexView::OnCaretChanged(int, LPNMHDR, BOOL&) {
	UpdateInspector();
	return 0;
}

LRESULT CHexView::OnSave(WORD, WORD, HWND, BOOL&) {
	CSimpleFileDialog dlg(FALSE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT,
		L"All Files\0*.*\0", m_hWnd);
	WTLHelper::SuspendHook();
	auto ok = IDOK == dlg.DoModal();
	WTLHelper::ResumeHook();
	if (ok) {
		HANDLE hFile = ::CreateFile(dlg.m_szFileName, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr);
		if (hFile == INVALID_HANDLE_VALUE) {
			AtlMessageBox(m_hWnd, L"Failed to create file", IDR_MAINFRAME, MB_ICONERROR);
			return 0;
		}
		DWORD bytes;
		if (!::WriteFile(hFile, m_Buffer->GetRawData(0), (ULONG)m_Buffer->GetSize(), &bytes, nullptr)) {
			AtlMessageBox(m_hWnd, L"Failed to write data", IDR_MAINFRAME, MB_ICONERROR);
		}
		::CloseHandle(hFile);
	}

	return 0;
}

LRESULT CHexView::OnDropDown(int, LPNMHDR hdr, BOOL&) {
	auto button = ((NMTOOLBAR*)hdr)->iItem;
	CMenu menu;
	CMenuHandle popup;
	switch (button) {
		case ID_BYTES_PER_LINE:
			menu.LoadMenu(IDR_CONTEXT);
			popup = menu.GetSubMenu(1);
			break;

		case ID_HEX_COPYAS:
			menu.CreatePopupMenu();
			BuildCopyAsMenu(menu.m_hMenu);
			popup = menu.m_hMenu;
			break;

		case ID_HEX_BOOKMARKS:
			menu.CreatePopupMenu();
			BuildBookmarksMenu(menu.m_hMenu);
			popup = menu.m_hMenu;
			break;

		default:
			return 0;
	}
	auto pt = ToolbarHelper::GetDropdownMenuPoint(hdr->hwndFrom, button);
	auto cmd = (UINT)Frame()->ShowContextMenu(popup, TPM_VERTICAL | TPM_RETURNCMD, pt.x, pt.y);
	if (cmd) {
		LRESULT result;
		ProcessWindowMessage(m_hWnd, WM_COMMAND, cmd, 0, result, 1);
	}
	return 0;
}

LRESULT CHexView::OnUpdateTheme(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateColors();

	return 0;
}

void CHexView::UpdateColors() {
	HexControlColors colors;
	colors.Offset = WTLHelper::IsDarkMode() ? RGB(0, 128, 255) : RGB(0, 0, 128);
	colors.Ascii = WTLHelper::IsDarkMode() ? RGB(255, 192, 0) : RGB(128, 0, 0);
	colors.Ruler = WTLHelper::IsDarkMode() ? RGB(0, 128, 255) : RGB(0, 0, 192);
	m_Hex.GetColors() = colors;
	RebuildHighlights();
	m_Hex.Invalidate();
}

//
// structure regions and bookmarks
//

COLORREF CHexView::RegionTextColor() const {
	return WTLHelper::IsDarkMode() ? RGB(235, 235, 235) : RGB(0, 0, 0);
}

COLORREF CHexView::RegionBackColor(int color) const {
	static const COLORREF palette[HexRegionColorCount] = {
		RGB(255, 205, 210), RGB(224, 224, 224), RGB(255, 224, 178), RGB(187, 222, 251),
		RGB(200, 230, 201), RGB(209, 196, 233), RGB(255, 249, 196), RGB(178, 235, 242),
		RGB(248, 187, 208), RGB(220, 237, 200), RGB(255, 204, 188), RGB(207, 216, 220),
	};
	auto c = palette[(color % HexRegionColorCount + HexRegionColorCount) % HexRegionColorCount];
	if (!WTLHelper::IsDarkMode())
		return c;
	return RGB(GetRValue(c) * 45 / 100, GetGValue(c) * 45 / 100, GetBValue(c) * 45 / 100);
}

std::wstring CHexView::RegionAt(int64_t fileOffset) const {
	// the smallest region wins: a directory inside a section is more specific than the section
	HexRegion const* best = nullptr;
	for (auto& r : m_Regions)
		if (fileOffset >= r.Offset && fileOffset < r.Offset + r.Length && (!best || r.Length < best->Length))
			best = &r;
	return best ? best->Name : std::wstring();
}

void CHexView::RebuildHighlights() {
	if (!m_Hex)
		return;

	m_Hex.ClearHighlights();
	if (!m_Buffer)
		return;

	// the control uses the first highlight that covers a byte, so order matters: bookmarks, then the
	// smallest regions, then the largest
	for (auto offset : m_Bookmarks)
		m_Hex.AddHighlight(offset, 1, RGB(0, 0, 0), RGB(255, 214, 0));

	auto bias = m_Hex.GetBiasOffset();
	auto size = m_Buffer->GetSize();
	std::vector<HexInspectorRegion> listed;
	std::vector<HexRegion const*> ordered;
	for (auto& r : m_Regions) {
		if (!m_ColorRegions)
			break;
		auto start = std::max<int64_t>(r.Offset - bias, 0);
		auto end = std::min<int64_t>(r.Offset - bias + r.Length, size);
		if (end <= start)
			continue;
		ordered.push_back(&r);
		listed.push_back({ start, end - start, start + bias, r.Name, RegionTextColor(), RegionBackColor(r.Color) });
	}
	std::vector<size_t> byLength(ordered.size());
	std::iota(byLength.begin(), byLength.end(), 0);
	std::stable_sort(byLength.begin(), byLength.end(), [&](size_t a, size_t b) { return listed[a].Length < listed[b].Length; });
	for (auto i : byLength)
		m_Hex.AddHighlight(listed[i].Offset, listed[i].Length, listed[i].Text, listed[i].Back);

	std::stable_sort(listed.begin(), listed.end(), [](auto& a, auto& b) { return a.Offset < b.Offset || (a.Offset == b.Offset && a.Length > b.Length); });
	m_Inspector.SetRegions(std::move(listed));
	m_Hex.Invalidate();
}

//
// data inspector
//

LRESULT CHexView::OnToggleInspector(WORD, WORD, HWND, BOOL&) {
	ShowInspector(!m_InspectorVisible);
	return 0;
}

LRESULT CHexView::OnToggleBigEndian(WORD, WORD, HWND, BOOL&) {
	auto bigEndian = !m_Hex.GetBigEndian();
	m_Hex.SetBigEndian(bigEndian);
	UISetCheck(ID_HEX_BIGENDIAN, bigEndian);
	m_Hex.Invalidate();
	UpdateInspector();
	return 0;
}

void CHexView::UpdateInspector() {
	if (!m_InspectorVisible || !m_Inspector)
		return;

	HexInspectorInput input;
	uint8_t bytes[32]{};
	if (m_Buffer) {
		auto caret = m_Hex.GetCaretOffset();
		auto size = m_Buffer->GetSize();
		if (caret >= 0 && caret < size)
			input.Count = m_Buffer->GetData(caret, bytes, (uint32_t)std::min<int64_t>(sizeof(bytes), size - caret));
		input.Data = bytes;
		input.DisplayOffset = caret + m_Hex.GetBiasOffset();
		input.SelectionLength = m_Hex.HasSelection() ? m_Hex.GetSelectionLength() : 0;

		input.HasPE = m_PE != nullptr;
		if (m_PE) {
			auto fileOffset = input.DisplayOffset;
			input.Region = RegionAt(fileOffset);
			DWORD rva;
			if (fileOffset >= 0 && FileOffsetToRva(*m_PE, fileOffset, rva)) {
				input.Rva = std::format(L"0x{:X}", rva);
				input.Symbol = Frame()->ResolveRva(rva);
			}
		}
	}
	input.BigEndian = m_Hex.GetBigEndian();
	m_Inspector.Update(input);
}

//
// Copy As
//

void CHexView::BuildCopyAsMenu(CMenuHandle menu) const {
	UINT flags = MF_STRING | (m_Hex.HasSelection() ? MF_ENABLED : MF_GRAYED);
	menu.AppendMenu(flags, ID_HEX_COPYAS_HEX, L"&Hex String (Spaced)");
	menu.AppendMenu(flags, ID_HEX_COPYAS_HEX_COMPACT, L"He&x String (Compact)");
	menu.AppendMenu(flags, ID_HEX_COPYAS_C, L"&C Array");
	menu.AppendMenu(flags, ID_HEX_COPYAS_CSHARP, L"C# &Byte Array");
	menu.AppendMenu(flags, ID_HEX_COPYAS_PYTHON, L"&Python Bytes");
	menu.AppendMenu(flags, ID_HEX_COPYAS_BASE64, L"Base&64");
}

LRESULT CHexView::OnCopyAs(WORD, WORD id, HWND, BOOL&) {
	auto bytes = m_Hex.GetSelectedBytes();
	if (bytes.empty())
		return 0;

	std::span<const uint8_t> data(bytes);
	std::wstring text;
	switch (id) {
		case ID_HEX_COPYAS_HEX: text = HexFormat::ToHexString(data, true); break;
		case ID_HEX_COPYAS_HEX_COMPACT: text = HexFormat::ToHexString(data, false); break;
		case ID_HEX_COPYAS_C: text = HexFormat::ToCArray(data); break;
		case ID_HEX_COPYAS_CSHARP: text = HexFormat::ToCSharpArray(data); break;
		case ID_HEX_COPYAS_PYTHON: text = HexFormat::ToPythonBytes(data); break;
		case ID_HEX_COPYAS_BASE64: text = HexFormat::ToBase64(data); break;
		default: return 0;
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

//
// bookmarks
//

void CHexView::BuildBookmarksMenu(CMenuHandle menu) const {
	menu.AppendMenu(MF_STRING, ID_HEX_BOOKMARK_TOGGLE, L"&Toggle Bookmark\tCtrl+B");
	UINT flags = MF_STRING | (m_Bookmarks.empty() ? MF_GRAYED : MF_ENABLED);
	menu.AppendMenu(flags, ID_HEX_BOOKMARK_NEXT, L"&Next Bookmark\tF2");
	menu.AppendMenu(flags, ID_HEX_BOOKMARK_PREV, L"&Previous Bookmark\tShift+F2");
	menu.AppendMenu(flags, ID_HEX_BOOKMARK_CLEAR, L"&Clear All Bookmarks");
	if (m_Bookmarks.empty())
		return;

	menu.AppendMenu(MF_SEPARATOR);
	auto bias = m_Hex.GetBiasOffset();
	for (int i = 0; i < (int)m_Bookmarks.size() && i < MaxBookmarkCommands; i++) {
		auto fileOffset = m_Bookmarks[i] + bias;
		auto region = m_PE ? RegionAt(fileOffset) : std::wstring();
		auto text = std::format(L"0x{:X}", fileOffset);
		if (!region.empty())
			text += L"   (" + region + L")";
		menu.AppendMenu(MF_STRING, FirstBookmarkCommand + i, text.c_str());
	}
}

void CHexView::GotoBookmark(int64_t offset) {
	m_Hex.GotoOffset(offset);
	m_Hex.SetFocus();
}

LRESULT CHexView::OnBookmarkToggle(WORD, WORD, HWND, BOOL&) {
	if (!m_Buffer)
		return 0;
	auto caret = m_Hex.GetCaretOffset();
	auto it = std::lower_bound(m_Bookmarks.begin(), m_Bookmarks.end(), caret);
	if (it != m_Bookmarks.end() && *it == caret)
		m_Bookmarks.erase(it);
	else
		m_Bookmarks.insert(it, caret);
	RebuildHighlights();
	return 0;
}

LRESULT CHexView::OnBookmarkNext(WORD, WORD, HWND, BOOL&) {
	if (m_Bookmarks.empty())
		return 0;
	auto caret = m_Hex.GetCaretOffset();
	auto it = std::upper_bound(m_Bookmarks.begin(), m_Bookmarks.end(), caret);
	GotoBookmark(it == m_Bookmarks.end() ? m_Bookmarks.front() : *it);	// wrap around
	return 0;
}

LRESULT CHexView::OnBookmarkPrev(WORD, WORD, HWND, BOOL&) {
	if (m_Bookmarks.empty())
		return 0;
	auto caret = m_Hex.GetCaretOffset();
	auto it = std::lower_bound(m_Bookmarks.begin(), m_Bookmarks.end(), caret);
	GotoBookmark(it == m_Bookmarks.begin() ? m_Bookmarks.back() : *(it - 1));
	return 0;
}

LRESULT CHexView::OnBookmarkClear(WORD, WORD, HWND, BOOL&) {
	m_Bookmarks.clear();
	RebuildHighlights();
	return 0;
}

LRESULT CHexView::OnBookmarkGoto(WORD, WORD id, HWND, BOOL&) {
	size_t index = id - FirstBookmarkCommand;
	if (index < m_Bookmarks.size())
		GotoBookmark(m_Bookmarks[index]);
	return 0;
}

//
// find
//

LRESULT CHexView::OnFind(WORD, WORD, HWND, BOOL&) {
	if (!m_Buffer)
		return 0;
	CHexFindDlg dlg(m_FindOptions);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;

	std::wstring error;
	if (HexSearch::BuildPattern(m_FindOptions, m_Pattern, error))
		DoFind(m_FindOptions.Down, true);
	return 0;
}

LRESULT CHexView::OnFindNext(WORD, WORD, HWND, BOOL& handled) {
	if (m_Pattern.empty()) {
		return OnFind(0, 0, nullptr, handled);
	}
	DoFind(true, false);
	return 0;
}

LRESULT CHexView::OnFindPrevious(WORD, WORD, HWND, BOOL& handled) {
	if (m_Pattern.empty()) {
		return OnFind(0, 0, nullptr, handled);
	}
	DoFind(false, false);
	return 0;
}

bool CHexView::DoFind(bool forward, bool fromDialog) {
	if (!m_Buffer || m_Pattern.empty())
		return false;

	auto size = m_Buffer->GetSize();
	auto data = m_Buffer->GetRawData(0);
	auto length = (int64_t)m_Pattern.Bytes.size();

	// continue after the current selection (or match); a fresh search from the dialog includes the caret itself
	bool hasSelection = m_Hex.HasSelection();
	auto current = hasSelection ? m_Hex.GetSelectionOffset() : m_Hex.GetCaretOffset();
	bool inclusive = fromDialog && !hasSelection;
	auto start = forward ? (inclusive ? current : current + 1) : (inclusive ? current : current - 1);

	int64_t found = -1;
	if (forward) {
		found = HexSearch::Find(data, size, m_Pattern, start, true);
		if (found < 0 && start > 0)
			found = HexSearch::Find(data, size, m_Pattern, 0, true);	// wrap around
	}
	else {
		if (start >= 0)
			found = HexSearch::Find(data, size, m_Pattern, start, false);
		if (found < 0)
			found = HexSearch::Find(data, size, m_Pattern, size, false);	// wrap around
	}

	if (found < 0) {
		AtlMessageBox(m_hWnd, L"The pattern was not found.", IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}
	m_Hex.SetSelection(found, length);
	m_Hex.GotoOffset(found);
	m_Hex.SetFocus();
	return true;
}
