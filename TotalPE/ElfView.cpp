#include "pch.h"
#include "ElfView.h"
#include "PEStrings.h"
#include "ItaniumDemangle.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>

namespace {
	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	std::wstring Hex(uint64_t value) {
		return std::format(L"0x{:X}", value);
	}

	// the dynamic entries whose value is the address of code: DT_INIT, DT_FINI
	bool IsCodeTag(int64_t tag) {
		return tag == 12 || tag == 13;	// DT_INIT, DT_FINI
	}

	// the dynamic entries whose value is an address
	bool IsAddressTag(int64_t tag) {
		switch (tag) {
			case 3: case 4: case 5: case 6: case 7: case 12: case 13: case 17: case 21: case 23: case 25: case 26: case 32: case 36:
			case 0x6FFFFEF5: case 0x6FFFFFF0: case 0x6FFFFFFC: case 0x6FFFFFFE:
				return true;
		}
		return false;
	}

	// a C++ name as it is in the source, or nothing
	std::wstring Demangled(std::string const& name) {
		auto d = DemangleItanium(name);
		return std::wstring(d.begin(), d.end());
	}

	uint64_t NumberOf(std::wstring const& text) {
		if (text.starts_with(L"0x"))
			return wcstoull(text.c_str() + 2, nullptr, 16);
		return wcstoull(text.c_str(), nullptr, 10);
	}
}

CElfView::CElfView(IMainFrame* frame, ElfFile const& elf, ElfViewKind kind, ElfDebugInfo const* debug)
	: CViewBase(frame), m_Elf(elf), m_Debug(debug), m_Kind(kind) {
}

CString CElfView::GetTitle() const {
	switch (m_Kind) {
		case ElfViewKind::Header: return L"ELF Header";
		case ElfViewKind::ProgramHeaders: return L"Program Headers";
		case ElfViewKind::Sections: return L"ELF Sections";
		case ElfViewKind::Symbols: return L"ELF Symbols";
		case ElfViewKind::Dynamic: return L"Dynamic";
		case ElfViewKind::Relocations: return L"ELF Relocations";
		case ElfViewKind::DebugInfo: return L"Debug Info";
		case ElfViewKind::CompileUnits: return L"Compile Units";
		case ElfViewKind::Functions: return L"Functions (DWARF)";
	}
	return L"Notes";
}

std::wstring CElfView::SectionName(uint32_t index) const {
	switch (index) {
		case 0: return L"UND";
		case 0xFFF1: return L"ABS";
		case 0xFFF2: return L"COMMON";
	}
	if (index < m_Elf.Sections().size())
		return std::format(L"{} ({})", index, Widen(m_Elf.Sections()[index].Name));
	return std::to_wstring(index);
}

void CElfView::BuildHeader() {
	m_Columns = { { L"Name", 160 }, { L"Value", 350 }, { L"Details", 450 } };
	auto add = [&](std::wstring name, std::wstring value, std::wstring details = L"", Target go = Target::None, uint64_t where = 0) {
		Row row{ { std::move(name), std::move(value), std::move(details) }, go, where, -1, go == Target::Address };
		m_Rows.push_back(std::move(row));
	};
	add(L"Class", m_Elf.Is64() ? L"ELF64" : L"ELF32");
	add(L"Byte Order", m_Elf.BigEndian() ? L"Big Endian" : L"Little Endian");
	add(L"OS/ABI", ElfFile::OsAbiName(m_Elf.OsAbi()), m_Elf.AbiVersion() ? std::format(L"ABI version {}", m_Elf.AbiVersion()) : L"");
	add(L"Type", ElfFile::TypeName(m_Elf.Type()), m_Elf.Type() == 3 && !m_Elf.Interpreter().empty() ? L"A position independent executable (it has an interpreter)" : L"");
	add(L"Machine", ElfFile::MachineName(m_Elf.Machine()), std::format(L"{}", m_Elf.Machine()));
	if (m_Elf.Flags())
		add(L"Flags", Hex(m_Elf.Flags()));
	if (m_Elf.Entry())
		add(L"Entry Point", Hex(m_Elf.Entry()), L"Double-click to see the code", Target::Address, m_Elf.Entry());
	if (!m_Elf.Interpreter().empty())
		add(L"Interpreter", Widen(m_Elf.Interpreter()), L"The dynamic linker that loads the program");
	for (auto const& lib : m_Elf.NeededLibraries())
		add(L"Needed Library", Widen(lib));
	if (auto s = m_Elf.DynamicString(14); !s.empty())
		add(L"SONAME", Widen(s));
	if (auto s = m_Elf.DynamicString(29); !s.empty())
		add(L"RUNPATH", Widen(s));
	if (auto s = m_Elf.DynamicString(15); !s.empty())
		add(L"RPATH", Widen(s));
	if (auto id = m_Elf.BuildId(); !id.empty())
		add(L"Build ID", Widen(id));
	add(L"Program Headers", std::to_wstring(m_Elf.ProgramHeaders().size()), std::format(L"At 0x{:X}", m_Elf.ProgramHeaderOffset()));
	add(L"Sections", std::to_wstring(m_Elf.Sections().size()), std::format(L"At 0x{:X}", m_Elf.SectionHeaderOffset()));
	add(L"Symbols", std::to_wstring(m_Elf.Symbols().size()));
	if (m_Debug) {
		if (m_Debug->Link.Present)
			add(L"Debug Link", Widen(m_Debug->Link.File), std::format(L"CRC 0x{:08X}{}", m_Debug->Link.Crc,
				m_Debug->DebugFile.empty() ? L"" : L", found: " + m_Debug->DebugFile));
		add(L"Debug Info", m_Debug->Dwarf.Empty() ? L"None" : std::format(L"DWARF, {} compile units, {} functions",
			m_Debug->Dwarf.Units.size(), m_Debug->Dwarf.Functions.size()), m_Debug->DebugFile.empty() ? L"" : L"In the debug file");
	}
	add(L"File Size", PEStrings::ToMemorySize(m_Elf.Data().size()));
	for (auto const& p : m_Elf.Problems())
		add(L"Problem", p);
}

void CElfView::BuildProgramHeaders() {
	m_Columns = { { L"#", 40, true }, { L"Type", 120 }, { L"Flags", 60 }, { L"Offset", 90, true }, { L"Virtual Address", 130, true },
		{ L"Physical Address", 130, true }, { L"File Size", 90, true }, { L"Memory Size", 90, true }, { L"Align", 70, true }, { L"Sections", 400 } };
	int i = 0;
	for (auto const& ph : m_Elf.ProgramHeaders()) {
		// the sections that the segment holds
		std::wstring sections;
		for (auto const& s : m_Elf.Sections()) {
			if (s.Size && s.Type != 8 && s.Offset >= ph.Offset && s.Offset + s.Size <= ph.Offset + ph.FileSize && !s.Name.empty())
				sections += (sections.empty() ? L"" : L" ") + Widen(s.Name);
		}
		m_Rows.push_back({ { std::to_wstring(i++), ElfFile::SegmentTypeName(ph.Type), ElfFile::SegmentFlagsToString(ph.Flags), Hex(ph.Offset),
			Hex(ph.VirtualAddress), Hex(ph.PhysicalAddress), Hex(ph.FileSize), Hex(ph.MemorySize), Hex(ph.Align), sections },
			ph.FileSize ? Target::FileOffset : Target::None, ph.Offset });
		// a loaded segment that is executable: its code
		if (ph.Type == 1 && (ph.Flags & 1) && ph.FileSize && !IsRelocatable()) {
			auto& row = m_Rows.back();
			row.Go = Target::Address;
			row.Where = ph.VirtualAddress;
			row.Code = true;
		}
	}
}

void CElfView::BuildSections() {
	m_Columns = { { L"#", 40, true }, { L"Name", 170 }, { L"Type", 120 }, { L"Flags", 170 }, { L"Address", 120, true }, { L"Offset", 90, true },
		{ L"Size", 90, true }, { L"Link", 50, true }, { L"Info", 50, true }, { L"Align", 60, true }, { L"Entry Size", 70, true } };
	int i = 0;
	for (auto const& s : m_Elf.Sections()) {
		m_Rows.push_back({ { std::to_wstring(i), Widen(s.Name), ElfFile::SectionTypeName(s.Type), ElfFile::SectionFlagsToString(s.Flags),
			s.Address ? Hex(s.Address) : L"", Hex(s.Offset), Hex(s.Size), std::to_wstring(s.Link), std::to_wstring(s.Info),
			std::to_wstring(s.AddressAlign), s.EntrySize ? std::to_wstring(s.EntrySize) : L"" }, Target::SectionOffset, 0, i,
			(s.Flags & 4) != 0 });	// SHF_EXECINSTR: disassembled
		i++;
	}
}

void CElfView::BuildSymbols() {
	m_Columns = { { L"Table", 70 }, { L"#", 50, true }, { L"Name", 350 }, { L"Demangled", 350 }, { L"Value", 120, true }, { L"Size", 70, true },
		{ L"Type", 80 }, { L"Bind", 70 }, { L"Visibility", 80 }, { L"Section", 170 } };
	for (auto const& s : m_Elf.Symbols()) {
		Row row{ { s.Dynamic ? L".dynsym" : L".symtab", std::to_wstring(s.Index), Widen(s.Name), Demangled(s.Name), Hex(s.Value), Hex(s.Size),
			ElfFile::SymbolTypeName(s.Type), ElfFile::SymbolBindName(s.Bind), ElfFile::SymbolVisibilityName(s.Visibility), SectionName(s.SectionIndex) } };
		// functions (and the GNU indirect ones) are disassembled; data and thread locals are shown in hex
		bool defined = s.SectionIndex != 0 && s.SectionIndex < 0xFF00 && s.SectionIndex < m_Elf.Sections().size();
		if (defined && (s.Type == 1 || s.Type == 2 || s.Type == 6 || s.Type == 10)) {
			row.Code = s.Type == 2 || s.Type == 10;
			if (IsRelocatable()) {
				// the value is an offset in the symbol's section
				row.Go = Target::SectionOffset;
				row.Section = (int)s.SectionIndex;
			}
			else
				row.Go = Target::Address;
			row.Where = s.Value;
		}
		m_Rows.push_back(std::move(row));
	}
}

void CElfView::BuildDynamic() {
	m_Columns = { { L"Tag", 140 }, { L"Value", 140, true }, { L"Details", 450 } };
	for (auto const& e : m_Elf.Dynamic()) {
		std::wstring details = Widen(e.Text);
		if (e.Tag == 30)
			details = PEStrings::ToHex((uint32_t)e.Value) + L" (" +
				std::wstring(e.Value & 8 ? L"BIND_NOW " : L"") + (e.Value & 1 ? L"ORIGIN " : L"") + (e.Value & 4 ? L"TEXTREL " : L"") + L")";
		else if (e.Tag == 0x6FFFFFFB)
			details = std::wstring(e.Value & 1 ? L"NOW " : L"") + (e.Value & 0x08000000 ? L"PIE " : L"") + (e.Value & 8 ? L"NODELETE " : L"");
		m_Rows.push_back({ { ElfFile::DynamicTagName(e.Tag), Hex(e.Value), details }, IsAddressTag(e.Tag) ? Target::Address : Target::None, e.Value, -1,
			IsCodeTag(e.Tag) });
	}
}

void CElfView::BuildRelocations() {
	m_Columns = { { L"Section", 150 }, { L"Offset", 130, true }, { L"Type", 200 }, { L"Symbol", 300 }, { L"Demangled", 300 },
		{ L"Symbol Index", 80, true }, { L"Addend", 100 } };
	auto const& sections = m_Elf.Sections();
	for (auto const& r : m_Elf.Relocations()) {
		auto const& sec = sections[r.Section];
		std::wstring addend;
		if (r.HasAddend)
			addend = r.Addend < 0 ? L"-" + Hex((uint64_t)-r.Addend) : Hex((uint64_t)r.Addend);
		Row row{ { Widen(sec.Name), Hex(r.Offset), r.Relr ? L"RELATIVE (packed)" : ElfFile::RelocationTypeName(m_Elf.Machine(), r.Type),
			Widen(r.SymbolName), Demangled(r.SymbolName), r.Relr ? L"" : std::to_wstring(r.SymbolIndex), addend }, Target::Address, r.Offset };
		// in a relocatable object the offset is in the section that the relocations are for (sh_info)
		if (IsRelocatable()) {
			row.Go = sec.Info < sections.size() ? Target::SectionOffset : Target::None;
			row.Section = (int)sec.Info;
		}
		m_Rows.push_back(std::move(row));
	}
}

void CElfView::BuildNotes() {
	m_Columns = { { L"Where", 180 }, { L"Owner", 80 }, { L"Type", 200 }, { L"Size", 60, true }, { L"Description", 500 } };
	for (auto const& n : m_Elf.Notes())
		m_Rows.push_back({ { Widen(n.Section), Widen(n.Owner), ElfFile::NoteTypeName(n.Owner, n.Type), std::to_wstring(n.Description.size()),
			m_Elf.NoteDescription(n) } });
}

// where the debug information is, and what it has
void CElfView::BuildDebugInfo() {
	m_Columns = { { L"Name", 160 }, { L"Value", 350 }, { L"Details", 500 } };
	if (!m_Debug)
		return;
	auto add = [&](std::wstring name, std::wstring value, std::wstring details = L"") {
		m_Rows.push_back({ { std::move(name), std::move(value), std::move(details) } });
	};
	auto const& d = *m_Debug;
	if (d.Link.Present)
		add(L"Debug Link", Widen(d.Link.File), std::format(L"CRC 0x{:08X} (.gnu_debuglink)", d.Link.Crc));
	if (!d.DebugFile.empty())
		add(L"Debug File", d.DebugFile, L"The DWARF below is from this file");
	for (auto const& s : d.Searched)
		add(L"Not Found", s, L"Where the debug file was looked for");
	if (d.Dwarf.Empty())
		add(L"DWARF", L"None", d.Link.Present || !m_Elf.BuildId().empty() ? L"The debug information is in a separate file" : L"The file has no debug information");
	else
		add(L"DWARF", std::format(L"{} compile units, {} functions", d.Dwarf.Units.size(), d.Dwarf.Functions.size()),
			std::format(L"Version {}", d.Dwarf.Units.front().Version));
	for (auto const& s : d.Dwarf.Sections)
		add(Widen(s.Name), PEStrings::ToMemorySize(s.UncompressedSize),
			s.Compression.empty() ? L"" : std::format(L"Compressed with {} to {}", s.Compression, PEStrings::ToMemorySize(s.Size)));
	for (auto const& p : d.Dwarf.Problems)
		add(L"Problem", p);
}

void CElfView::BuildCompileUnits() {
	m_Columns = { { L"Offset", 80, true }, { L"Name", 260 }, { L"Language", 90 }, { L"Version", 60, true }, { L"Type", 80 },
		{ L"Low PC", 110, true }, { L"High PC", 110, true }, { L"Functions", 70, true }, { L"Directory", 300 }, { L"Producer", 500 } };
	if (!m_Debug)
		return;
	for (auto const& u : m_Debug->Dwarf.Units) {
		Row row{ { Hex(u.Offset), Widen(u.Name), DwarfInfo::LanguageName(u.Language), std::to_wstring(u.Version), DwarfInfo::UnitTypeName(u.UnitType),
			u.HasRange ? Hex(u.LowPc) : L"", u.HasRange ? Hex(u.HighPc) : L"", std::to_wstring(u.Functions), Widen(u.CompDir), Widen(u.Producer) } };
		// the code of the unit (the addresses of a relocatable object are not relocated)
		if (u.HasRange && u.LowPc && !IsRelocatable()) {
			row.Go = Target::Address;
			row.Where = u.LowPc;
			row.Code = true;
		}
		m_Rows.push_back(std::move(row));
	}
}

void CElfView::BuildFunctions() {
	m_Columns = { { L"Name", 400 }, { L"Address", 110, true }, { L"Size", 70, true }, { L"Line", 60, true }, { L"External", 60 },
		{ L"Compile Unit", 200 }, { L"Linkage Name", 300 } };
	if (!m_Debug)
		return;
	auto const& units = m_Debug->Dwarf.Units;
	for (auto const& f : m_Debug->Dwarf.Functions) {
		// the full name (with the class and the parameters) is in the linkage name
		auto name = Demangled(f.LinkageName);
		if (name.empty())
			name = Widen(f.Name.empty() ? f.LinkageName : f.Name);
		Row row{ { name, Hex(f.LowPc), Hex(f.HighPc - f.LowPc), f.Line ? std::to_wstring(f.Line) : L"", f.External ? L"Yes" : L"",
			f.Unit < units.size() ? Widen(units[f.Unit].Name) : L"", Widen(f.LinkageName) } };
		if (!IsRelocatable()) {
			row.Go = Target::Address;
			row.Where = f.LowPc;
			row.Code = true;
		}
		m_Rows.push_back(std::move(row));
	}
}

void CElfView::Build() {
	switch (m_Kind) {
		case ElfViewKind::Header: BuildHeader(); break;
		case ElfViewKind::ProgramHeaders: BuildProgramHeaders(); break;
		case ElfViewKind::Sections: BuildSections(); break;
		case ElfViewKind::Symbols: BuildSymbols(); break;
		case ElfViewKind::Dynamic: BuildDynamic(); break;
		case ElfViewKind::Relocations: BuildRelocations(); break;
		case ElfViewKind::Notes: BuildNotes(); break;
		case ElfViewKind::DebugInfo: BuildDebugInfo(); break;
		case ElfViewKind::CompileUnits: BuildCompileUnits(); break;
		case ElfViewKind::Functions: BuildFunctions(); break;
	}
}

CString CElfView::GetColumnText(HWND, int row, int col) const {
	if (row < 0 || row >= (int)m_Rows.size() || col < 0 || col >= (int)m_Rows[row].Cells.size())
		return CString();
	return m_Rows[row].Cells[col].c_str();
}

int CElfView::GetRowImage(HWND, int row, int) const {
	if (m_Kind == ElfViewKind::Sections)
		return Frame()->GetIconIndex(IDI_SECTION);
	if (m_Kind == ElfViewKind::Symbols && row >= 0 && row < (int)m_Rows.size())
		return Frame()->GetIconIndex(m_Rows[row].Cells[6] == L"FUNC" ? IDI_FUNCTION : IDI_DATA);
	if (m_Kind == ElfViewKind::Functions)
		return Frame()->GetIconIndex(IDI_FUNCTION);
	if (m_Kind == ElfViewKind::CompileUnits)
		return Frame()->GetIconIndex(IDI_TEXT);
	return -1;
}

bool CElfView::IsSortable(HWND, int) const {
	return m_Kind != ElfViewKind::Header;
}

void CElfView::DoSort(SortInfo const* si) {
	if (si == nullptr || si->SortColumn < 0 || si->SortColumn >= (int)m_Columns.size())
		return;
	auto col = si->SortColumn;
	auto numeric = m_Columns[col].Right;
	auto asc = si->SortAscending;
	std::ranges::stable_sort(m_Rows, [&](Row const& a, Row const& b) {
		if (numeric)
			return SortHelper::Sort(NumberOf(a.Cells[col]), NumberOf(b.Cells[col]), asc);
		return SortHelper::Sort(a.Cells[col], b.Cells[col], asc);
	});
}

bool CElfView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	auto const& r = m_Rows[row];
	switch (r.Go) {
		case Target::Address: return Frame()->ShowElfAddress(r.Where, r.Code);
		case Target::SectionOffset: return Frame()->ShowElfSection(r.Section, (int64_t)r.Where, r.Code);
		case Target::FileOffset: return Frame()->ShowElfFileOffset((int64_t)r.Where);
	}
	return false;
}

void CElfView::OnStateChanged(HWND, int, int, DWORD oldState, DWORD newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CElfView::UpdateUI(bool first) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	if (first && m_Kind != ElfViewKind::Header)
		Frame()->SetStatusText(1, std::format(L"Items: {}", m_Rows.size()).c_str());
}

LRESULT CElfView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	if (m_Kind == ElfViewKind::Sections || m_Kind == ElfViewKind::Symbols || m_Kind == ElfViewKind::Functions || m_Kind == ElfViewKind::CompileUnits)
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	CWaitCursor wait;
	Build();
	auto cm = GetColumnManager(m_List);
	for (auto const& c : m_Columns)
		cm->AddColumn(c.Name, c.Right ? LVCFMT_RIGHT : LVCFMT_LEFT, c.Width);
	m_List.SetItemCount((int)m_Rows.size());
	return 0;
}

LRESULT CElfView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
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

LRESULT CElfView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List));
	return 0;
}
