#include "pch.h"
#include "SymbolsView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include "resource.h"
#include "PEStrings.h"
#include "TypeExporter.h"
#include <unordered_set>

CSymbolsView::CSymbolsView(IMainFrame* frame, DiaSession const& session, SymViewType type)
	: CViewBase(frame), m_Session(session), m_Type(type) {
}

CString CSymbolsView::GetColumnText(HWND h, int row, int col) const {
	auto& item = m_Items[row];
	switch (GetColumnManager(h)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return item.Name.c_str();
		case ColumnType::SymType: return PEStrings::SymbolTagToString(item.Tag);
		case ColumnType::Type: GetDetails(item); return item.TypeName.c_str();
		case ColumnType::RVA: return item.Rva ? std::format(L"0x{:X}", item.Rva).c_str() : L"";
		case ColumnType::Size: return item.Size ? std::format(L"0x{:X}", item.Size).c_str() : L"";
		case ColumnType::Location: return item.Location == LocationKind::Null ? L"" : PEStrings::SymbolLocationToString(item.Location);
		case ColumnType::DataKind: return item.Tag == SymbolTag::Data ? DataKindToString(item.Symbol.Kind()) : L"";
		case ColumnType::UndecoratedName: GetDetails(item); return item.Undecorated.c_str();
	}
	return CString();
}

int CSymbolsView::GetRowImage(HWND, int row, int) const {
	auto& item = m_Items[row];
	UINT icon;
	switch (item.Tag) {
		case SymbolTag::Function:
		case SymbolTag::Thunk:
			icon = IDI_FUNCTION;
			break;
		case SymbolTag::PublicSymbol:
			icon = item.Symbol.IsCode() ? IDI_FUNCTION : IDI_DATA;
			break;
		case SymbolTag::Data:
			icon = IDI_DATA;
			break;
		case SymbolTag::UDT:
			switch (item.Symbol.UdtKind()) {
				case UdtType::Class: icon = IDI_CLASS; break;
				case UdtType::Union: icon = IDI_UNION; break;
				default: icon = IDI_TYPE; break;
			}
			break;
		case SymbolTag::Enum:
			icon = IDI_ENUM;
			break;
		default:
			icon = IDI_SYMBOLS;
			break;
	}
	return Frame()->GetIconIndex(icon);
}

// The type of a symbol, as far as it has one: of data its type, of a function its return type, of a UDT its kind
void CSymbolsView::GetDetails(Item const& item) const {
	if (item.HasDetails)
		return;
	item.HasDetails = true;

	auto& sym = item.Symbol;
	switch (item.Tag) {
		case SymbolTag::UDT:
			switch (sym.UdtKind()) {
				case UdtType::Struct: item.TypeName = L"struct"; break;
				case UdtType::Class: item.TypeName = L"class"; break;
				case UdtType::Union: item.TypeName = L"union"; break;
			}
			break;

		case SymbolTag::Enum:
			item.TypeName = L"enum";
			break;

		case SymbolTag::Function:
			if (auto type = sym.Type())
				if (auto ret = type.Type())
					item.TypeName = ret.Name() + L" ()";
			break;

		default:
			if (auto type = sym.Type())
				item.TypeName = type.Name();
			break;
	}

	if (item.Tag == SymbolTag::Function || item.Tag == SymbolTag::Data || item.Tag == SymbolTag::PublicSymbol || item.Tag == SymbolTag::Thunk) {
		item.Undecorated = sym.UndecoratedName();
		if (item.Undecorated.empty() || item.Undecorated == item.Name)
			item.Undecorated = PEStrings::UndecorateName(item.Name.c_str());
		if (item.Undecorated == item.Name)
			item.Undecorated.clear();
	}
}

PCWSTR CSymbolsView::DataKindToString(DataItemKind kind) {
	switch (kind) {
		case DataItemKind::Local: return L"Local";
		case DataItemKind::StaticLocal: return L"Static Local";
		case DataItemKind::Param: return L"Parameter";
		case DataItemKind::ObjectPtr: return L"Object Pointer";
		case DataItemKind::FileStatic: return L"File Static";
		case DataItemKind::Global: return L"Global";
		case DataItemKind::Member: return L"Member";
		case DataItemKind::StaticMember: return L"Static Member";
		case DataItemKind::Constant: return L"Constant";
	}
	return L"";
}

void CSymbolsView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	auto col = GetColumnManager(si->hWnd)->GetColumnTag<ColumnType>(si->SortColumn);
	if (col == ColumnType::Type || col == ColumnType::UndecoratedName) {
		CWaitCursor wait;
		for (auto& item : m_Items)
			GetDetails(item);
	}
	auto compare = [&](auto& s1, auto& s2) {
		switch (col) {
			case ColumnType::Name: return SortHelper::Sort(s1.Name, s2.Name, asc);
			case ColumnType::SymType: return SortHelper::Sort(PEStrings::SymbolTagToString(s1.Tag), PEStrings::SymbolTagToString(s2.Tag), asc);
			case ColumnType::Type: return SortHelper::Sort(s1.TypeName, s2.TypeName, asc);
			case ColumnType::RVA: return SortHelper::Sort(s1.Rva, s2.Rva, asc);
			case ColumnType::Size: return SortHelper::Sort(s1.Size, s2.Size, asc);
			case ColumnType::Location: return SortHelper::Sort((int)s1.Location, (int)s2.Location, asc);
			case ColumnType::DataKind:
				return SortHelper::Sort(s1.Tag == SymbolTag::Data ? DataKindToString(s1.Symbol.Kind()) : L"",
					s2.Tag == SymbolTag::Data ? DataKindToString(s2.Symbol.Kind()) : L"", asc);
			case ColumnType::UndecoratedName: return SortHelper::Sort(s1.Undecorated, s2.Undecorated, asc);
		}
		return false;
	};
	std::ranges::sort(m_Items, compare);
}

void CSymbolsView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

// The layout of a type, or the code of a function (or of a public symbol, thunk or label in code), in a view of its own
bool CSymbolsView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	auto& item = m_Items[row];
	// a type: its layout (of a typedef, that of the type it names)
	if (item.Tag == SymbolTag::UDT || item.Tag == SymbolTag::Enum)
		return Frame()->ShowType(item.Symbol);
	if (item.Tag == SymbolTag::Typedef) {
		auto type = item.Symbol.Type();
		return type && (type.Tag() == SymbolTag::UDT || type.Tag() == SymbolTag::Enum) && Frame()->ShowType(type);
	}
	if (item.Rva == 0)
		return false;
	switch (item.Tag) {
		case SymbolTag::Function:
		case SymbolTag::Thunk:
			break;
		case SymbolTag::PublicSymbol:
		case SymbolTag::Label:
			if (!item.Symbol.IsCode())
				return false;
			break;
		default:
			return false;
	}
	return Frame()->DisassembleFunction(m_Session.LoadAddress() + item.Rva);
}

// A type (or a typedef of one) can be saved as a definition
bool CSymbolsView::OnRightClickList(HWND, int row, int, POINT const& pt) const {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	auto type = TypeExporter::ExportableType(m_Items[row].Symbol);
	if (!type)
		return false;
	TypeExporter::ShowMenu(Frame(), m_hWnd, type, pt);
	return true;
}

CString CSymbolsView::GetTitle() const {
	switch (m_Type) {
		case SymViewType::All: return L"Symbols";
		case SymViewType::Class: return L"Class";
		case SymViewType::Struct: return L"Struct";
		case SymViewType::Function: return L"Function";
		case SymViewType::Data: return L"Data";
		case SymViewType::Enum: return L"Enum";
		case SymViewType::UDT: return L"Types";
	}
	ATLASSERT(false);
	return L"(Symbols)";
}

LRESULT CSymbolsView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 300, ColumnType::Name);
	cm->AddColumn(L"Symbol Type", LVCFMT_LEFT, 110, ColumnType::SymType);
	cm->AddColumn(L"Type", LVCFMT_LEFT, 200, ColumnType::Type);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90, ColumnType::RVA);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 70, ColumnType::Size);
	cm->AddColumn(L"Location", LVCFMT_LEFT, 90, ColumnType::Location);
	cm->AddColumn(L"Data Kind", LVCFMT_LEFT, 90, ColumnType::DataKind);
	cm->AddColumn(L"Undecorated Name", LVCFMT_LEFT, 400, ColumnType::UndecoratedName);

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
	if (first)
		Frame()->SetStatusText(1, std::format(L"Symbols: {}", m_Items.size()).c_str());
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

// The kinds of symbols of the global scope a list has
std::vector<SymbolTag> CSymbolsView::Tags(SymViewType type) {
	switch (type) {
		// public symbols too: symbols without private information (such as Microsoft's) have no functions
		case SymViewType::Function: return { SymbolTag::Function, SymbolTag::Thunk, SymbolTag::PublicSymbol };
		case SymViewType::UDT: case SymViewType::Class: case SymViewType::Struct: return { SymbolTag::UDT };
		case SymViewType::Enum: return { SymbolTag::Enum };
		case SymViewType::Data: return { SymbolTag::Data };
		// the named things of the global scope (not compilands, or types such as pointers and arrays)
		case SymViewType::All:
			return { SymbolTag::Function, SymbolTag::Data, SymbolTag::PublicSymbol, SymbolTag::Thunk,
				SymbolTag::Label, SymbolTag::UDT, SymbolTag::Enum, SymbolTag::Typedef };
	}
	return {};
}

// Whether a symbol of one of the kinds belongs in the list
bool CSymbolsView::Includes(SymViewType type, SymbolTag tag, DiaSymbol const& sym) {
	switch (type) {
		case SymViewType::Class: return sym.UdtKind() == UdtType::Class;
		case SymViewType::Struct: return sym.UdtKind() == UdtType::Struct || sym.UdtKind() == UdtType::Union;
		case SymViewType::Function: return tag != SymbolTag::PublicSymbol || sym.IsCode();
	}
	return true;
}

bool CSymbolsView::HasSymbols(DiaSession const& session, SymViewType type) {
	for (auto tag : Tags(type))
		for (auto const& sym : session.FindChildren(nullptr, tag))
			if (Includes(type, tag, sym))
				return true;
	return false;
}

void CSymbolsView::BuildItems() {
	std::unordered_set<uint32_t> functions;	// the RVAs of functions: their public symbols are not listed again
	for (auto tag : Tags(m_Type)) {
		for (auto& sym : m_Session.FindChildren(nullptr, tag)) {
			if (!Includes(m_Type, tag, sym))
				continue;
			if (m_Type == SymViewType::Function && tag == SymbolTag::PublicSymbol && functions.contains(sym.RelativeVirtualAddress()))
				continue;
			Item item{ sym };
			item.Name = sym.Name();
			item.Tag = tag;
			item.Location = sym.Location();
			item.Rva = item.Location == LocationKind::Static ? sym.RelativeVirtualAddress() : 0;
			// the size of a typedef is that of its type: not of interest
			item.Size = tag == SymbolTag::Typedef ? 0 : sym.Length();
			if (m_Type == SymViewType::Function && tag != SymbolTag::PublicSymbol)
				functions.insert(item.Rva);
			m_Items.push_back(std::move(item));
		}
	}

	m_List.SetItemCount((int)m_Items.size());
}
