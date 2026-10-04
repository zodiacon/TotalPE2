#include "pch.h"
#include "DebugInfoView.h"
#include "resource.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include <numeric>

CDebugInfoView::CDebugInfoView(IMainFrame* frame, CoffObject const& obj, CodeViewInfo const& cv, DebugInfoViewKind kind, int member, PCWSTR owner)
	: CViewBase(frame), m_Object(obj), m_CodeView(cv), m_Kind(kind), m_Member(member), m_Owner(owner) {
}

CString CDebugInfoView::GetTitle() const {
	CString title;
	switch (m_Kind) {
		case DebugInfoViewKind::Subsections: title = L"CodeView"; break;
		case DebugInfoViewKind::Symbols: title = L"CodeView Symbols"; break;
		case DebugInfoViewKind::Lines: title = L"Source Lines"; break;
		case DebugInfoViewKind::Files: title = L"Source Files"; break;
		case DebugInfoViewKind::Types: title = L"CodeView Types"; break;
	}
	return m_Owner.IsEmpty() ? title : m_Owner + L": " + title;
}

std::wstring CDebugInfoView::SectionText(uint32_t section) const {
	if (section >= m_Object.Sections().size())
		return std::to_wstring(section + 1);
	auto const& name = m_Object.Sections()[section].Name;
	return std::format(L"{} ({})", section + 1, std::wstring(name.begin(), name.end()));
}

// "4 (.text$mn) + 0x1A"
std::wstring CDebugInfoView::TargetText(CvTarget const& target) const {
	if (target.Section < 0)
		return L"";
	return std::format(L"{} + 0x{:X}", SectionText(target.Section), target.Offset);
}

CString CDebugInfoView::GetColumnText(HWND, int row, int col) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return CString();
	int i = m_Rows[row];
	auto tag = GetColumnManager(m_List)->GetColumnTag<Column>(col);

	switch (m_Kind) {
		case DebugInfoViewKind::Subsections:
		{
			auto const& s = m_CodeView.Subsections[i];
			switch (tag) {
				case Section: return SectionText(s.Section).c_str();
				case Offset: return std::format(L"0x{:X}", s.Offset).c_str();
				case Type: return std::format(L"0x{:X}", s.Type).c_str();
				case Name: return CodeViewInfo::SubsectionName(s.Type).c_str();
				case Size: return std::format(L"0x{:X}", s.Size).c_str();
			}
			break;
		}

		case DebugInfoViewKind::Symbols:
		{
			auto const& s = m_CodeView.Symbols[i];
			switch (tag) {
				case Kind: return CodeViewInfo::SymbolKindName(s.Kind).c_str();
				// the nesting in procedures and blocks
				case Name: return (std::wstring(s.Depth * 4, L' ') + s.Name).c_str();
				case Details: return s.Details.c_str();
				case Target: return TargetText(s.Target).c_str();
				case Section: return SectionText(s.Section).c_str();
				case Offset: return std::format(L"0x{:X}", s.Offset).c_str();
				case Size: return std::to_wstring(s.Size).c_str();
			}
			break;
		}

		case DebugInfoViewKind::Lines:
		{
			auto const& l = m_CodeView.Lines[i];
			switch (tag) {
				case Function: return m_Functions[i].c_str();
				case CodeOffset: return std::format(L"+0x{:X}", l.CodeOffset).c_str();
				case File: return l.File.c_str();
				case Line:
					// lines the debuggers skip
					if (l.Line == 0xFEEFEE || l.Line == 0xF00F00)
						return std::format(L"Hidden (0x{:X})", l.Line).c_str();
					return l.EndLine != l.Line ? std::format(L"{}-{}", l.Line, l.EndLine).c_str() : std::to_wstring(l.Line).c_str();
				case LineColumn:
					if (l.Column == 0)
						return L"";
					return l.EndColumn > l.Column ? std::format(L"{}-{}", l.Column, l.EndColumn).c_str() : std::to_wstring(l.Column).c_str();
				case Statement: return l.Statement ? L"Yes" : L"";
				case Target: return TargetText(l.Target).c_str();
			}
			break;
		}

		case DebugInfoViewKind::Files:
		{
			auto const& f = m_CodeView.Files[i];
			switch (tag) {
				case Id: return std::format(L"0x{:X}", f.Id).c_str();
				case File: return f.Name.c_str();
				case ChecksumKind: return CodeViewInfo::ChecksumKindName(f.ChecksumKind).c_str();
				case Checksum: return f.Checksum.c_str();
			}
			break;
		}

		case DebugInfoViewKind::Types:
		{
			auto const& t = m_CodeView.Types[i];
			switch (tag) {
				case Index: return t.Index ? std::format(L"0x{:X}", t.Index).c_str() : L"";
				case Kind: return CodeViewInfo::TypeKindName(t.Kind).c_str();
				case Name: return t.Name.c_str();
				case Details: return t.Details.c_str();
				case Section: return SectionText(t.Section).c_str();
				case Offset: return std::format(L"0x{:X}", t.Offset).c_str();
				case Size: return std::to_wstring(t.Size).c_str();
			}
			break;
		}
	}
	return CString();
}

bool CDebugInfoView::IsSortable(HWND, int) const {
	return true;
}

void CDebugInfoView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;
	auto tag = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto asc = si->SortAscending;
	auto const& cv = m_CodeView;
	auto targetKey = [](CvTarget const& t) { return ((int64_t)t.Section << 32) | t.Offset; };

	std::ranges::stable_sort(m_Rows, [&](int a, int b) {
		switch (m_Kind) {
			case DebugInfoViewKind::Subsections:
			{
				auto const& x = cv.Subsections[a], & y = cv.Subsections[b];
				switch (tag) {
					case Type: case Name: return SortHelper::Sort(x.Type, y.Type, asc);
					case Size: return SortHelper::Sort(x.Size, y.Size, asc);
				}
				return SortHelper::Sort(a, b, asc);
			}
			case DebugInfoViewKind::Symbols:
			{
				auto const& x = cv.Symbols[a], & y = cv.Symbols[b];
				switch (tag) {
					case Kind: return SortHelper::Sort(CodeViewInfo::SymbolKindName(x.Kind), CodeViewInfo::SymbolKindName(y.Kind), asc);
					case Name: return SortHelper::Sort(x.Name, y.Name, asc);
					case Details: return SortHelper::Sort(x.Details, y.Details, asc);
					case Target: return SortHelper::Sort(targetKey(x.Target), targetKey(y.Target), asc);
					case Size: return SortHelper::Sort(x.Size, y.Size, asc);
				}
				return SortHelper::Sort(a, b, asc);
			}
			case DebugInfoViewKind::Lines:
			{
				auto const& x = cv.Lines[a], & y = cv.Lines[b];
				switch (tag) {
					case Function: return SortHelper::Sort(m_Functions[a], m_Functions[b], asc);
					case CodeOffset: return SortHelper::Sort(x.CodeOffset, y.CodeOffset, asc);
					case File: return SortHelper::Sort(x.File, y.File, asc);
					case Line: return SortHelper::Sort(x.Line, y.Line, asc);
					case LineColumn: return SortHelper::Sort(x.Column, y.Column, asc);
					case Statement: return SortHelper::Sort(x.Statement, y.Statement, asc);
					case Target: return SortHelper::Sort(targetKey(x.Target), targetKey(y.Target), asc);
				}
				return SortHelper::Sort(a, b, asc);
			}
			case DebugInfoViewKind::Files:
			{
				auto const& x = cv.Files[a], & y = cv.Files[b];
				switch (tag) {
					case File: return SortHelper::Sort(x.Name, y.Name, asc);
					case ChecksumKind: return SortHelper::Sort(x.ChecksumKind, y.ChecksumKind, asc);
					case Checksum: return SortHelper::Sort(x.Checksum, y.Checksum, asc);
				}
				return SortHelper::Sort(a, b, asc);
			}
			case DebugInfoViewKind::Types:
			{
				auto const& x = cv.Types[a], & y = cv.Types[b];
				switch (tag) {
					case Kind: return SortHelper::Sort(CodeViewInfo::TypeKindName(x.Kind), CodeViewInfo::TypeKindName(y.Kind), asc);
					case Name: return SortHelper::Sort(x.Name, y.Name, asc);
					case Details: return SortHelper::Sort(x.Details, y.Details, asc);
					case Size: return SortHelper::Sort(x.Size, y.Size, asc);
				}
				return SortHelper::Sort(a, b, asc);
			}
		}
		return false;
	});
}

// the code or the data the row is for, or the record in its .debug$ section
bool CDebugInfoView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	int i = m_Rows[row];
	auto show = [&](uint32_t section, uint32_t offset) {
		return Frame()->ShowObjectSection((int)section, offset, m_Member);
	};
	switch (m_Kind) {
		case DebugInfoViewKind::Subsections:
			return show(m_CodeView.Subsections[i].Section, m_CodeView.Subsections[i].Offset);
		case DebugInfoViewKind::Symbols:
		{
			auto const& s = m_CodeView.Symbols[i];
			return s.Target.Section >= 0 ? show(s.Target.Section, s.Target.Offset) : show(s.Section, s.Offset);
		}
		case DebugInfoViewKind::Lines:
		{
			auto const& l = m_CodeView.Lines[i];
			return l.Target.Section >= 0 ? show(l.Target.Section, l.Target.Offset) : show(l.Section, l.Offset);
		}
		case DebugInfoViewKind::Files:
			return show(m_CodeView.Files[i].Section, m_CodeView.Files[i].Offset);
		case DebugInfoViewKind::Types:
			return show(m_CodeView.Types[i].Section, m_CodeView.Types[i].Offset);
	}
	return false;
}

void CDebugInfoView::OnStateChanged(HWND, int, int, DWORD oldState, DWORD newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CDebugInfoView::UpdateUI(bool first) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	if (!first)
		return;
	std::wstring status = std::format(L"{} items", m_Rows.size());
	if (m_Kind == DebugInfoViewKind::Subsections) {
		if (!m_CodeView.Compiler.empty())
			status += L". Compiler: " + m_CodeView.Compiler;
		if (!m_CodeView.TypeServer.empty())
			status += L". Types in " + m_CodeView.TypeServer;
	}
	if (!m_CodeView.Problems.empty())
		status += L". " + m_CodeView.Problems.front();
	Frame()->SetStatusText(1, status.c_str());
}

LRESULT CDebugInfoView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	size_t count = 0;
	switch (m_Kind) {
		case DebugInfoViewKind::Subsections:
			count = m_CodeView.Subsections.size();
			cm->AddColumn(L"Section", LVCFMT_LEFT, 120, Section);
			cm->AddColumn(L"Offset", LVCFMT_RIGHT, 80, Offset);
			cm->AddColumn(L"Kind", LVCFMT_LEFT, 180, Name);
			cm->AddColumn(L"Type", LVCFMT_RIGHT, 80, Type);
			cm->AddColumn(L"Size", LVCFMT_RIGHT, 80, Size);
			break;

		case DebugInfoViewKind::Symbols:
			count = m_CodeView.Symbols.size();
			cm->AddColumn(L"Kind", LVCFMT_LEFT, 170, Kind);
			cm->AddColumn(L"Name", LVCFMT_LEFT, 280, Name);
			cm->AddColumn(L"Details", LVCFMT_LEFT, 400, Details);
			cm->AddColumn(L"Address", LVCFMT_LEFT, 180, Target);
			cm->AddColumn(L"Record Section", LVCFMT_LEFT, 120, Section);
			cm->AddColumn(L"Record Offset", LVCFMT_RIGHT, 90, Offset);
			cm->AddColumn(L"Size", LVCFMT_RIGHT, 50, Size);
			break;

		case DebugInfoViewKind::Lines:
		{
			count = m_CodeView.Lines.size();
			// the lines refer to the functions by their (decorated) symbols; there are many lines to a function
			std::unordered_map<std::wstring, std::wstring> names;
			m_Functions.reserve(count);
			for (auto const& l : m_CodeView.Lines) {
				auto it = names.find(l.Function);
				if (it == names.end()) {
					auto name = l.Function;
					if (name.starts_with(L"?"))
						name = PEStrings::UndecorateName(name.c_str());
					it = names.emplace(l.Function, std::move(name)).first;
				}
				m_Functions.push_back(it->second);
			}
			cm->AddColumn(L"Function", LVCFMT_LEFT, 250, Function);
			cm->AddColumn(L"Offset", LVCFMT_RIGHT, 70, CodeOffset);
			cm->AddColumn(L"Line", LVCFMT_RIGHT, 80, Line);
			cm->AddColumn(L"Column", LVCFMT_RIGHT, 60, LineColumn);
			cm->AddColumn(L"Statement", LVCFMT_LEFT, 70, Statement);
			cm->AddColumn(L"File", LVCFMT_LEFT, 450, File);
			cm->AddColumn(L"Address", LVCFMT_LEFT, 180, Target);
			break;
		}

		case DebugInfoViewKind::Files:
			count = m_CodeView.Files.size();
			cm->AddColumn(L"ID", LVCFMT_RIGHT, 60, Id);
			cm->AddColumn(L"File", LVCFMT_LEFT, 500, File);
			cm->AddColumn(L"Checksum Kind", LVCFMT_LEFT, 100, ChecksumKind);
			cm->AddColumn(L"Checksum", LVCFMT_LEFT, 480, Checksum);
			break;

		case DebugInfoViewKind::Types:
			count = m_CodeView.Types.size();
			cm->AddColumn(L"Index", LVCFMT_RIGHT, 70, Index);
			cm->AddColumn(L"Kind", LVCFMT_LEFT, 150, Kind);
			cm->AddColumn(L"Name", LVCFMT_LEFT, 280, Name);
			cm->AddColumn(L"Details", LVCFMT_LEFT, 500, Details);
			cm->AddColumn(L"Section", LVCFMT_LEFT, 120, Section);
			cm->AddColumn(L"Offset", LVCFMT_RIGHT, 80, Offset);
			cm->AddColumn(L"Size", LVCFMT_RIGHT, 50, Size);
			break;
	}
	m_Rows.resize(count);
	std::iota(m_Rows.begin(), m_Rows.end(), 0);
	m_List.SetItemCount((int)count);
	return 0;
}

LRESULT CDebugInfoView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr || m_List.GetItemCount() == 0)
		return 0;
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

LRESULT CDebugInfoView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List));
	return 0;
}
