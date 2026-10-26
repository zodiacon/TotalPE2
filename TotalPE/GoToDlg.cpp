#include "pch.h"
#include "GoToDlg.h"
#include <DpiHelper.h>
#include <ListViewhelper.h>
#include <unordered_set>

LRESULT CGoToDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());

	SetDlgItemText(IDC_GOTO_VALUE, m_Options.Text.c_str());
	CheckRadioButton(IDC_GOTO_RVA, IDC_GOTO_OFFSET, IDC_GOTO_RVA + (int)m_Options.Kind);
	CheckDlgButton(IDC_GOTO_DISASM, m_Options.Disassemble ? BST_CHECKED : BST_UNCHECKED);

	GetDlgItem(IDC_GOTO_VALUE).SetFocus();
	((CEdit)GetDlgItem(IDC_GOTO_VALUE)).SetSelAll();
	return FALSE;
}

LRESULT CGoToDlg::OnOK(WORD, WORD wID, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_GOTO_VALUE, text);

	uint64_t value;
	if (!ParseHexNumber((PCWSTR)text, value)) {
		AtlMessageBox(m_hWnd, L"Enter a hexadecimal number, such as 1A40 or 0x140001A40.", IDR_MAINFRAME, MB_ICONWARNING);
		GetDlgItem(IDC_GOTO_VALUE).SetFocus();
		return 0;
	}

	m_Options.Text = (PCWSTR)text;
	m_Options.Value = value;
	m_Options.Kind = IsDlgButtonChecked(IDC_GOTO_VA) == BST_CHECKED ? GoToKind::Va :
		IsDlgButtonChecked(IDC_GOTO_OFFSET) == BST_CHECKED ? GoToKind::FileOffset : GoToKind::Rva;
	m_Options.Disassemble = IsDlgButtonChecked(IDC_GOTO_DISASM) == BST_CHECKED;
	EndDialog(wID);
	return 0;
}

LRESULT CGoToDlg::OnCancel(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}

namespace {
	enum FunctionColumn { Name, Details, Location };
	constexpr UINT_PTR TimerUpdateFunctions = 1;
}

CString CDisassembleFunctionDlg::GetColumnText(HWND, int row, int col) const {
	auto const& function = m_Functions[row];
	auto const& item = *function.Item;
	switch (col) {
		case FunctionColumn::Name: return item.Name.c_str();
		case FunctionColumn::Details:
			if (!function.Declaration)
				function.Declaration = m_Host ? m_Host->FunctionDeclaration(function.Address) : std::wstring();
			return function.Declaration->empty() ? item.Details.c_str() : function.Declaration->c_str();
		case FunctionColumn::Location: return item.Location.c_str();
	}
	return CString();
}

int CDisassembleFunctionDlg::GetRowImage(HWND, int row, int) const {
	return m_Functions[row].Item->Image;
}

bool CDisassembleFunctionDlg::OnDoubleClickList(HWND, int row, int, CPoint const&) {
	if (row < 0 || row >= (int)m_Functions.size())
		return false;
	Choose(row);
	return true;
}

LRESULT CDisassembleFunctionDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());
	if (m_Caption)
		SetWindowText(m_Caption);
	if (m_Button)
		SetDlgItemText(IDOK, m_Button);

	m_Edit.SubclassWindow(GetDlgItem(IDC_DISASM_TEXT));
	m_List.Attach(GetDlgItem(IDC_DISASM_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	CRect rc;
	m_List.GetClientRect(&rc);
	auto cx = rc.Width() - DpiHelper::GetSystemMetrics(SM_CXVSCROLL, m_List);
	m_List.InsertColumn(FunctionColumn::Name, L"Name", LVCFMT_LEFT, cx * 35 / 100);
	m_List.InsertColumn(FunctionColumn::Details, L"Details", LVCFMT_LEFT, cx * 45 / 100);
	m_List.InsertColumn(FunctionColumn::Location, L"Location", LVCFMT_LEFT, cx - cx * 35 / 100 - cx * 45 / 100);
	ListViewHelper::EnableColumnDpiScaling(m_List);
	if (m_Host) {
		m_List.SetImageList(m_Host->GetImageList(), LVSIL_SMALL);	// the tree's (LVS_SHAREIMAGELISTS)
		CWaitCursor wait;	// the index is built the first time
		m_Host->GetSearchIndex();
	}

	// with nothing in it when some were opened before (they are listed), else the last text (the functions it names are)
	if (m_Host)
		m_Recent = m_Host->RecentFunctions();
	m_Setting = true;
	m_Edit.SetWindowText(m_Recent.empty() ? m_Text.c_str() : L"");
	m_Setting = false;
	Update();
	m_Edit.SetFocus();
	m_Edit.SetSelAll();
	return FALSE;
}

LRESULT CDisassembleFunctionDlg::OnTextChanged(WORD, WORD, HWND, BOOL&) {
	if (m_Setting)
		return 0;
	// after a pause in the typing: the index can be large
	SetTimer(TimerUpdateFunctions, 100);
	return 0;
}

LRESULT CDisassembleFunctionDlg::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
	if (id != TimerUpdateFunctions) {
		handled = FALSE;
		return 0;
	}
	KillTimer(TimerUpdateFunctions);
	Update();
	return 0;
}

void CDisassembleFunctionDlg::Update() {
	CString text;
	m_Edit.GetWindowText(text);
	text.Trim();
	m_Functions.clear();
	if (text.IsEmpty())
		for (auto const& item : m_Recent)
			m_Functions.push_back({ &item, item.Value, {} });
	else if (m_Host) {
		auto& index = m_Host->GetSearchIndex();
		auto kinds = SearchKindBit(SearchKind::Export) | SearchKindBit(SearchKind::Symbol);
		// (the best of the exports and the symbols, of which those that are functions; one for each address)
		std::unordered_set<uint64_t> seen;
		for (auto i : index.FindBest((PCWSTR)text, kinds, MaxResults * 10)) {
			uint64_t address;
			if (m_Host->FunctionAddress(index[i], address) && seen.insert(address).second) {
				m_Functions.push_back({ &index[i], address, {} });
				if (m_Functions.size() == MaxResults)
					break;
			}
		}
		// (the compiler's pieces of functions last: "`F'::`1'::dtor$0", "`F'::`1'::catch$2")
		std::stable_partition(m_Functions.begin(), m_Functions.end(), [](auto const& f) { return !f.Item->Name.starts_with(L'`'); });
		// what is typed may be the start of an address: the functions there (those without names too), after those named so
		m_ByAddress = m_Host->FunctionsAt((PCWSTR)text, MaxResults);
		for (auto const& item : m_ByAddress)
			if (m_Functions.size() < MaxResults && seen.insert(item.Value).second)
				m_Functions.push_back({ &item, item.Value, {} });
	}
	// (none chosen: the rows are others)
	m_Selecting = true;
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_Selecting = false;
	m_List.SetItemCountEx((int)m_Functions.size(), 0);
	// the best is chosen (the last opened), unless an address is typed (it starts with a digit)
	if (!m_Functions.empty() && (text.IsEmpty() || !iswdigit(text[0]))) {
		m_Selecting = true;
		m_List.SelectItem(0);
		m_List.EnsureVisible(0, FALSE);
		m_Selecting = false;
	}
	SetDlgItemText(IDC_DISASM_STATUS, !m_Host ? L"" :
		text.IsEmpty() ? (m_Recent.empty() ? L"Type a part of a function's name" : L"Opened recently (type a part of a name to find others)") :
		m_Functions.empty() ? L"No function matches" :
		m_Functions.size() >= MaxResults ? std::format(L"The first {} functions that match", m_Functions.size()).c_str() :
		std::format(L"{} functions match", m_Functions.size()).c_str());
}

void CDisassembleFunctionDlg::Choose(int row) {
	m_Text = m_Functions[row].Item->Name;
	m_Address = m_Functions[row].Address;
	EndDialog(IDOK);
}

// Enter: the function chosen in the list, else the text (a name or an address)
LRESULT CDisassembleFunctionDlg::OnOK(WORD, WORD wID, HWND, BOOL&) {
	if (KillTimer(TimerUpdateFunctions))
		Update();
	if (auto row = m_List.GetNextItem(-1, LVNI_SELECTED); row >= 0 && row < (int)m_Functions.size()) {
		Choose(row);
		return 0;
	}
	CString text;
	m_Edit.GetWindowText(text);
	text.Trim();
	if (text.IsEmpty()) {
		m_Edit.SetFocus();
		return 0;
	}
	m_Text = (PCWSTR)text;
	m_Address = 0;
	EndDialog(wID);
	return 0;
}

LRESULT CDisassembleFunctionDlg::OnCancel(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}

// a function chosen in the list (a click; not from the box, whose keys move in it): its name in the box, to change, or to
// take with Enter
LRESULT CDisassembleFunctionDlg::OnListItemChanged(int, LPNMHDR hdr, BOOL& handled) {
	handled = FALSE;	// (the list's own handling)
	auto lv = (NMLISTVIEW*)hdr;
	auto row = lv->iItem;
	if (m_Selecting || !(lv->uChanged & LVIF_STATE) || !(lv->uNewState & LVIS_SELECTED) || (lv->uOldState & LVIS_SELECTED) ||
		row < 0 || row >= (int)m_Functions.size())
		return 0;
	m_Setting = true;
	m_Edit.SetWindowText(m_Functions[row].Item->Name.c_str());
	m_Setting = false;
	return 0;
}

LRESULT CDisassembleFunctionDlg::OnEditKeyDown(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
	switch (wp) {
		case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
			if (KillTimer(TimerUpdateFunctions))
				Update();
			m_Selecting = true;
			m_List.SendMessage(msg, wp, lp);
			m_Selecting = false;
			return 0;
	}
	handled = FALSE;
	return 0;
}
