#include "pch.h"
#include "HexView.h"
#include "PEFile.h"
#include "MemoryBuffer.h"
#include <ToolbarHelper.h>
#include <ThemeHelper.h>

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

	return true;
}

bool CHexView::SetData(std::span<const std::byte> data) {
	m_Buffer = std::make_unique<MemoryBuffer>((const uint8_t*)data.data(), (uint32_t)data.size(), false);
	m_Hex.SetBufferManager(m_Buffer.get());
	return true;
}

bool CHexView::SetData(PVOID address, uint32_t size, bool copy) {
	m_Buffer = std::make_unique<MemoryBuffer>((const uint8_t*)address, size, copy);
	m_Hex.SetBufferManager(m_Buffer.get());
	return true;
}

void CHexView::ClearData() {
	m_Hex.SetBufferManager(nullptr);
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
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 24);

	AddSimpleReBarBand(m_tb);
	UIAddToolBar(m_tb);
	UISetRadioMenuItem(ID_BYTESPERLINE_32, ID_BYTESPERLINE_8, ID_BYTESPERLINE_64);
	UISetCheck(ID_DATASIZE_1BYTE, true);

	m_hWndClient = m_Hex.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
	UpdateColors();

	bHandled = FALSE;
	return 0;
}

LRESULT CHexView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CPoint pt;
	::GetCursorPos(&pt);
	return Frame()->TrackPopupMenu(menu.GetSubMenu(5), 0, pt.x, pt.y);
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
	return 0;
}

LRESULT CHexView::OnSave(WORD, WORD, HWND, BOOL&) {
	CSimpleFileDialog dlg(FALSE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT,
		L"All Files\0*.*\0", m_hWnd);
	ThemeHelper::Suspend();
	auto ok = IDOK == dlg.DoModal();
	ThemeHelper::Resume();
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
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	auto pt = ToolbarHelper::GetDropdownMenuPoint(hdr->hwndFrom, ID_BYTES_PER_LINE);
	auto cmd = (UINT)Frame()->TrackPopupMenu(menu.GetSubMenu(1), TPM_VERTICAL | TPM_RETURNCMD, pt.x, pt.y);
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
	colors.Offset = ThemeHelper::IsDefault() ? RGB(0, 0, 128) : RGB(0, 128, 255);
	colors.Ascii = ThemeHelper::IsDefault() ? RGB(128, 0, 0) : RGB(255, 128, 0);
	m_Hex.GetColors() = colors;
	m_Hex.Invalidate();
}
