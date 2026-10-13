#include "pch.h"
#include "Win32ApiView.h"
#include <ToolbarHelper.h>
#include <SortHelper.h>

namespace {
	std::string Lower(std::string text) {
		for (auto& c : text)
			c = (char)tolower((unsigned char)c);
		return text;
	}
}

CWin32ApiView::CWin32ApiView(IMainFrame* frame, PEFile const& pe, Win32Api& api, std::unordered_set<std::string> imports) :
	CViewBase(frame), m_PE(pe), m_Api(api), m_Imports(std::move(imports)) {
}

CString CWin32ApiView::GetTitle() const {
	return L"Windows API";
}

void CWin32ApiView::UpdateUI(bool) {
	auto text = std::format(L"{} of {} functions and types{}", m_Items.size(), m_Api.Entries().size(),
		m_ImportsOnly ? L" (the imports of the file)" : L"");
	Frame()->SetStatusText(1, text.c_str());
}

LRESULT CWin32ApiView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	// Imports, then the filter, in bands of their own (the rebar passes their notifications on to this window)
	ToolBarButtonInfo const buttons[] = {
		{ ID_WIN32_IMPORTS, 0, BTNS_CHECK | BTNS_SHOWTEXT | BTNS_AUTOSIZE, L"Imports" },
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

	m_Code = new CScintillaView(Frame(), m_PE, GetTitle());
	m_Code->DoCreate(m_Splitter);
	m_Code->SetLanguage(LexLanguage::C);
	m_Code->GetCtrl().SetReadOnly(true);
	m_Code->GetCtrl().SendMessage(SCI_SETTABWIDTH, 4);
	m_Code->SetLineAction([this](int line) { return GoToLine(line); });

	m_Splitter.SetSplitterPanes(m_List, *m_Code);
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
		bool inCode = m_Code && pMsg->hwnd == m_Code->GetCtrl().m_hWnd;
		// to the filter: Ctrl+Q, or Ctrl+F (in the declaration, Ctrl+F is Find)
		if (control && !shift && !alt && (pMsg->wParam == 'Q' || (pMsg->wParam == 'F' && !inCode))) {
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
	return CViewBase::PreTranslateMessage(pMsg);
}

LRESULT CWin32ApiView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_List.SetFocus();
	return 0;
}

LRESULT CWin32ApiView::OnForward(UINT msg, WPARAM wp, LPARAM lp, BOOL&) {
	return m_Code ? m_Code->SendMessage(msg, wp, lp) : 0;
}

void CWin32ApiView::Fill() {
	auto const& entries = m_Api.Entries();
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
	m_List.RedrawItems(0, m_List.GetItemCount());
	UpdateUI();
}

CString CWin32ApiView::GetColumnText(HWND, int row, int col) const {
	auto const& e = m_Api.Entries()[m_Items[row]];
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
	switch (m_Api.Entries()[m_Items[row]].Kind) {
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
	auto const& entries = m_Api.Entries();
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
	auto& ctrl = m_Code->GetCtrl();
	ctrl.SetReadOnly(false);
	if (entry < 0) {
		m_LineTypes.clear();
		m_Code->SetText("");
	}
	else
		m_Code->SetText(m_Api.Declaration(m_Api.Entries()[entry], &m_LineTypes).c_str());
	ctrl.SetReadOnly(true);
	ctrl.EmptyUndoBuffer();
}

LRESULT CWin32ApiView::OnItemChanged(int, LPNMHDR hdr, BOOL& handled) {
	auto lv = (NMLISTVIEW*)hdr;
	if (hdr->hwndFrom != m_List) {
		handled = FALSE;
		return 0;
	}
	if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED))
		Show(SelectedEntry());
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
	auto selected = SelectedEntry();
	Fill();
	// the one that was selected, if it is still there
	if (auto it = std::ranges::find(m_Items, (size_t)selected); selected >= 0 && it != m_Items.end()) {
		auto row = (int)(it - m_Items.begin());
		m_List.SelectItem(row);
	}
	return 0;
}

LRESULT CWin32ApiView::OnImports(WORD, WORD, HWND, BOOL&) {
	m_ImportsOnly = !m_ImportsOnly;
	UISetCheck(ID_WIN32_IMPORTS, m_ImportsOnly);
	Fill();
	return 0;
}

void CWin32ApiView::UpdateImportsButton() {
	// (without a file, there are no imports)
	m_tb.EnableButton(ID_WIN32_IMPORTS, !m_Imports.empty());
}

bool CWin32ApiView::Select(std::string const& name) {
	auto entry = m_Api.Find(name);
	if (!entry)
		return false;
	auto index = (size_t)(entry - m_Api.Entries().data());
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
	if (m_Shown >= 0 && m_Api.Entries()[m_Shown].Name == name)
		return false;
	Frame()->RecordNavigation();
	if (!Select(name))
		return false;
	Frame()->RecordNavigation();
	return true;
}

int64_t CWin32ApiView::GetNavigationPosition() const {
	return m_Shown;
}

void CWin32ApiView::SetNavigationPosition(int64_t position) {
	if (position >= 0 && position < (int64_t)m_Api.Entries().size())
		Select(m_Api.Entries()[(size_t)position].Name);
}
