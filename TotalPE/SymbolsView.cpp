#include "pch.h"
#include "SymbolsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include "resource.h"
#include "PEStrings.h"

CSymbolsView::CSymbolsView(IMainFrame* frame, DiaSession const& session, SymViewType type)
	: CViewBase(frame), m_Session(session), m_Type(type) {
}

CString CSymbolsView::GetColumnText(HWND h, int row, int col) const {
	auto& item = m_Items[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return item.Name().c_str();
		case ColumnType::Type: return item.TypeName().c_str();
		case ColumnType::Class: return item.ClassParent() ? item.ClassParent().Name().c_str() : L"";
		case ColumnType::Location: return PEStrings::SymbolLocationToString(item.Location());
		case ColumnType::UndecoratedName: return item.UndecoratedName().empty() ? PEStrings::UndecorateName(item.Name().c_str()).c_str() : item.UndecoratedName().c_str();
	}
	return CString();
}

void CSymbolsView::DoSort(SortInfo const* si) {
}

void CSymbolsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
}

CString CSymbolsView::GetTitle() const {
	switch (m_Type) {
		case SymViewType::Function: return L"Functions";
		case SymViewType::Data: return L"Data";
		case SymViewType::Enum: return L"Enums";
		case SymViewType::UDT: return L"Types";
	}
	ATLASSERT(false);
	return L"(Symbols)";
}

LRESULT CSymbolsView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 250, ColumnType::Name);
	cm->AddColumn(L"Location", LVCFMT_LEFT, 150, ColumnType::Location);
	cm->AddColumn(L"Class", LVCFMT_LEFT, 150, ColumnType::Class);
	cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 350, ColumnType::UndecoratedName);

	BuildItems();

	return 0;
}

LRESULT CSymbolsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List);
	ClipboardHelper::CopyText(m_hWnd, text);

	return 0;
}

void CSymbolsView::UpdateUI(bool first) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

LRESULT CSymbolsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());

	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else {
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	}
	return 0;
}

void CSymbolsView::BuildItems() {
	auto tag = SymbolTag::Null;
	switch (m_Type) {
		case SymViewType::Function: tag = SymbolTag::Function; break;
		case SymViewType::UDT: tag = SymbolTag::UDT; break;
		case SymViewType::Enum: tag = SymbolTag::Enum; break;
		case SymViewType::Data: tag = SymbolTag::Data; break;
	}
	m_Items = m_Session.FindChildren(nullptr, tag);

	m_List.SetItemCount((int)m_Items.size());
}
