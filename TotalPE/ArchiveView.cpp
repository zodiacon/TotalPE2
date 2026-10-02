#include "pch.h"
#include "ArchiveView.h"
#include "PEStrings.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>

namespace {
	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	CString MachineText(uint16_t machine) {
		auto name = LibArchive::MachineName(machine);
		if (name.empty())
			return std::format(L"0x{:X}", machine).c_str();
		return CString(name.data(), (int)name.size());
	}

	PCWSTR ImportTypeText(ImportType type) {
		switch (type) {
			case ImportType::Code: return L"Code";
			case ImportType::Data: return L"Data";
			case ImportType::Const: return L"Const";
		}
		return L"?";
	}

	PCWSTR ImportByText(ImportNameType type) {
		switch (type) {
			case ImportNameType::Ordinal: return L"Ordinal";
			case ImportNameType::Name: return L"Name";
			case ImportNameType::NameNoPrefix: return L"Name (no prefix)";
			case ImportNameType::Undecorate: return L"Name (undecorated)";
		}
		return L"?";
	}

	std::wstring UndecoratedText(std::string const& name) {
		if (name.empty())
			return L"";
		auto s = PEStrings::UndecorateName(name.c_str());
		return s == name ? L"" : Widen(s);
	}
}

CArchiveView::CArchiveView(IMainFrame* frame, LibArchive const& archive, ArchiveViewKind kind) : CViewBase(frame), m_Archive(archive), m_Kind(kind) {
}

CString CArchiveView::GetTitle() const {
	switch (m_Kind) {
		case ArchiveViewKind::Members: return L"Members";
		case ArchiveViewKind::Symbols: return L"Symbols";
	}
	return L"Import Records";
}

int CArchiveView::MemberOf(int row) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return -1;
	int i = m_Rows[row];
	switch (m_Kind) {
		case ArchiveViewKind::Members: return i;
		case ArchiveViewKind::Symbols: return m_Archive.Symbols()[i].Member;
	}
	return m_Archive.Imports()[i].Member;
}

CString CArchiveView::GetColumnText(HWND, int row, int col) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return CString();
	int i = m_Rows[row];
	auto tag = GetColumnManager(m_List)->GetColumnTag<Column>(col);
	auto const& members = m_Archive.Members();

	switch (m_Kind) {
		case ArchiveViewKind::Members:
		{
			auto const& m = members[i];
			switch (tag) {
				case Index: return std::to_wstring(i).c_str();
				case Name: return Widen(m.Name).c_str();
				case Type: return m.BigObj ? CString(L"Object (bigobj)") : CString(LibArchive::KindName(m.Kind).data());
				case Machine: return m.Machine ? MachineText(m.Machine) : CString();
				case Size: return std::format(L"{:L}", m.Size).c_str();
				case Offset: return std::format(L"0x{:X}", m.DataOffset).c_str();
				case Sections: return m.Kind == ArchiveMemberKind::Object ? std::to_wstring(m.Sections).c_str() : L"";
				case Symbols: return m.Kind == ArchiveMemberKind::Object ? std::to_wstring(m.Symbols).c_str() : L"";
				case Date: {
					if (m.Date == 0)
						return CString();
					FILETIME ft;
					*(uint64_t*)&ft = 116444736000000000ULL + (uint64_t)m.Date * 10000000ULL;
					SYSTEMTIME st, local;
					::FileTimeToSystemTime(&ft, &st);
					::SystemTimeToTzSpecificLocalTime(nullptr, &st, &local);
					return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond).c_str();
				}
			}
			break;
		}

		case ArchiveViewKind::Symbols:
		{
			auto const& s = m_Archive.Symbols()[i];
			switch (tag) {
				case Symbol: return Widen(s.Name).c_str();
				case Undecorated: return UndecoratedText(s.Name).c_str();
				case Member: return s.Member >= 0 ? Widen(members[s.Member].Name).c_str() : L"";
				case MemberIndex: return s.Member >= 0 ? std::to_wstring(s.Member).c_str() : L"";
			}
			break;
		}

		case ArchiveViewKind::Imports:
		{
			auto const& imp = m_Archive.Imports()[i];
			switch (tag) {
				case Symbol: return Widen(imp.Symbol).c_str();
				case Undecorated: return UndecoratedText(imp.Symbol).c_str();
				case Dll: return Widen(imp.Dll).c_str();
				case ImportType: return ImportTypeText(imp.Type);
				case ImportBy: return ImportByText(imp.NameType);
				case HintOrdinal: return std::to_wstring(imp.OrdinalOrHint).c_str();
				case Machine: return MachineText(imp.Machine);
				case MemberIndex: return std::to_wstring(imp.Member).c_str();
			}
			break;
		}
	}
	return CString();
}

int CArchiveView::GetRowImage(HWND, int row, int) const {
	switch (m_Kind) {
		case ArchiveViewKind::Members: return Frame()->GetIconIndex(IDI_BINARY);
		case ArchiveViewKind::Symbols: return Frame()->GetIconIndex(IDI_FUNCTION);
	}
	return Frame()->GetIconIndex(IDI_DLL_IMPORT);
}

void CArchiveView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	auto col = GetColumnManager(si->hWnd)->GetColumnTag<Column>(si->SortColumn);
	auto const& members = m_Archive.Members();
	auto const& symbols = m_Archive.Symbols();
	auto const& imports = m_Archive.Imports();

	auto compare = [&](int a, int b) {
		switch (m_Kind) {
			case ArchiveViewKind::Members:
			{
				auto const &m1 = members[a], &m2 = members[b];
				switch (col) {
					case Index: return SortHelper::Sort(a, b, asc);
					case Name: return SortHelper::Sort(m1.Name, m2.Name, asc);
					case Type: return SortHelper::Sort((int)m1.Kind, (int)m2.Kind, asc);
					case Machine: return SortHelper::Sort(m1.Machine, m2.Machine, asc);
					case Size: return SortHelper::Sort(m1.Size, m2.Size, asc);
					case Offset: return SortHelper::Sort(m1.DataOffset, m2.DataOffset, asc);
					case Sections: return SortHelper::Sort(m1.Sections, m2.Sections, asc);
					case Symbols: return SortHelper::Sort(m1.Symbols, m2.Symbols, asc);
					case Date: return SortHelper::Sort(m1.Date, m2.Date, asc);
				}
				break;
			}
			case ArchiveViewKind::Symbols:
			{
				auto const &s1 = symbols[a], &s2 = symbols[b];
				switch (col) {
					case Symbol: return SortHelper::Sort(s1.Name, s2.Name, asc);
					case Undecorated: return SortHelper::Sort(UndecoratedText(s1.Name), UndecoratedText(s2.Name), asc);
					case Member: return SortHelper::Sort(s1.Member < 0 ? std::string() : members[s1.Member].Name,
						s2.Member < 0 ? std::string() : members[s2.Member].Name, asc);
					case MemberIndex: return SortHelper::Sort(s1.Member, s2.Member, asc);
				}
				break;
			}
			case ArchiveViewKind::Imports:
			{
				auto const &i1 = imports[a], &i2 = imports[b];
				switch (col) {
					case Symbol: return SortHelper::Sort(i1.Symbol, i2.Symbol, asc);
					case Undecorated: return SortHelper::Sort(UndecoratedText(i1.Symbol), UndecoratedText(i2.Symbol), asc);
					case Dll: return SortHelper::Sort(i1.Dll, i2.Dll, asc);
					case ImportType: return SortHelper::Sort((int)i1.Type, (int)i2.Type, asc);
					case ImportBy: return SortHelper::Sort((int)i1.NameType, (int)i2.NameType, asc);
					case HintOrdinal: return SortHelper::Sort(i1.OrdinalOrHint, i2.OrdinalOrHint, asc);
					case Machine: return SortHelper::Sort(i1.Machine, i2.Machine, asc);
					case MemberIndex: return SortHelper::Sort(i1.Member, i2.Member, asc);
				}
				break;
			}
		}
		return false;
	};
	std::stable_sort(m_Rows.begin(), m_Rows.end(), compare);
}

bool CArchiveView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	auto member = MemberOf(row);
	if (member < 0)
		return false;
	return Frame()->ShowArchiveMember(member);
}

void CArchiveView::UpdateUI(bool) const {
	if (!m_List)
		return;
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	Frame()->SetStatusText(1, std::format(L"{} items", m_Rows.size()).c_str());
}

LRESULT CArchiveView::OnItemChanged(int, LPNMHDR, BOOL& handled) {
	UpdateUI();
	handled = FALSE;
	return 0;
}

LRESULT CArchiveView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
}

LRESULT CArchiveView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	int index = -1;
	if (m_List.GetItemCount()) {
		index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());
		if (index >= 0) {
			m_List.SelectItem(index);
			m_List.SetFocus();
		}
	}
	if (index < 0)
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

LRESULT CArchiveView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);
	size_t count = 0;
	switch (m_Kind) {
		case ArchiveViewKind::Members:
			cm->AddColumn(L"Name", LVCFMT_LEFT, 320, Column::Name);
			cm->AddColumn(L"#", LVCFMT_RIGHT, 50, Column::Index);
			cm->AddColumn(L"Type", LVCFMT_LEFT, 110, Column::Type);
			cm->AddColumn(L"Machine", LVCFMT_LEFT, 80, Column::Machine);
			cm->AddColumn(L"Size", LVCFMT_RIGHT, 90, Column::Size);
			cm->AddColumn(L"Data Offset", LVCFMT_RIGHT, 90, Column::Offset);
			cm->AddColumn(L"Sections", LVCFMT_RIGHT, 70, Column::Sections);
			cm->AddColumn(L"Symbols", LVCFMT_RIGHT, 70, Column::Symbols);
			cm->AddColumn(L"Date", LVCFMT_LEFT, 150, Column::Date);
			count = m_Archive.Members().size();
			break;

		case ArchiveViewKind::Symbols:
			cm->AddColumn(L"Symbol", LVCFMT_LEFT, 380, Column::Symbol);
			cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 380, Column::Undecorated);
			cm->AddColumn(L"Member", LVCFMT_LEFT, 250, Column::Member);
			cm->AddColumn(L"Member #", LVCFMT_RIGHT, 70, Column::MemberIndex);
			count = m_Archive.Symbols().size();
			break;

		case ArchiveViewKind::Imports:
			cm->AddColumn(L"Symbol", LVCFMT_LEFT, 300, Column::Symbol);
			cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 260, Column::Undecorated);
			cm->AddColumn(L"DLL", LVCFMT_LEFT, 160, Column::Dll);
			cm->AddColumn(L"Type", LVCFMT_LEFT, 60, Column::ImportType);
			cm->AddColumn(L"Import By", LVCFMT_LEFT, 110, Column::ImportBy);
			cm->AddColumn(L"Hint/Ordinal", LVCFMT_RIGHT, 90, Column::HintOrdinal);
			cm->AddColumn(L"Machine", LVCFMT_LEFT, 70, Column::Machine);
			cm->AddColumn(L"Member #", LVCFMT_RIGHT, 70, Column::MemberIndex);
			count = m_Archive.Imports().size();
			break;
	}

	m_Rows.resize(count);
	for (size_t i = 0; i < count; i++)
		m_Rows[i] = (int)i;
	m_List.SetItemCount((int)count);
	return 0;
}
