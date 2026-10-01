#include "pch.h"
#include "DialogView.h"

namespace {
	INT_PTR CALLBACK PreviewDlgProc(HWND, UINT, WPARAM, LPARAM) {
		// inert: the preview must never act on commands or take focus
		return FALSE;
	}

	void InitPreviewControls() {
		static bool done = false;
		if (done)
			return;
		done = true;
		INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_USEREX_CLASSES |
			ICC_COOL_CLASSES | ICC_INTERNET_CLASSES | ICC_PAGESCROLLER_CLASS | ICC_NATIVEFNTCTL_CLASS |
			ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
		::InitCommonControlsEx(&icc);
		// rich edit controls are common in dialogs
		::LoadLibrary(L"Msftedit.dll");
		::LoadLibrary(L"Riched20.dll");
	}

	std::wstring Hex(DWORD v) {
		return std::format(L"0x{:08X}", v);
	}
}

bool CDialogHost::ShowDialog(DlgTemplate const& dlg) {
	InitPreviewControls();
	auto tmpl = dlg.BuildPreviewTemplate();
	auto hDlg = ::CreateDialogIndirectParam(_Module.GetModuleInstance(), (LPCDLGTEMPLATE)tmpl.data(), m_hWnd, PreviewDlgProc, 0);
	if (!hDlg)
		return false;

	const int margin = 12;
	CRect rc;
	::GetWindowRect(hDlg, &rc);
	::SetWindowPos(hDlg, nullptr, margin, margin, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
	SetScrollSize(rc.Width() + 2 * margin, rc.Height() + 2 * margin);
	return true;
}

CDialogView::CDialogView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CDialogView::GetTitle() const {
	return m_Title;
}

bool CDialogView::SetData(std::span<const std::byte> data) {
	if (!DlgTemplate::Parse(data, m_Dialog))
		return false;

	BuildRows();
	m_Host.ShowDialog(m_Dialog);
	return true;
}

CString CDialogView::GetColumnText(HWND, int row, int col) const {
	auto& r = m_Rows[row];
	switch (col) {
		case 0: return r.Class.c_str();
		case 1: return r.Id.c_str();
		case 2: return r.Text.c_str();
		case 3: return r.X.c_str();
		case 4: return r.Y.c_str();
		case 5: return r.Width.c_str();
		case 6: return r.Height.c_str();
		case 7: return r.Style.c_str();
		case 8: return r.ExStyle.c_str();
	}
	return CString();
}

void CDialogView::DoSort(SortInfo const*) {
}

bool CDialogView::IsSortable(HWND, int) const {
	// the order of controls is the tab order
	return false;
}

LRESULT CDialogView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Host.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Class", LVCFMT_LEFT, 110);
	cm->AddColumn(L"ID", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Text", LVCFMT_LEFT, 160);
	cm->AddColumn(L"X", LVCFMT_RIGHT, 40);
	cm->AddColumn(L"Y", LVCFMT_RIGHT, 40);
	cm->AddColumn(L"Width", LVCFMT_RIGHT, 50);
	cm->AddColumn(L"Height", LVCFMT_RIGHT, 50);
	cm->AddColumn(L"Style", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Ex Style", LVCFMT_RIGHT, 90);

	m_Splitter.SetSplitterPanes(m_Host, m_List);
	m_Splitter.SetSplitterPosPct(50);
	return 0;
}

void CDialogView::BuildRows() {
	m_Rows.clear();
	auto& d = m_Dialog;
	m_Rows.push_back({ d.Extended ? L"Dialog (Ex)" : L"Dialog", L"", d.Title, std::to_wstring(d.X), std::to_wstring(d.Y),
		std::to_wstring(d.CX), std::to_wstring(d.CY), Hex(d.Style), Hex(d.ExStyle) });
	if (d.HasFont)
		m_Rows.push_back({ L"Font", L"", std::format(L"{}, {} pt{}", d.Typeface, d.PointSize,
			d.Extended ? std::format(L", weight {}{}", d.Weight, d.Italic ? L", italic" : L"") : L""), L"", L"", L"", L"", L"", L"" });
	if (!d.Menu.empty())
		m_Rows.push_back({ L"Menu", L"", d.Menu, L"", L"", L"", L"", L"", L"" });
	if (!d.ClassName.empty())
		m_Rows.push_back({ L"Dialog Class", L"", d.ClassName, L"", L"", L"", L"", L"", L"" });

	for (auto& c : d.Controls)
		m_Rows.push_back({ c.ClassName, std::to_wstring(c.Id), c.Title, std::to_wstring(c.X), std::to_wstring(c.Y),
			std::to_wstring(c.CX), std::to_wstring(c.CY), Hex(c.Style), Hex(c.ExStyle) });
	m_List.SetItemCount((int)m_Rows.size());
}
