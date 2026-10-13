#include "pch.h"
#include "FunctionsView.h"
#include <PEFile.h>
#include <CodeAnalysis.h>
#include <DiaHelper.h>
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>
#include <map>
#include <thread>

CFunctionsView::CFunctionsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CFunctionsView::GetTitle() const {
	return L"Functions";
}

std::wstring CFunctionsView::SourceText(uint8_t sources) {
	static PCWSTR const names[] = { L"Symbols", L"Unwind Info", L"Export", L"Entry Point", L"Calls" };
	std::wstring text;
	for (int i = 0; i < _countof(names); i++)
		if (sources & (1 << i))
			text += (text.empty() ? L"" : L", ") + std::wstring(names[i]);
	return text;
}

CString CFunctionsView::GetColumnText(HWND, int row, int col) const {
	auto const& f = m_Items[row];
	switch ((Column)col) {
		case Column::Name: return f.Name.c_str();
		case Column::Address: return std::format(L"0x{:X}", m_PE.GetImageBase() + f.Rva).c_str();
		case Column::Rva: return std::format(L"0x{:X}", f.Rva).c_str();
		case Column::Size: return f.Size ? std::format(L"{}{}", f.KnownSize ? L"" : L"~", f.Size).c_str() : L"";
		case Column::Calls: return Frame()->XrefsReady() ? std::to_wstring(f.Calls).c_str() : L"...";
		case Column::Section: return f.Section >= 0 ? m_SectionNames[f.Section].c_str() : L"";
		case Column::Source: return SourceText(f.Sources).c_str();
	}
	return CString();
}

void CFunctionsView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;
	auto asc = si->SortAscending;
	m_Items.Sort([&](Function const& a, Function const& b) {
		switch ((Column)si->SortColumn) {
			case Column::Name: return SortHelper::Sort(a.Name, b.Name, asc);
			case Column::Address: case Column::Rva: return SortHelper::Sort(a.Rva, b.Rva, asc);
			case Column::Size: return SortHelper::Sort(a.Size, b.Size, asc);
			case Column::Calls: return SortHelper::Sort(a.Calls, b.Calls, asc);
			case Column::Section: return SortHelper::Sort(a.Section, b.Section, asc);
			case Column::Source: return SortHelper::Sort(a.Sources, b.Sources, asc);
		}
		return false;
	});
}

int CFunctionsView::GetRowImage(HWND, int row, int) const {
	auto sources = m_Items[row].Sources;
	return Frame()->GetIconIndex(sources & FromExports ? IDI_FUNCTION : sources & FromSymbols ? IDI_FUNCTION2 : IDI_FUNCTION);
}

// What the file and its symbols tell (once: the symbols of a large program take seconds)
void CFunctionsView::Collect() {
	auto sections = m_PE.GetSecHeaders();
	if (!sections)
		return;

	// the code sections: functions are there only
	auto& code = m_Code;
	m_SectionNames.clear();
	int index = 0;
	for (auto const& sec : *sections) {
		auto const& h = sec.SecHdr;
		CString name = sec.SectionName.c_str();
		if (name.IsEmpty())
			name = CString((PCSTR)h.Name, 8);
		m_SectionNames.push_back((PCWSTR)name);
		if (h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))
			code.push_back({ h.VirtualAddress, h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData), index });
		index++;
	}
	auto& functions = m_Known;
	auto add = [&](uint32_t rva, uint8_t source, std::wstring const& name = {}, uint32_t size = 0) {
		Add(functions, rva, source, name, size);
	};

	// the functions with unwind information (x64 and ARM64): a part of a function that has its own (an entry that is
	// "chained" to the function's) is not a function
	if (auto exceptions = m_PE.GetExceptions()) {
		bool x64 = m_PE.GetFileInfo()->IsPE64 && m_PE.GetNTHeader()->NTHdr64.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
		for (auto const& e : *exceptions) {
			auto const& entry = e.RuntimeFuncEntry;
			uint32_t size = entry.EndAddress > entry.BeginAddress ? entry.EndAddress - entry.BeginAddress : 0;
			if (x64 && entry.UnwindInfoAddress) {
				auto offset = m_PE.GetOffsetFromRVA(entry.UnwindInfoAddress);
				if (offset && offset < m_PE.GetFileSize() && ((uint8_t)m_PE.GetData()[offset] >> 3) & UNW_FLAG_CHAININFO)
					continue;
			}
			add(entry.BeginAddress, FromExceptions, {}, size);
		}
	}

	if (auto exp = m_PE.GetExport())
		for (auto const& f : exp->Funcs)
			if (f.FuncRVA && f.ForwarderName.empty())
				add(f.FuncRVA, FromExports, f.FuncName.empty() ? std::wstring() : std::wstring(f.FuncName.begin(), f.FuncName.end()));

	auto entry = m_PE.GetFileInfo()->IsPE64 ? m_PE.GetNTHeader()->NTHdr64.OptionalHeader.AddressOfEntryPoint : m_PE.GetNTHeader()->NTHdr32.OptionalHeader.AddressOfEntryPoint;
	if (entry)
		add(entry, FromEntryPoint);

	// The symbols' functions come later: they are read on a thread of its own (those of a large program take seconds),
	// from a session of its own of the same symbols file (the frame's is the frame thread's)
	auto const& symbols = Frame()->GetSymbols();
	if (!symbols || symbols.GetSymbolFile().empty())
		return;
	m_ReadingSymbols = true;
	m_Cancel = std::make_shared<std::atomic<bool>>(false);
	std::thread([hWnd = m_hWnd, cancel = m_Cancel, path = symbols.GetSymbolFile()] {
		::SetThreadDescription(::GetCurrentThread(), L"Functions of the symbols");
		::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		auto functions = std::make_unique<std::vector<SymbolFunction>>();
		{
			DiaSession session;
			if (session.OpenPdb(path.c_str())) {
				// a function of private symbols, or a public symbol in code
				for (auto const& sym : session.FindChildren(nullptr, SymbolTag::Function)) {
					if (*cancel)
						break;
					if (auto rva = sym.RelativeVirtualAddress())
						functions->push_back({ rva, (uint32_t)sym.Length(), sym.Name() });
				}
				for (auto const& sym : session.FindChildren(nullptr, SymbolTag::PublicSymbol)) {
					if (*cancel)
						break;
					if (auto rva = sym.RelativeVirtualAddress(); rva && sym.IsFunction()) {
						auto name = sym.UndecoratedName();
						functions->push_back({ rva, 0, name.empty() ? sym.Name() : name, true });
					}
				}
			}
		}
		if (!*cancel && ::PostMessage(hWnd, WM_SYMBOL_FUNCTIONS, 0, (LPARAM)functions.get()))
			functions.release();
		::CoUninitialize();
		}).detach();
}

LRESULT CFunctionsView::OnSymbolFunctions(UINT, WPARAM, LPARAM lParam, BOOL&) {
	std::unique_ptr<std::vector<SymbolFunction>> functions(reinterpret_cast<std::vector<SymbolFunction>*>(lParam));
	m_ReadingSymbols = false;
	// the names and sizes of the symbols are those of the functions (a public symbol's only if there is no other)
	for (auto const& f : *functions)
		Add(m_Known, f.Rva, FromSymbols, f.Name, f.Size, !f.Public);
	CWaitCursor wait;
	Build();
	return 0;
}

LRESULT CFunctionsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	if (m_Cancel)
		*m_Cancel = true;	// the thread reading the symbols ends soon, and what it read goes
	handled = FALSE;
	return 0;
}

CFunctionsView::Code const* CFunctionsView::SectionOf(uint32_t rva) const {
	for (auto const& c : m_Code)
		if (rva >= c.Start && rva < c.End)
			return &c;
	return nullptr;
}

void CFunctionsView::Add(std::map<uint32_t, Function>& functions, uint32_t rva, uint8_t source, std::wstring const& name, uint32_t size,
	bool prefer) const {
	if (!SectionOf(rva))
		return;
	auto& f = functions.try_emplace(rva, Function{ rva }).first->second;
	f.Sources |= source;
	if ((f.Name.empty() || prefer) && !name.empty())
		f.Name = name;
	if (size && (!f.KnownSize || prefer)) {
		f.Size = size;
		f.KnownSize = true;
	}
}

// What is known, and the targets of calls once the cross-references are there
void CFunctionsView::Build() {
	auto base = m_PE.GetImageBase();
	auto functions = m_Known;
	auto const& xrefs = Frame()->GetXrefs();
	if (Frame()->XrefsReady()) {
		for (auto va : xrefs.Targets())
			if (va >= base && va - base <= 0xFFFFFFFFULL && xrefs.CallCount(va))
				Add(functions, (uint32_t)(va - base), FromCalls);
		for (auto& [rva, f] : functions)
			f.Calls = (uint32_t)xrefs.CallCount(base + rva);
	}

	// A stub that only jumps to a function (of incremental linking: the calls of the function are to it) is named after
	// the function, and its calls are the function's too
	auto machine = m_PE.GetNTHeader()->NTHdr32.FileHeader.Machine;
	if (machine == IMAGE_FILE_MACHINE_AMD64 || machine == IMAGE_FILE_MACHINE_I386) {
		for (auto& [rva, f] : functions) {
			auto offset = m_PE.GetOffsetFromRVA(rva);
			if (!offset || offset + 5 > m_PE.GetFileSize() || (uint8_t)m_PE.GetData()[offset] != 0xE9)
				continue;
			int32_t displacement;
			memcpy(&displacement, m_PE.GetData() + offset + 1, sizeof(displacement));
			auto target = functions.find(rva + 5 + displacement);
			if (target == functions.end() || target->first == rva)
				continue;
			f.Target = target->first;
			target->second.Calls += f.Calls;
		}
		for (auto& [rva, f] : functions)
			if (f.Target && f.Name.empty())
				f.Name = L"j_" + (functions[f.Target].Name.empty() ? std::format(L"sub_{:X}", base + f.Target) : functions[f.Target].Name);
	}

	// names for those without one, and sizes up to the next function (in the same section) for those with none
	std::vector<Function> items;
	items.reserve(functions.size());
	for (auto it = functions.begin(); it != functions.end(); ++it) {
		auto& f = it->second;
		auto section = SectionOf(f.Rva);
		f.Section = section->Section;
		if (f.Name.empty())
			f.Name = std::format(L"sub_{:X}", base + f.Rva);
		if (!f.KnownSize) {
			auto next = std::next(it);
			uint32_t end = next != functions.end() && next->first < section->End ? next->first : section->End;
			f.Size = end - f.Rva;
		}
		items.push_back(std::move(f));
	}
	m_Items.Set(std::move(items));
	ApplyFilter();
}

void CFunctionsView::ApplyFilter() {
	if (m_FilterText.IsEmpty())
		m_Items.Filter(nullptr);
	else {
		CString filter(m_FilterText);
		filter.MakeLower();
		// a name, or an address (or a part of one; "0x" or not)
		CString address(filter);
		if (address.Left(2) == L"0x")
			address = address.Mid(2);
		auto base = m_PE.GetImageBase();
		m_Items.Filter([&](Function const& f, size_t) {
			CString name(f.Name.c_str());
			name.MakeLower();
			return name.Find(filter) >= 0 || (!address.IsEmpty() && CString(std::format(L"{:x}", base + f.Rva).c_str()).Find(address) >= 0);
		});
	}
	m_List.SetItemCountEx((int)m_Items.size(), LVSICF_NOSCROLL);
	Sort(m_List, true);
	UpdateUI(true);
}

void CFunctionsView::XrefsChanged() {
	Build();
}

CFunctionsView::Function const* CFunctionsView::GetSelected() const {
	if (m_List.GetSelectedCount() != 1)
		return nullptr;
	return &m_Items[m_List.GetNextItem(-1, LVNI_SELECTED)];
}

bool CFunctionsView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0)
		return false;
	return Frame()->DisassembleFunction(m_PE.GetImageBase() + m_Items[row].Rva);
}

bool CFunctionsView::OnRightClickList(HWND, int row, int, POINT const& pt) const {
	if (row < 0)
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(3), 0, pt.x, pt.y);
}

void CFunctionsView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CFunctionsView::UpdateUI(bool first) const {
	auto& ui = Frame()->GetUI();
	int selected = m_List.GetSelectedCount();
	ui.UIEnable(ID_EDIT_COPY, selected > 0);
	for (auto id : { ID_VIEW_DISASSEMBLE, ID_EXPORT_DECOMPILE, ID_EXPORT_SIDEBYSIDE, ID_EXPORT_XREFS, ID_EXPORT_FLOWGRAPH, ID_EXPORT_CALLGRAPH })
		ui.UIEnable(id, selected == 1);
	if (first) {
		auto text = m_Items.size() == m_Items.TotalSize() ? std::format(L"Functions: {}", m_Items.size()) :
			std::format(L"Functions: {} of {}", m_Items.size(), m_Items.TotalSize());
		if (m_ReadingSymbols)
			text += L" (reading the symbols...)";
		Frame()->SetStatusText(1, text.c_str());
	}
}

LRESULT CFunctionsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	// the filter, in a band of its own (the rebar passes its notifications on to this window)
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	CRect rc(0, 0, 300, 20);
	m_Filter.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
	m_Filter.SetFont(AtlGetDefaultGuiFont());
	m_Filter.SetWatermark(L"Filter by name or address (Esc to clear)");
	AddSimpleReBarBand(m_Filter, nullptr, FALSE, 300);

	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 350);
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 120);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 80);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 70);
	cm->AddColumn(L"Calls", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Section", LVCFMT_LEFT, 70);
	cm->AddColumn(L"Known From", LVCFMT_LEFT, 200);

	{
		CWaitCursor wait;
		Collect();
		Build();
	}
	return 0;
}

LRESULT CFunctionsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr || m_List.GetItemCount() == 0)
		return 0;
	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());
	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

LRESULT CFunctionsView::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Filter) {
		handled = FALSE;
		return 0;
	}
	m_Filter.GetWindowText(m_FilterText);
	ApplyFilter();
	return 0;
}

LRESULT CFunctionsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L","));
	return 0;
}

LRESULT CFunctionsView::OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const {
	auto f = GetSelected();
	if (!f)
		return 0;
	auto va = m_PE.GetImageBase() + f->Rva;
	switch (id) {
		case ID_VIEW_DISASSEMBLE: Frame()->DisassembleFunction(va); break;
		case ID_EXPORT_DECOMPILE: Frame()->DecompileFunction(va); break;
		case ID_EXPORT_SIDEBYSIDE: Frame()->ShowSideBySide(va); break;
		case ID_EXPORT_XREFS: Frame()->ShowXrefs(va); break;
		case ID_EXPORT_FLOWGRAPH: Frame()->ShowFlowGraph(va); break;
		case ID_EXPORT_CALLGRAPH: Frame()->ShowCallGraph(va); break;
	}
	return 0;
}
