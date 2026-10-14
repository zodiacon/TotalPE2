// MainFrmSearch.cpp: Search All - the items of the file that one search goes through, and how each is shown
#include "pch.h"
#include "resource.h"
#include "MainFrm.h"
#include "FileStrings.h"
#include "PEStrings.h"
#include "ImportAnalysis.h"
#include "ItaniumDemangle.h"
#include <unordered_set>
#include <DbgHelp.h>
#include <ClrMetadata.h>
#include <HexNumber.h>
#include "GoAnywhereDlg.h"
#include "ClrTypesView.h"

namespace {
	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	// the value of an object symbol or section that has no place to go to
	constexpr uint64_t NoOffset = ~0ull;

	// adds items with their icons: the icon of a kind, unless the item has its own (a resource has the icon of its type)
	struct IndexWithIcons {
		SearchIndex& Index;
		int (*Image)(SearchItem const&);
		void Add(SearchItem item) {
			if (item.Image < 0)
				item.Image = Image(item);
			Index.Add(std::move(item));
		}
	};
}

// the icons the kinds of items have in the tree (and in the imports view)
int CMainFrame::SearchItemImage(SearchItem const& item) {
	switch (item.Kind) {
		case SearchKind::ImportModule: return GetTreeIcon(item.Name.starts_with(L"api-ms-win") ? IDI_INTERFACE : IDI_DLL_IMPORT);
		case SearchKind::Import: return GetTreeIcon(IDI_IMPORTS);
		case SearchKind::Export: return GetTreeIcon(IDI_EXPORTS);
		case SearchKind::String: return GetTreeIcon(IDI_TEXT);
		case SearchKind::Resource: return GetTreeIcon(IDI_RESOURCE);
		case SearchKind::Symbol: return GetTreeIcon(IDI_SYMBOLS);
		case SearchKind::Section: return GetTreeIcon(IDI_SECTION);
		case SearchKind::ClrType: return GetTreeIcon(IDI_CLASS);
		case SearchKind::ClrMember: return GetTreeIcon(IDI_FUNCTION);
		case SearchKind::Address: return GetTreeIcon(IDI_BINARY);
	}
	return -1;
}

LRESULT CMainFrame::OnNavGoAnywhere(WORD, WORD, HWND, BOOL&) {
	if (!m_PE && !m_Elf && !m_Object && !m_Archive)
		return 0;
	CGoAnywhereDlg dlg(this);
	if (dlg.DoModal(m_hWnd) == IDOK && !GoToSearchItem(dlg.Selected()))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

// A hexadecimal number as the places it can be: an address in the image, an RVA, an offset in the file
std::vector<SearchItem> CMainFrame::AddressItems(std::wstring const& text) {
	std::vector<SearchItem> items;
	uint64_t value;
	if (!m_PE || !ParseHexNumber(text, value))
		return items;
	auto base = m_PE.GetImageBase();
	auto nt = m_PE.GetNTHeader();
	uint64_t size = m_PE.GetFileInfo()->IsPE64 ? nt->NTHdr64.OptionalHeader.SizeOfImage : nt->NTHdr32.OptionalHeader.SizeOfImage;
	auto image = SearchItemImage({ .Kind = SearchKind::Address });
	if (value >= base && value - base < size)
		items.push_back({ .Kind = SearchKind::Address, .Name = std::format(L"VA 0x{:X}", value), .Details = ResolveVa(value),
			.Location = std::format(L"RVA 0x{:X}", value - base), .Value = value, .Index = 0, .Image = image });
	if (value < size)
		items.push_back({ .Kind = SearchKind::Address, .Name = std::format(L"RVA 0x{:X}", value), .Details = ResolveRva((DWORD)value),
			.Location = std::format(L"VA 0x{:X}", base + value), .Value = value, .Index = 1, .Image = image });
	if (value < m_PE.GetFileSize())
		items.push_back({ .Kind = SearchKind::Address, .Name = std::format(L"File offset 0x{:X}", value), .Value = value, .Index = 2,
			.Image = image });
	return items;
}

// the nodes of the tree that show something (not the sections and resources, which have their own items)
void CMainFrame::AddViewsToSearchIndex(HTREEITEM hParent, std::wstring const& parent, int depth) {
	for (auto hItem = hParent ? m_Tree.GetChildItem(hParent) : m_Tree.GetRootItem(); hItem; hItem = m_Tree.GetNextSiblingItem(hItem)) {
		CString text;
		m_Tree.GetItemText(hItem, text);
		auto type = (TreeItemType)m_Tree.GetItemData(hItem);
		auto kind = type & TreeItemType::ItemMask;
		int image = -1, selected;
		m_Tree.GetItemImage(hItem, image, selected);
		if (kind != TreeItemType::Section && kind != TreeItemType::Resource && kind != TreeItemType::ResourceName &&
			kind != TreeItemType::ResourceLnaguage && hParent)	// the root is the file
			m_SearchIndex.Add({ .Kind = SearchKind::View, .Name = (PCWSTR)text, .Details = parent, .Value = (uint64_t)type, .Image = image });
		if (depth < 3)
			AddViewsToSearchIndex(hItem, hParent ? std::wstring(text) : std::wstring(), depth + 1);
	}
}

LRESULT CMainFrame::OnEditSearchAll(WORD, WORD, HWND, BOOL&) {
	m_SearchDlg.Show(m_hWnd);
	return 0;
}

void CMainFrame::SearchTargetChanged() {
	m_SearchIndex.Clear();
	m_SearchIndexValid = false;
	m_SearchDlg.Invalidate();
}

SearchIndex const& CMainFrame::GetSearchIndex() {
	if (!m_SearchIndexValid) {
		CWaitCursor wait;
		m_SearchIndex.Clear();
		BuildSearchIndex();
		m_SearchIndexValid = true;
	}
	return m_SearchIndex;
}

void CMainFrame::AddStringsToSearchIndex(std::span<const std::byte> data, std::function<bool(uint32_t offset)> const& keep) {
	for (auto& s : FindStrings(data)) {
		if (keep && !keep(s.Offset))
			continue;
		auto details = std::format(L"{}, {} characters", StringEncodingToString(s.Encoding), s.Length);
		auto location = std::format(L"Offset 0x{:X}", s.Offset);
		IndexWithIcons{ m_SearchIndex, SearchItemImage }.Add({ .Kind = SearchKind::String, .Name = std::move(s.Text), .Details = std::move(details), .Location = std::move(location),
			.Value = s.Offset });
	}
}

void CMainFrame::BuildSearchIndex() {
	IndexWithIcons index{ m_SearchIndex, SearchItemImage };
	if (m_PE) {
		auto is64 = m_PE.GetFileInfo()->IsPE64;
		auto base = m_PE.GetImageBase();

		if (auto imports = m_PE.GetImport()) {
			int m = 0;
			for (auto const& mod : *imports) {
				auto module = Widen(mod.ModuleName);
				index.Add({ .Kind = SearchKind::ImportModule, .Name = module, .Details = std::format(L"{} functions", mod.ImportFunc.size()), .Index = m });
				for (auto const& fn : mod.ImportFunc) {
					SearchItem item{ .Kind = SearchKind::Import, .Details = module, .Index = m, .Owner = module };
					if (IsOrdinalImport(fn, is64)) {
						// the name the ordinal has in the DLL on this system, if it can be found
						auto ordinal = ImportOrdinal(fn);
						auto name = ResolveOrdinalName(mod.ModuleName, ordinal, !is64);
						item.Name = name.empty() ? std::format(L"#{}", ordinal) : Widen(name);
						item.Details += std::format(L", ordinal {}", ordinal);
						item.Value = ordinal;
					}
					else
						item.Name = Widen(fn.FuncName);
					index.Add(std::move(item));
				}
				m++;
			}
		}

		if (auto exports = m_PE.GetExport()) {
			for (auto const& exp : exports->Funcs) {
				SearchItem item{ .Kind = SearchKind::Export, .Location = std::format(L"RVA 0x{:X}", exp.FuncRVA), .Value = exp.Ordinal };
				item.Name = Widen(exp.FuncName);
				if (item.Name.empty() && m_Symbols)
					if (auto sym = m_Symbols.GetSymbolByRVA(exp.FuncRVA))
						item.Name = sym.Name();
				if (item.Name.empty())
					item.Name = std::format(L"#{}", exp.Ordinal);
				item.Details = exp.ForwarderName.empty() ? std::format(L"Ordinal {}", exp.Ordinal) :
					std::format(L"Ordinal {}, forwarded to {}", exp.Ordinal, Widen(exp.ForwarderName));
				index.Add(std::move(item));
			}
		}

		int i = 0;
		for (auto const& res : m_FlatResources) {
			index.Add({ .Kind = SearchKind::Resource, .Name = res.Type + L"\\" + res.Name, .Details = res.Language,
				.Location = std::format(L"{} bytes", res.Data.size()), .Value = res.Data.size(), .Index = i++, .Image = ResourceTypeToIcon(res.TypeID) });
		}

		// the user's: names of functions and global variables, bookmarks, comments
		for (auto const& [rva, name] : m_Annotations.Functions)
			index.Add({ .Kind = SearchKind::Symbol, .Name = Widen(name), .Details = L"Function (your name)",
				.Location = std::format(L"RVA 0x{:X}", rva), .Value = base + rva });
		for (auto const& [rva, v] : m_Annotations.Globals)
			index.Add({ .Kind = SearchKind::Symbol, .Name = Widen(v.Name), .Details = L"Data (your name): " + Widen(v.Type),
				.Location = std::format(L"RVA 0x{:X}", rva), .Value = base + rva });
		for (auto const& b : m_Annotations.Bookmarks)
			index.Add({ .Kind = SearchKind::Symbol, .Name = b.Name, .Details = L"Bookmark",
				.Location = std::format(L"RVA 0x{:X}", b.Rva), .Value = base + b.Rva, .Image = GetTreeIcon(IDI_BOOKMARK) });
		for (auto const& [rva, text] : m_Annotations.Comments)
			index.Add({ .Kind = SearchKind::Symbol, .Name = text, .Details = L"Comment",
				.Location = std::format(L"RVA 0x{:X}", rva), .Value = base + rva });

		// the functions and the data of the symbols, then the public symbols that are elsewhere
		if (m_Symbols) {
			std::unordered_set<uint64_t> seen;
			auto add = [&](SymbolTag tag, PCWSTR kind) {
				for (auto const& sym : m_Symbols.FindChildren(nullptr, tag)) {
					if (sym.Location() != LocationKind::Static)	// a constant, say: no address
						continue;
					auto va = sym.VirtualAddress();
					if (va < base || !seen.insert(va).second)
						continue;
					auto name = sym.Name();
					std::wstring details = kind;
					// a public symbol has the decorated name: the name is searched, the declaration is in the details
					if (tag == SymbolTag::PublicSymbol && name.starts_with(L'?')) {
						WCHAR plain[1024];
						if (::UnDecorateSymbolNameW(name.c_str(), plain, _countof(plain), UNDNAME_NAME_ONLY)) {
							details = std::format(L"{}: {}", kind, sym.UndecoratedName());
							name = plain;
						}
					}
					if (name.empty())
						continue;
					index.Add({ .Kind = SearchKind::Symbol, .Name = std::move(name), .Details = std::move(details),
						.Location = std::format(L"RVA 0x{:X}", va - base), .Value = va });
				}
			};
			add(SymbolTag::Function, L"Function");
			add(SymbolTag::Data, L"Data");
			add(SymbolTag::PublicSymbol, L"Public symbol");
		}

		i = 0;
		for (auto const& sec : *m_PE.GetSecHeaders()) {
			index.Add({ .Kind = SearchKind::Section, .Name = Widen(sec.SectionName),
				.Details = PEStrings::SectionCharacteristicsToString(sec.SecHdr.Characteristics),
				.Location = std::format(L"RVA 0x{:X}", sec.SecHdr.VirtualAddress), .Value = sec.SecHdr.VirtualAddress, .Index = i++ });
		}

		// the types and members of a .NET file
		if (m_PE.GetFileInfo()->HasCOMDescr) {
			ClrMetadata md;
			if (md.Parse(m_PE)) {
				auto& types = md.Types();
				for (uint32_t t = 1; t <= types.size(); t++)
					index.Add({ .Kind = SearchKind::ClrType, .Name = md.FullName(t), .Details = md.TypeKind(t),
						.Value = (ClrTableId::TypeDef << 24) | t });
				auto field = GetTreeIcon(IDI_FIELD);
				for (uint32_t m = 1; m <= md.Methods().size(); m++) {
					auto& method = md.Methods()[m - 1];
					index.Add({ .Kind = SearchKind::ClrMember, .Name = method.Name, .Details = method.Type ? md.FullName(method.Type) : std::wstring(),
						.Value = (ClrTableId::MethodDef << 24) | m });
				}
				for (uint32_t f = 1; f <= md.Fields().size(); f++) {
					auto& fd = md.Fields()[f - 1];
					index.Add({ .Kind = SearchKind::ClrMember, .Name = fd.Name, .Details = fd.Type ? md.FullName(fd.Type) : std::wstring(),
						.Value = (ClrTableId::Field << 24) | f, .Image = field });
				}
				auto property = GetTreeIcon(IDI_PROPERTY), event = GetTreeIcon(IDI_EVENT);
				for (uint32_t p = 1; p <= md.Properties().size(); p++) {
					auto& pd = md.Properties()[p - 1];
					index.Add({ .Kind = SearchKind::ClrMember, .Name = pd.Name, .Details = pd.Type ? md.FullName(pd.Type) : std::wstring(),
						.Value = (ClrTableId::Property << 24) | p, .Image = property });
				}
				for (uint32_t e = 1; e <= md.Events().size(); e++) {
					auto& ed = md.Events()[e - 1];
					index.Add({ .Kind = SearchKind::ClrMember, .Name = ed.Name, .Details = ed.Type ? md.FullName(ed.Type) : std::wstring(),
						.Value = (ClrTableId::Event << 24) | e, .Image = event });
				}
			}
		}

		AddStringsToSearchIndex(std::span((const std::byte*)m_PE.GetData(), m_PE.GetFileSize()));
	}
	else if (m_Object) {
		int i = 0;
		for (auto const& sec : m_Object.Sections()) {
			index.Add({ .Kind = SearchKind::Section, .Name = Widen(sec.Name), .Details = PEStrings::SectionCharacteristicsToString(sec.Characteristics),
				.Location = std::format(L"Section {}", i + 1), .Value = NoOffset, .Index = i });
			i++;
		}
		for (auto const& sym : m_Object.Symbols()) {
			if (sym.StorageClass == IMAGE_SYM_CLASS_FILE || sym.Name.empty())
				continue;
			bool defined = sym.SectionNumber > 0 && sym.SectionNumber <= (int32_t)m_Object.Sections().size();
			auto details = CoffObject::StorageClassName(sym.StorageClass);
			if (sym.Name[0] == '?')
				details = PEStrings::UndecorateName(Widen(sym.Name).c_str()) + L" (" + details + L")";
			index.Add({ .Kind = SearchKind::Symbol, .Name = Widen(sym.Name), .Details = std::move(details),
				.Location = defined ? std::format(L"{} + 0x{:X}", Widen(m_Object.Sections()[sym.SectionNumber - 1].Name), sym.Value) :
					CoffObject::SectionNumberName(sym.SectionNumber),
				.Value = sym.Value, .Index = defined ? sym.SectionNumber - 1 : -1 });
		}
		// only the strings in the data of sections can be shown (not those of the symbol table, say)
		AddStringsToSearchIndex(m_Object.Data(), [&](uint32_t offset) {
			return std::ranges::any_of(m_Object.Sections(), [&](auto const& sec) {
				return sec.PointerToRawData && offset >= sec.PointerToRawData && offset - sec.PointerToRawData < sec.SizeOfRawData;
			});
		});
	}
	else if (m_Elf) {
		bool relocatable = m_Elf.Type() == 1;
		int i = 0;
		for (auto const& sec : m_Elf.Sections()) {
			if (!sec.Name.empty())
				index.Add({ .Kind = SearchKind::Section, .Name = Widen(sec.Name), .Details = ElfFile::SectionTypeName(sec.Type),
					.Location = sec.Address ? std::format(L"Address 0x{:X}", sec.Address) : std::format(L"Offset 0x{:X}", sec.Offset),
					.Value = sec.Address, .Index = i });
			i++;
		}
		for (auto const& sym : m_Elf.Symbols()) {
			if (sym.Name.empty())
				continue;
			// functions are disassembled, data is shown in hex (as the symbols view does)
			bool defined = sym.SectionIndex != 0 && sym.SectionIndex < 0xFF00 && sym.SectionIndex < m_Elf.Sections().size();
			bool place = defined && (sym.Type == 1 || sym.Type == 2 || sym.Type == 6 || sym.Type == 10);
			// a C++ name is found by what it is in the source; the details have the symbol
			auto demangled = DemangleItanium(sym.Name);
			SearchItem item{ .Kind = SearchKind::Symbol, .Name = demangled.empty() ? Widen(sym.Name) : Widen(demangled),
				.Details = std::format(L"{} {}{}", ElfFile::SymbolBindName(sym.Bind), ElfFile::SymbolTypeName(sym.Type), defined ? L"" : L", undefined"),
				.Value = sym.Value, .Code = sym.Type == 2 || sym.Type == 10 };
			if (!demangled.empty())
				item.Details += L": " + Widen(sym.Name);
			if (place) {
				item.Location = std::format(L"0x{:X}", sym.Value);
				// in a relocatable object the value is an offset in the section; -2: the symbol has no place
				item.Index = relocatable ? (int)sym.SectionIndex : -1;
			}
			else
				item.Index = -2;
			index.Add(std::move(item));
		}
		// the functions of the debug information that no symbol names (a stripped file with its debug file)
		if (!relocatable) {
			std::unordered_set<uint64_t> named;
			for (auto const& sym : m_Elf.Symbols())
				if (sym.Value)
					named.insert(sym.Value);
			for (auto const& f : m_ElfDebug.Dwarf.Functions) {
				if (!f.LowPc || named.contains(f.LowPc))
					continue;
				auto demangled = DemangleItanium(f.LinkageName);
				auto name = !demangled.empty() ? Widen(demangled) : Widen(f.Name.empty() ? f.LinkageName : f.Name);
				index.Add({ .Kind = SearchKind::Symbol, .Name = std::move(name), .Details = L"Function (DWARF)",
					.Location = std::format(L"0x{:X}", f.LowPc), .Value = f.LowPc, .Index = -1, .Code = true });
			}
		}
		for (auto const& dyn : m_Elf.Dynamic())
			if (dyn.Tag == 1)	// DT_NEEDED
				index.Add({ .Kind = SearchKind::ImportModule, .Name = Widen(dyn.Text), .Details = L"Needed library" });
		AddStringsToSearchIndex(m_Elf.Data());
	}
	if (m_PE || m_Elf || m_Object || m_Archive)
		AddViewsToSearchIndex(nullptr, L"", 0);
	if (m_Archive) {
		auto memberName = [&](int member) {
			return member >= 0 && member < (int)m_Archive.Members().size() ? Widen(m_Archive.Members()[member].Name) : std::wstring();
		};
		for (auto const& sym : m_Archive.Symbols())
			index.Add({ .Kind = SearchKind::Symbol, .Name = Widen(sym.Name), .Details = memberName(sym.Member),
				.Location = std::format(L"Member {}", sym.Member), .Index = sym.Member });
		for (auto const& imp : m_Archive.Imports())
			index.Add({ .Kind = SearchKind::Import, .Name = Widen(imp.Symbol), .Details = Widen(imp.Dll),
				.Location = std::format(L"Member {}", imp.Member), .Index = imp.Member });
	}
}

// An export of code (not one forwarded to another module), a function of the symbols (or one the user named), a public
// symbol in code; in an ELF file, a function symbol or one of the debug information
bool CMainFrame::FunctionAddress(SearchItem const& item, uint64_t& address) {
	if (m_Elf) {
		// (not those of a relocatable object: their values are offsets in their sections)
		if (item.Kind != SearchKind::Symbol || !item.Code || item.Index != -1 || !item.Value)
			return false;
		address = item.Value;
		return true;
	}
	if (!m_PE)
		return false;
	auto base = m_PE.GetImageBase();
	uint64_t rva = 0;
	if (item.Kind == SearchKind::Export) {
		auto exports = m_PE.GetExport();
		if (!exports)
			return false;
		auto it = std::ranges::find_if(exports->Funcs, [&](auto const& f) { return f.Ordinal == item.Value; });
		if (it == exports->Funcs.end() || !it->FuncRVA || !it->ForwarderName.empty())
			return false;
		rva = it->FuncRVA;
	}
	else if (item.Kind == SearchKind::Symbol && (item.Details.starts_with(L"Function") || item.Details.starts_with(L"Public symbol"))) {
		if (item.Value < base || item.Value - base > 0xFFFFFFFFULL)
			return false;
		rva = item.Value - base;
	}
	else
		return false;
	// in code (a public symbol may be of data)
	for (auto const& s : *m_PE.GetSecHeaders()) {
		auto& h = s.SecHdr;
		if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData)) {
			if (!(h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)))
				return false;
			address = base + rva;
			return true;
		}
	}
	return false;
}

// The public symbol at the address, undecorated (a C++ function's decorated name has its parameters): its name and its
// parameters, without what the return type, the calling convention and the like add
std::wstring CMainFrame::FunctionDeclaration(uint64_t address) {
	auto base = m_PE ? m_PE.GetImageBase() : 0;
	if (!m_PE || !m_Symbols || address < base || address - base > 0xFFFFFFFFULL)
		return {};
	long displacement = 0;
	auto sym = m_Symbols.GetSymbolByRVA((uint32_t)(address - base), SymbolTag::PublicSymbol, &displacement);
	if (!sym || displacement != 0 || !sym.Name().starts_with(L'?'))
		return {};
	// (and the undecorator's own: no "class", "struct", "enum" before a type's name, no __ptr64)
	constexpr DWORD NoClassPrefixes = 0x8000, NoPtr64 = 0x20000;
	WCHAR text[2048];
	auto flags = UNDNAME_NO_FUNCTION_RETURNS | UNDNAME_NO_MS_KEYWORDS | UNDNAME_NO_ALLOCATION_MODEL | UNDNAME_NO_ACCESS_SPECIFIERS |
		UNDNAME_NO_THROW_SIGNATURES | UNDNAME_NO_MEMBER_TYPE | UNDNAME_NO_RETURN_UDT_MODEL | NoClassPrefixes | NoPtr64;
	if (!::UnDecorateSymbolNameW(sym.Name().c_str(), text, _countof(text), flags))
		return sym.UndecoratedName();
	return text;
}

bool CMainFrame::GoToSearchItem(SearchItem const& item) {
	// a view of the tree, whatever the file
	if (item.Kind == SearchKind::View) {
		auto hItem = FindTreeItem((TreeItemType)item.Value);
		if (!hItem)
			return false;
		RecordNavigation();
		auto shown = ShowView(hItem);
		RecordNavigation();
		return shown;
	}
	if (m_PE) {
		switch (item.Kind) {
			case SearchKind::Address:
				switch (item.Index) {
					case 0: return GoToVa(item.Value);
					case 1: return GoToVa(m_PE.GetImageBase() + item.Value);
					case 2: return GoToFileOffset((int64_t)item.Value);
				}
				return false;
			case SearchKind::ClrType:
			case SearchKind::ClrMember:
			{
				auto hItem = FindTreeItem(TreeItemType::ClrTypes);
				if (!hItem)
					return false;
				RecordNavigation();
				if (!ShowView(hItem))
					return false;
				auto it = m_Views.find(TreeItemType::ClrTypes);
				auto view = it == m_Views.end() ? nullptr : dynamic_cast<CClrTypesView*>(it->second);
				if (!view)
					return false;
				RecordNavigation();
				auto selected = view->SelectToken((uint32_t)item.Value);
				RecordNavigation();
				return selected;
			}
			case SearchKind::ImportModule:
			case SearchKind::Import:
			{
				if (!ShowView(TreeItemType::DirectoryImports, nullptr, IDI_IMPORTS))
					return false;
				auto name = item.Kind == SearchKind::ImportModule ? item.Name : item.Value ? std::format(L"#{}", item.Value) : item.Name;
				return m_Views.at(TreeItemType::DirectoryImports)->SelectItem(name, item.Owner);
			}
			case SearchKind::Export:
				if (!ShowView(TreeItemType::DirectoryExports, nullptr, IDI_EXPORTS))
					return false;
				return m_Views.at(TreeItemType::DirectoryExports)->SelectItem(std::format(L"#{}", item.Value));
			case SearchKind::Resource:
			case SearchKind::Section:
			{
				auto type = TreeItemWithIndex(item.Kind == SearchKind::Section ? TreeItemType::Section : TreeItemType::Resource, (int64_t)(item.Index + 1) << ItemShift);
				auto hItem = FindTreeItem(type);
				if (!hItem)
					return false;
				RecordNavigation();
				auto shown = ShowView(type, hItem);
				RecordNavigation();
				return shown;
			}
			case SearchKind::String:
				return GoToFileOffset((int64_t)item.Value);
			case SearchKind::Symbol:
				return GoToVa(item.Value);
		}
		return false;
	}

	if (m_Object) {
		switch (item.Kind) {
			case SearchKind::Section:
				return ShowObjectSection(item.Index, -1);
			case SearchKind::Symbol:
				return item.Index >= 0 && ShowObjectSection(item.Index, item.Value);
			case SearchKind::String:
				// the section that has the string
				for (int i = 0; i < (int)m_Object.Sections().size(); i++) {
					auto const& sec = m_Object.Sections()[i];
					if (sec.PointerToRawData && item.Value >= sec.PointerToRawData && item.Value < (uint64_t)sec.PointerToRawData + sec.SizeOfRawData)
						return ShowObjectSection(i, item.Value - sec.PointerToRawData);
				}
				return false;
		}
		return false;
	}

	if (m_Elf) {
		switch (item.Kind) {
			case SearchKind::Section:
				return ShowElfSection(item.Index, 0, false);
			case SearchKind::Symbol:
				if (item.Index == -2)
					return false;
				return item.Index >= 0 ? ShowElfSection(item.Index, (int64_t)item.Value, item.Code) : ShowElfAddress(item.Value, item.Code);
			case SearchKind::String:
				return ShowElfFileOffset((int64_t)item.Value);
			case SearchKind::ImportModule:
				return ShowView(TreeItemType::ElfDynamic, FindTreeItem(TreeItemType::ElfDynamic));
		}
		return false;
	}

	if (m_Archive)
		return item.Index >= 0 && ShowArchiveMember(item.Index);
	return false;
}
