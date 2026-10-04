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
	}
	return -1;
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

		// the functions and the data of the symbols, then the public symbols that are elsewhere
		if (m_Symbols) {
			std::unordered_set<uint64_t> seen;
			auto add = [&](SymbolTag tag, PCWSTR kind) {
				for (auto const& sym : m_Symbols.FindChildren(nullptr, tag)) {
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
	else if (m_Archive) {
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

bool CMainFrame::GoToSearchItem(SearchItem const& item) {
	if (m_PE) {
		switch (item.Kind) {
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
