#include "pch.h"
#include "ObjectView.h"
#include "LibArchive.h"
#include "PEStrings.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include <numeric>

namespace {
	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	std::wstring Undecorated(std::string const& name) {
		if (name.empty() || (name[0] != '?' && name[0] != '_' && name[0] != '@'))
			return L"";
		auto s = PEStrings::UndecorateName(name.c_str());
		return s == name ? L"" : Widen(s);
	}

	std::wstring TypeText(uint16_t type) {
		if ((type >> 4) == IMAGE_SYM_DTYPE_FUNCTION)
			return L"Function";
		return type ? std::format(L"0x{:X}", type) : L"";
	}
}

CObjectView::CObjectView(IMainFrame* frame, CoffObject const& obj, ObjectViewKind kind) : CViewBase(frame), m_Object(obj), m_Kind(kind) {
}

CString CObjectView::GetTitle() const {
	switch (m_Kind) {
		case ObjectViewKind::Header: return L"Object Header";
		case ObjectViewKind::Sections: return L"Object Sections";
		case ObjectViewKind::Symbols: return L"Object Symbols";
	}
	return L"Object Relocations";
}

std::wstring CObjectView::SectionText(int32_t number) const {
	if (number > 0 && number <= (int32_t)m_Object.Sections().size())
		return std::format(L"{} ({})", number, Widen(m_Object.Sections()[number - 1].Name));
	return CoffObject::SectionNumberName(number);
}

std::wstring CObjectView::SymbolNameAt(uint32_t index) const {
	auto sym = m_Object.SymbolByIndex(index);
	return sym ? Widen(sym->Name) : L"?";
}

void CObjectView::BuildHeader() {
	auto machine = LibArchive::MachineName(m_Object.Machine());
	auto add = [&](std::wstring name, std::wstring value, std::wstring details = L"") {
		m_Header.push_back({ std::move(name), std::move(value), std::move(details) });
	};
	add(L"Format", m_Object.BigObj() ? L"COFF (bigobj)" : L"COFF", m_Object.BigObj() ? L"Up to 2^31 sections, 32-bit section numbers" : L"");
	add(L"Machine", std::format(L"{} (0x{:X})", machine.empty() ? L"Unknown" : std::wstring(machine), m_Object.Machine()));
	add(L"Sections", std::to_wstring(m_Object.Sections().size()));
	add(L"Symbols", std::to_wstring(m_Object.Symbols().size()), std::format(L"{} entries in the symbol table, with the auxiliary records", m_Object.SymbolTableCount()));
	add(L"Relocations", std::to_wstring(m_Object.Relocations().size()));
	add(L"Time/Date Stamp", std::format(L"0x{:08X}", m_Object.TimeDateStamp()),
		m_Object.TimeDateStamp() ? PEStrings::Sec1970ToString(m_Object.TimeDateStamp()) : L"Not set (reproducible build)");
	if (!m_Object.BigObj())
		add(L"Characteristics", std::format(L"0x{:04X}", m_Object.Characteristics()), CoffObject::CharacteristicsToString(m_Object.Characteristics()));
	add(L"Symbol Table", std::format(L"0x{:X}", m_Object.SymbolTableOffset()), L"Offset in the file");
	add(L"String Table", std::format(L"{} bytes", m_Object.StringTableSize()));
	add(L"File Size", PEStrings::ToMemorySize(m_Object.Data().size()));

	// the options for the linker, one per row
	auto directives = m_Object.Directives();
	size_t pos = 0;
	while (pos < directives.size()) {
		while (pos < directives.size() && directives[pos] == ' ')
			pos++;
		if (pos >= directives.size())
			break;
		// an option ends at a space that is not in quotes
		size_t end = pos;
		bool quoted = false;
		while (end < directives.size() && (quoted || directives[end] != ' ')) {
			if (directives[end] == '"')
				quoted = !quoted;
			end++;
		}
		add(L"Linker Directive", Widen(directives.substr(pos, end - pos)));
		pos = end;
	}
	for (auto const& problem : m_Object.Problems())
		add(L"Problem", problem);
}

CString CObjectView::GetColumnText(HWND, int row, int col) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return CString();
	int i = m_Rows[row];
	auto tag = GetColumnManager(m_List)->GetColumnTag<Column>(col);

	switch (m_Kind) {
		case ObjectViewKind::Header:
		{
			auto const& h = m_Header[i];
			switch (tag) {
				case Name: return h.Name.c_str();
				case Value: return h.Value.c_str();
				case Details: return h.Details.c_str();
			}
			break;
		}

		case ObjectViewKind::Sections:
		{
			auto const& s = m_Object.Sections()[i];
			switch (tag) {
				case Number: return std::to_wstring(i + 1).c_str();
				case SectionName: return Widen(s.Name).c_str();
				case RawSize: return std::format(L"0x{:X}", s.SizeOfRawData).c_str();
				case RawOffset: return s.PointerToRawData ? std::format(L"0x{:X}", s.PointerToRawData).c_str() : L"";
				case RelocCount: return s.NumberOfRelocations ? std::to_wstring(s.NumberOfRelocations).c_str() : L"";
				case RelocOffset: return s.NumberOfRelocations ? std::format(L"0x{:X}", s.PointerToRelocations).c_str() : L"";
				case Characteristics: return std::format(L"0x{:08X} ({})", s.Characteristics, PEStrings::SectionCharacteristicsToString(s.Characteristics)).c_str();
			}
			break;
		}

		case ObjectViewKind::Symbols:
		{
			auto const& s = m_Object.Symbols()[i];
			switch (tag) {
				case Index: return std::to_wstring(s.Index).c_str();
				case SymbolName: return Widen(s.Name).c_str();
				case Undecorated: return ::Undecorated(s.Name).c_str();
				case SymbolValue: return std::format(L"0x{:X}", s.Value).c_str();
				case Section: return SectionText(s.SectionNumber).c_str();
				case Type: return TypeText(s.Type).c_str();
				case StorageClass: return CoffObject::StorageClassName(s.StorageClass).c_str();
				case Aux: return s.AuxCount ? std::to_wstring(s.AuxCount).c_str() : L"";
				case SymbolDetails: return Widen(s.Details).c_str();
			}
			break;
		}

		case ObjectViewKind::Relocations:
		{
			auto const& r = m_Object.Relocations()[i];
			switch (tag) {
				case RelocSection: return SectionText(r.Section + 1).c_str();
				case RelocOffsetInSection: return std::format(L"0x{:X}", r.Offset).c_str();
				case RelocType: return CoffObject::RelocationTypeName(m_Object.Machine(), r.Type).c_str();
				case RelocSymbol: return SymbolNameAt(r.SymbolIndex).c_str();
				case RelocSymbolIndex: return std::to_wstring(r.SymbolIndex).c_str();
			}
			break;
		}
	}
	return CString();
}

// the sections have the icon they have in the tree
int CObjectView::GetRowImage(HWND, int, int) const {
	return m_Kind == ObjectViewKind::Sections ? Frame()->GetIconIndex(IDI_SECTION) : -1;
}

bool CObjectView::IsSortable(HWND, int) const {
	return m_Kind != ObjectViewKind::Header;
}

void CObjectView::DoSort(SortInfo const* si) {
	if (si == nullptr || m_Kind == ObjectViewKind::Header)
		return;
	auto tag = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto asc = si->SortAscending;
	auto const& sections = m_Object.Sections();
	auto const& symbols = m_Object.Symbols();
	auto const& relocs = m_Object.Relocations();

	std::ranges::stable_sort(m_Rows, [&](int a, int b) {
		switch (m_Kind) {
			case ObjectViewKind::Sections:
			{
				auto const& x = sections[a], & y = sections[b];
				switch (tag) {
					case SectionName: return SortHelper::Sort(x.Name, y.Name, asc);
					case RawSize: return SortHelper::Sort(x.SizeOfRawData, y.SizeOfRawData, asc);
					case RawOffset: return SortHelper::Sort(x.PointerToRawData, y.PointerToRawData, asc);
					case RelocCount: return SortHelper::Sort(x.NumberOfRelocations, y.NumberOfRelocations, asc);
					case RelocOffset: return SortHelper::Sort(x.PointerToRelocations, y.PointerToRelocations, asc);
					case Characteristics: return SortHelper::Sort(x.Characteristics, y.Characteristics, asc);
				}
				return SortHelper::Sort(a, b, asc);
			}
			case ObjectViewKind::Symbols:
			{
				auto const& x = symbols[a], & y = symbols[b];
				switch (tag) {
					case SymbolName: return SortHelper::Sort(x.Name, y.Name, asc);
					case Undecorated: return SortHelper::Sort(::Undecorated(x.Name), ::Undecorated(y.Name), asc);
					case SymbolValue: return SortHelper::Sort(x.Value, y.Value, asc);
					case Section: return SortHelper::Sort(x.SectionNumber, y.SectionNumber, asc);
					case Type: return SortHelper::Sort(x.Type, y.Type, asc);
					case StorageClass: return SortHelper::Sort(x.StorageClass, y.StorageClass, asc);
					case Aux: return SortHelper::Sort(x.AuxCount, y.AuxCount, asc);
					case SymbolDetails: return SortHelper::Sort(x.Details, y.Details, asc);
				}
				return SortHelper::Sort(x.Index, y.Index, asc);
			}
			case ObjectViewKind::Relocations:
			{
				auto const& x = relocs[a], & y = relocs[b];
				switch (tag) {
					case RelocOffsetInSection: return SortHelper::Sort(x.Offset, y.Offset, asc);
					case RelocType: return SortHelper::Sort(x.Type, y.Type, asc);
					case RelocSymbol: return SortHelper::Sort(SymbolNameAt(x.SymbolIndex), SymbolNameAt(y.SymbolIndex), asc);
					case RelocSymbolIndex: return SortHelper::Sort(x.SymbolIndex, y.SymbolIndex, asc);
				}
				return x.Section != y.Section ? SortHelper::Sort(x.Section, y.Section, asc) : SortHelper::Sort(x.Offset, y.Offset, asc);
			}
		}
		return false;
	});
}

// the place in the data of the section
bool CObjectView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	int i = m_Rows[row];
	switch (m_Kind) {
		case ObjectViewKind::Sections:
			return Frame()->ShowObjectSection(i, -1);
		case ObjectViewKind::Symbols:
		{
			auto const& s = m_Object.Symbols()[i];
			if (s.SectionNumber <= 0 || s.SectionNumber > (int32_t)m_Object.Sections().size())
				return false;
			return Frame()->ShowObjectSection(s.SectionNumber - 1, s.Value);
		}
		case ObjectViewKind::Relocations:
		{
			auto const& r = m_Object.Relocations()[i];
			return Frame()->ShowObjectSection(r.Section, r.Offset);
		}
	}
	return false;
}

void CObjectView::OnStateChanged(HWND, int, int, DWORD oldState, DWORD newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CObjectView::UpdateUI(bool first) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	if (first && m_Kind != ObjectViewKind::Header)
		Frame()->SetStatusText(1, std::format(L"{}: {}", (PCWSTR)GetTitle().Mid(7), m_Rows.size()).c_str());
}

LRESULT CObjectView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	if (m_Kind == ObjectViewKind::Sections)
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	auto cm = GetColumnManager(m_List);

	size_t count = 0;
	switch (m_Kind) {
		case ObjectViewKind::Header:
			BuildHeader();
			count = m_Header.size();
			cm->AddColumn(L"Name", LVCFMT_LEFT, 150, Name);
			cm->AddColumn(L"Value", LVCFMT_LEFT, 350, Value);
			cm->AddColumn(L"Details", LVCFMT_LEFT, 450, Details);
			break;

		case ObjectViewKind::Sections:
			count = m_Object.Sections().size();
			cm->AddColumn(L"#", LVCFMT_RIGHT, 50, Number);
			cm->AddColumn(L"Name", LVCFMT_LEFT, 180, SectionName);
			cm->AddColumn(L"Size", LVCFMT_RIGHT, 80, RawSize);
			cm->AddColumn(L"Offset", LVCFMT_RIGHT, 80, RawOffset);
			cm->AddColumn(L"Relocations", LVCFMT_RIGHT, 80, RelocCount);
			cm->AddColumn(L"Relocations Offset", LVCFMT_RIGHT, 110, RelocOffset);
			cm->AddColumn(L"Characteristics", LVCFMT_LEFT, 450, Characteristics);
			break;

		case ObjectViewKind::Symbols:
			count = m_Object.Symbols().size();
			cm->AddColumn(L"Index", LVCFMT_RIGHT, 60, Index);
			cm->AddColumn(L"Name", LVCFMT_LEFT, 300, SymbolName);
			cm->AddColumn(L"Undecorated", LVCFMT_LEFT, 300, Undecorated);
			cm->AddColumn(L"Value", LVCFMT_RIGHT, 80, SymbolValue);
			cm->AddColumn(L"Section", LVCFMT_LEFT, 150, Section);
			cm->AddColumn(L"Type", LVCFMT_LEFT, 70, Type);
			cm->AddColumn(L"Storage Class", LVCFMT_LEFT, 110, StorageClass);
			cm->AddColumn(L"Aux", LVCFMT_RIGHT, 40, Aux);
			cm->AddColumn(L"Details", LVCFMT_LEFT, 400, SymbolDetails);
			break;

		case ObjectViewKind::Relocations:
			count = m_Object.Relocations().size();
			cm->AddColumn(L"Section", LVCFMT_LEFT, 180, RelocSection);
			cm->AddColumn(L"Offset", LVCFMT_RIGHT, 80, RelocOffsetInSection);
			cm->AddColumn(L"Type", LVCFMT_LEFT, 130, RelocType);
			cm->AddColumn(L"Symbol", LVCFMT_LEFT, 400, RelocSymbol);
			cm->AddColumn(L"Symbol Index", LVCFMT_RIGHT, 90, RelocSymbolIndex);
			break;
	}
	m_Rows.resize(count);
	std::iota(m_Rows.begin(), m_Rows.end(), 0);
	m_List.SetItemCount((int)count);
	return 0;
}

LRESULT CObjectView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
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

LRESULT CObjectView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List));
	return 0;
}
