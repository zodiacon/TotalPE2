#include "pch.h"
#include "Win32ApiView.h"
#include <ToolbarHelper.h>
#include <SortHelper.h>
#include "AppSettings.h"

namespace {
	std::string Lower(std::string text) {
		for (auto& c : text)
			c = (char)tolower((unsigned char)c);
		return text;
	}
}

CWin32ApiView::CWin32ApiView(IMainFrame* frame, PEFile const& pe, Win32Api& api, std::unordered_set<std::string> imports) :
	CViewBase(frame), m_PE(pe), m_Api(&api), m_Imports(std::move(imports)) {
}

CString CWin32ApiView::GetTitle() const {
	return L"Windows API";
}

void CWin32ApiView::UpdateUI(bool) {
	auto text = std::format(L"{} of {} functions and types{}", m_Items.size(), m_Api->Entries().size(),
		m_ImportsOnly ? L" (the imports of the file)" : L"");
	if (m_hStatusBar)
		::SetWindowText(m_hStatusBar, text.c_str());
}

LRESULT CWin32ApiView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	// Imports, then the filter, in bands of their own (the rebar passes their notifications on to this window)
	ToolBarButtonInfo const buttons[] = {
		{ ID_NAV_BACK, IDI_BACK },
		{ ID_NAV_FORWARD, IDI_FORWARD },
		{ 0 },
		{ ID_WIN32_IMPORTS, 0, BTNS_CHECK | BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Imports" },
		{ ID_WIN32_REFERENCES, 0, BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"References" },
		{ 0 },
		{ ID_WIN32_DOCS, 0, BTNS_CHECK | BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Documentation" },
		{ ID_WIN32_BROWSER, 0, BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Open in Browser" },
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 16);
	AddSimpleReBarBand(m_tb);
	UIAddToolBar(m_tb);
	CRect rc(0, 0, 300, 20);
	m_FilterEdit.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
	m_FilterEdit.SetFont(AtlGetDefaultGuiFont());
	m_FilterEdit.SetWatermark(L"Filter functions and types (Esc to clear)");
	AddSimpleReBarBand(m_FilterEdit, nullptr, FALSE, 300);
	CRect rcLast;
	m_tb.GetItemRect(m_tb.GetButtonCount() - 1, &rcLast);
	CReBarCtrl rebar(m_hWndToolBar);
	REBARBANDINFO band{ sizeof(band) };
	band.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
	rebar.GetBandInfo(0, &band);
	band.cxMinChild = band.cxIdeal = rcLast.right;
	band.cx = rcLast.right + 16;
	rebar.SetBandInfo(0, &band);
	UpdateImportsButton();
	UpdateHistoryButtons();
	UISetCheck(ID_WIN32_DOCS, AppSettings::Get().Win32ApiDocumentation());
	UIEnable(ID_WIN32_BROWSER, false);

	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// (the icons are the frame's: shared, not destroyed with the list)
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 240);
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 70);
	cm->AddColumn(L"DLL or Namespace", LVCFMT_LEFT, 200);

	m_Right.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Code = new CScintillaView(Frame(), m_PE, GetTitle());
	m_Code->DoCreate(m_Right);
	m_Code->SetLanguage(LexLanguage::C);
	m_Code->GetCtrl().SetReadOnly(true);
	m_Code->GetCtrl().SendMessage(SCI_SETTABWIDTH, 4);
	m_Code->SetLineAction([this](int line) { return GoToLine(line); });

	m_Doc.Create(m_Right, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Right.SetSplitterPanes(*m_Code, m_Doc);
	m_Right.SetSplitterPosPct(40);
	if (!AppSettings::Get().Win32ApiDocumentation())
		m_Right.SetSinglePaneMode(SPLIT_PANE_TOP);
	m_Splitter.SetSplitterPanes(m_List, m_Right);
	m_Splitter.SetSplitterPosPct(40);

	{
		CWaitCursor wait;
		Fill();
	}
	handled = FALSE;
	return 0;
}

BOOL CWin32ApiView::PreTranslateMessage(MSG* pMsg) {
	if (pMsg->message == WM_KEYDOWN && m_hWnd && (pMsg->hwnd == m_hWnd || IsChild(pMsg->hwnd))) {
		bool control = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0, alt = ::GetKeyState(VK_MENU) < 0;
		// to the filter: Ctrl+Q, Ctrl+F
		if (control && !shift && !alt && (pMsg->wParam == 'Q' || pMsg->wParam == 'F')) {
			m_FilterEdit.SetFocus();
			m_FilterEdit.SetSelAll();
			return TRUE;
		}
		// from the filter to the list (its first row, if none is selected)
		if (pMsg->hwnd == m_FilterEdit && !control && !alt && (pMsg->wParam == VK_DOWN || pMsg->wParam == VK_RETURN) && !m_Items.empty()) {
			if (m_List.GetSelectedIndex() < 0)
				m_List.SelectItem(0);
			m_List.SetFocus();
			return TRUE;
		}
	}
	if (pMsg->message == WM_SYSKEYDOWN && m_hWnd && (pMsg->hwnd == m_hWnd || IsChild(pMsg->hwnd)) &&
		(pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT)) {
		if (pMsg->wParam == VK_LEFT)
			GoBack();
		else
			GoForward();
		return TRUE;
	}
	return CViewBase::PreTranslateMessage(pMsg);
}

LRESULT CWin32ApiView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_List.SetFocus();
	return 0;
}

LRESULT CWin32ApiView::OnForward(UINT msg, WPARAM wp, LPARAM lp, BOOL&) {
	return m_Code ? m_Code->SendMessage(msg, wp, lp) : 0;
}

// The one selected stays selected if it is still there; else none is (the rows are others: what a row showed is not what it
// is now)
void CWin32ApiView::Fill() {
	auto selected = SelectedEntry();
	auto const& entries = m_Api->Entries();
	m_Items.clear();
	for (size_t i = 0; i < entries.size(); i++) {
		auto const& e = entries[i];
		if (m_ImportsOnly && (e.Kind != Win32Api::Entry::Kinds::Function || !m_Imports.contains(e.Name)))
			continue;
		if (!m_Filter.empty() && Lower(e.Name).find(m_Filter) == std::string::npos)
			continue;
		m_Items.push_back(i);
	}
	m_List.SetItemCountEx((int)m_Items.size(), 0);
	Sort(m_List);
	if (auto it = std::ranges::find(m_Items, (size_t)selected); selected >= 0 && it != m_Items.end()) {
		auto row = (int)(it - m_Items.begin());
		m_List.SelectItem(row);
	}
	else
		m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.RedrawItems(0, m_List.GetItemCount());
	UpdateUI();
}

CString CWin32ApiView::GetColumnText(HWND, int row, int col) const {
	auto const& e = m_Api->Entries()[m_Items[row]];
	switch (col) {
		case 0: return CString(e.Name.c_str());
		case 1: return Win32Api::KindName(e.Kind);
		case 2: return CString(e.Where.c_str());
	}
	return CString();
}

// the icons of the kinds, as the views of .NET types and of the symbols have them
int CWin32ApiView::GetRowImage(HWND, int row, int) const {
	using Kinds = Win32Api::Entry::Kinds;
	UINT icon = IDI_FUNCTION;
	switch (m_Api->Entries()[m_Items[row]].Kind) {
		case Kinds::Function: icon = IDI_FUNCTION; break;
		case Kinds::Struct: icon = IDI_TYPE; break;
		case Kinds::Union: icon = IDI_UNION; break;
		case Kinds::Enum: icon = IDI_ENUM; break;
		case Kinds::Typedef: icon = IDI_DATA; break;
		case Kinds::Callback: icon = IDI_FUNCTION2; break;
		case Kinds::Interface: icon = IDI_INTERFACE; break;
	}
	return Frame()->GetIconIndex(icon);
}

void CWin32ApiView::DoSort(SortInfo const* si) {
	auto const& entries = m_Api->Entries();
	if (si == nullptr) {
		std::ranges::sort(m_Items);	// (the entries are by name)
		return;
	}
	auto asc = si->SortAscending;
	std::stable_sort(m_Items.begin(), m_Items.end(), [&](size_t a, size_t b) {
		auto& x = entries[a];
		auto& y = entries[b];
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(x.Name, y.Name, asc);
			case 1: return SortHelper::Sort((int)x.Kind, (int)y.Kind, asc);
			case 2: return SortHelper::Sort(x.Where, y.Where, asc);
		}
		return false;
	});
}

int CWin32ApiView::SelectedEntry() const {
	auto row = m_List.GetSelectedIndex();
	return row >= 0 && row < (int)m_Items.size() ? (int)m_Items[row] : -1;
}

void CWin32ApiView::Show(int entry) {
	if (entry == m_Shown)
		return;
	m_Shown = entry;
	m_ShownAt = ::GetTickCount64();
	UIEnable(ID_WIN32_REFERENCES, CanShowReferences());
	auto& ctrl = m_Code->GetCtrl();
	ctrl.SetReadOnly(false);
	if (entry < 0) {
		m_LineTypes.clear();
		m_Code->SetText("");
	}
	else
		m_Code->SetText(m_Api->Declaration(m_Api->Entries()[entry], &m_LineTypes).c_str());
	ctrl.SetReadOnly(true);
	ctrl.EmptyUndoBuffer();
	m_DocUrl = entry < 0 ? std::wstring() : m_Api->DocumentationUrl(m_Api->Entries()[entry]);
	UIEnable(ID_WIN32_BROWSER, !m_DocUrl.empty());
	ShowDocumentation();
}

void CWin32ApiView::ShowDocumentation() {
	if (!AppSettings::Get().Win32ApiDocumentation() || !m_Doc)
		return;
	if (m_Shown < 0 && m_Doc.Url().empty())
		return;		// (the browser is started for a page)
	m_Doc.Navigate(m_DocUrl, m_Shown < 0 ? std::wstring() : std::wstring(CA2W(m_Api->Entries()[m_Shown].Name.c_str())));
}

LRESULT CWin32ApiView::OnDocumentation(WORD, WORD, HWND, BOOL&) {
	bool show = !AppSettings::Get().Win32ApiDocumentation();
	AppSettings::Get().Win32ApiDocumentation(show);
	UISetCheck(ID_WIN32_DOCS, show);
	m_Right.SetSinglePaneMode(show ? SPLIT_PANE_NONE : SPLIT_PANE_TOP);
	ShowDocumentation();
	return 0;
}

LRESULT CWin32ApiView::OnOpenInBrowser(WORD, WORD, HWND, BOOL&) {
	if (!m_DocUrl.empty())
		::ShellExecute(nullptr, L"open", m_DocUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CWin32ApiView::OnItemChanged(int, LPNMHDR hdr, BOOL& handled) {
	auto lv = (NMLISTVIEW*)hdr;
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
		auto entry = SelectedEntry();
		// the one that was shown, if the user looked at it (a second, at least)
		if (!m_Navigating && entry != m_Shown && m_Shown >= 0 && ::GetTickCount64() - m_ShownAt >= 1000)
			Remember(m_Api->Entries()[m_Shown].Name);
		Show(entry);
	}
	return 0;
}

bool CWin32ApiView::OnDoubleClickList(HWND, int row, int, CPoint const&) {
	if (row < 0)
		return false;
	m_Code->GetCtrl().SetFocus();
	return true;
}

LRESULT CWin32ApiView::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_FilterEdit) {
		handled = FALSE;
		return 0;
	}
	CString text;
	m_FilterEdit.GetWindowText(text);
	m_Filter = Lower(std::string(CW2A(text)));
	Fill();
	return 0;
}

LRESULT CWin32ApiView::OnImports(WORD, WORD, HWND, BOOL&) {
	m_ImportsOnly = !m_ImportsOnly;
	UISetCheck(ID_WIN32_IMPORTS, m_ImportsOnly);
	Fill();
	return 0;
}

void CWin32ApiView::SetFile(Win32Api& api, std::unordered_set<std::string> imports) {
	// what it showed, by name: the same function or type of the other machine
	auto shown = m_Shown >= 0 ? m_Api->Entries()[m_Shown].Name : std::string();
	m_Api = &api;
	m_Imports = std::move(imports);
	if (m_Imports.empty() && m_ImportsOnly) {
		m_ImportsOnly = false;
		UISetCheck(ID_WIN32_IMPORTS, false);
	}
	UpdateImportsButton();
	m_Shown = -1;
	m_Navigating = true;
	Fill();
	if (shown.empty() || !Select(shown))
		Show(-1);
	m_Navigating = false;
}

bool CWin32ApiView::CanShowReferences() const {
	if (m_Shown < 0)
		return false;
	auto const& e = m_Api->Entries()[m_Shown];
	return e.Kind == Win32Api::Entry::Kinds::Function && m_Imports.contains(e.Name);
}

LRESULT CWin32ApiView::OnReferences(WORD, WORD, HWND, BOOL&) {
	if (!CanShowReferences() || !Frame()->ShowImportReferences(m_Api->Entries()[m_Shown].Name))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

bool CWin32ApiView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0)
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (CanShowReferences() ? 0 : MF_GRAYED), ID_WIN32_REFERENCES, L"&References in the File");
	auto command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (command)
		PostMessage(WM_COMMAND, command);
	return true;
}

void CWin32ApiView::UpdateImportsButton() {
	// (without a file, there are no imports)
	UIEnable(ID_WIN32_IMPORTS, !m_Imports.empty());
	UIEnable(ID_WIN32_REFERENCES, CanShowReferences());
}

bool CWin32ApiView::Select(std::string const& name) {
	auto entry = m_Api->Find(name);
	if (!entry)
		return false;
	auto index = (size_t)(entry - m_Api->Entries().data());
	auto it = std::ranges::find(m_Items, index);
	if (it == m_Items.end()) {
		// hidden by the filter: all are shown again
		m_Filter.clear();
		m_ImportsOnly = false;
		UISetCheck(ID_WIN32_IMPORTS, false);
		m_FilterEdit.SetWindowText(L"");
		Fill();
		it = std::ranges::find(m_Items, index);
		if (it == m_Items.end())
			return false;
	}
	auto row = (int)(it - m_Items.begin());
	m_List.SelectItem(row);
	m_List.EnsureVisible(row, FALSE);
	Show((int)index);
	return true;
}

bool CWin32ApiView::GoToLine(int line) {
	if (line < 0 || line >= (int)m_LineTypes.size() || m_LineTypes[line].empty())
		return false;
	auto const& name = m_LineTypes[line];
	if (m_Shown >= 0 && m_Api->Entries()[m_Shown].Name == name)
		return false;
	return Navigate(name);
}

bool CWin32ApiView::Navigate(std::string const& name) {
	auto from = m_Shown >= 0 ? m_Api->Entries()[m_Shown].Name : std::string();
	m_Navigating = true;
	bool shown = Select(name);
	m_Navigating = false;
	if (!shown)
		return false;
	if (from != name)
		Remember(from);
	return true;
}

void CWin32ApiView::Remember(std::string const& name) {
	if (name.empty() || (!m_Back.empty() && m_Back.back() == name))
		return;
	m_Back.push_back(name);
	m_Forward.clear();
	UpdateHistoryButtons();
}

bool CWin32ApiView::ShowFromHistory(std::string const& name) {
	m_Navigating = true;
	bool shown = Select(name);
	m_Navigating = false;
	return shown;
}

void CWin32ApiView::GoBack() {
	while (!m_Back.empty()) {
		auto name = std::move(m_Back.back());
		m_Back.pop_back();
		auto current = m_Shown >= 0 ? m_Api->Entries()[m_Shown].Name : std::string();
		if (ShowFromHistory(name)) {
			if (!current.empty())
				m_Forward.push_back(current);
			break;
		}
	}
	UpdateHistoryButtons();
}

void CWin32ApiView::GoForward() {
	while (!m_Forward.empty()) {
		auto name = std::move(m_Forward.back());
		m_Forward.pop_back();
		auto current = m_Shown >= 0 ? m_Api->Entries()[m_Shown].Name : std::string();
		if (ShowFromHistory(name)) {
			if (!current.empty())
				m_Back.push_back(current);
			break;
		}
	}
	UpdateHistoryButtons();
}

void CWin32ApiView::UpdateHistoryButtons() {
	// (through the UI's state: it is what the toolbar is updated from)
	UIEnable(ID_NAV_BACK, !m_Back.empty());
	UIEnable(ID_NAV_FORWARD, !m_Forward.empty());
}

LRESULT CWin32ApiView::OnBack(WORD, WORD, HWND, BOOL&) {
	GoBack();
	return 0;
}

LRESULT CWin32ApiView::OnForwardCommand(WORD, WORD, HWND, BOOL&) {
	GoForward();
	return 0;
}

void CWin32ApiView::SetStatusBar(HWND hStatusBar) {
	m_hStatusBar = hStatusBar;
}

int64_t CWin32ApiView::GetNavigationPosition() const {
	return m_Shown;
}

void CWin32ApiView::SetNavigationPosition(int64_t position) {
	if (position >= 0 && position < (int64_t)m_Api->Entries().size())
		Select(m_Api->Entries()[(size_t)position].Name);
}
