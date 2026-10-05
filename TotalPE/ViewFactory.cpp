#include "pch.h"
#include "resource.h"
#include "MainFrm.h"
#include "Interfaces.h"
#include "DebugView.h"
#include "BitmapView.h"
#include "SecurityView.h"
#include "ScintillaView.h"
#include "LoadConfigView.h"
#include "RelocationsView.h"
#include "SymbolsView.h"
#include "SectionsView.h"
#include "DataDirectoriesView.h"
#include "ExportsView.h"
#include "ImportsView.h"
#include "VersionView.h"
#include "AcceleratorTableView.h"
#include "ExceptionsView.h"
#include "ResourcesView.h"
#include "StructView.h"
#include "IconsView.h"
#include "StringMessageTableView.h"
#include "PEImageView.h"
#include "TlsView.h"
#include "DelayImportView.h"
#include "RichHeaderView.h"
#include "IATView.h"
#include "ClrView.h"
#include "BoundImportView.h"
#include "GlobalPtrView.h"
#include "DialogView.h"
#include "MenuView.h"
#include "AnomalyView.h"
#include "AnimatedCursorView.h"
#include "XrefView.h"
#include "OverlayView.h"
#include "FlowGraphView.h"
#include "ArchiveView.h"
#include "StringsView.h"
#include "FontView.h"
#include "EventManifestView.h"
#include "ResourceContent.h"
#include "TypeLibText.h"
#include "ObjectView.h"
#include "DebugInfoView.h"
#include "GuardTableView.h"
#include "ElfView.h"

std::pair<IView*, CMessageMap*> CMainFrame::CreateView(TreeItemType type) {
	CWaitCursor wait;
	auto symType = SymViewType::None;

	CString title;
	switch (type & TreeItemType::ItemMask) {
		case TreeItemType::AsmEntryPoint:
		{
			bool is32Bit = m_PE.GetFileInfo()->IsPE32;
			auto entry = is32Bit ? m_PE.GetNTHeader()->NTHdr32.OptionalHeader.AddressOfEntryPoint : m_PE.GetNTHeader()->NTHdr64.OptionalHeader.AddressOfEntryPoint;
			auto arch = ArchOf(m_PE);
			if (entry == 0 || !arch)
				return {};

			auto view = new CScintillaView(this, m_PE, L"Entry Point");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetLanguage(LexLanguage::Asm);

			ULONGLONG imageBase = m_PE.GetImageBase();
			auto offset = m_PE.GetOffsetFromRVA(entry);
			uint32_t size = 0x500;		// hard coded for now
			// the address is that of the entry point (its file offset is not its RVA, unless the alignments are the same)
			view->SetAsmCode(m_PE.GetSpan((uint32_t)offset, size), entry + imageBase, *arch);
			view->GetCtrl().SetReadOnly(true);
			auto hItem = InsertTreeItem(m_Tree, view->GetTitle(), GetIconIndex(IDI_BINARY), type, m_Views.at(TreeItemType::Image)->GetHTreeItem(), TVI_SORT);
			view->SetDeleteFromTree(true);
			view->SetHTreeItem(hItem);

			return { view, view };
		}

		case TreeItemType::ArchiveMembers:
		case TreeItemType::ArchiveSymbols:
		case TreeItemType::ArchiveImports:
		{
			auto kind = (type & TreeItemType::ItemMask) == TreeItemType::ArchiveMembers ? ArchiveViewKind::Members :
				(type & TreeItemType::ItemMask) == TreeItemType::ArchiveSymbols ? ArchiveViewKind::Symbols : ArchiveViewKind::Imports;
			auto view = new CArchiveView(this, m_Archive, kind);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		// the data of a member: opened from a list, and a tree item of its own while the view is open
		case TreeItemType::ArchiveMember:
		{
			auto index = (size_t)((int64_t)type >> ItemShift) - 1;
			if (!m_Archive || index >= m_Archive.Members().size())
				return {};
			auto const& member = m_Archive.Members()[index];
			CString name(std::wstring(member.Name.begin(), member.Name.end()).c_str());
			auto view = new CHexView(this, name + L" (Member)");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(m_Archive.MemberData(index));
			view->ShowInspector(true);
			// an object that was opened has a tree item for its data already
			if (!m_MemberObjects.contains((int)index)) {
				auto hItem = InsertTreeItem(m_Tree, view->GetTitle(), GetIconIndex(IDI_BINARY), type, m_hRoot, TVI_SORT);
				view->SetDeleteFromTree(true);
				view->SetHTreeItem(hItem);
			}
			return { view, view };
		}

		case TreeItemType::FileInHex:
		{
			auto view = new CHexView(this, L"PE in Hex");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(m_PE.GetSpan(0, m_PE.GetFileSize()));
			view->SetPE(m_PE, BuildPERegions(m_PE));	// color the headers, sections, directories and overlay
			view->ShowInspector(true);
			auto hItem = InsertTreeItem(m_Tree, view->GetTitle(), GetIconIndex(IDI_BINARY), type, m_Views.at(TreeItemType::Image)->GetHTreeItem(), TVI_SORT);
			view->SetDeleteFromTree(true);
			view->SetHTreeItem(hItem);

			return { view, view };
		}

		case TreeItemType::Image:
		{
			auto view = new CPEImageView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			if (m_Vt.Step != vt::Phase::Idle)
				view->SetVirusTotalStatus(m_Vt);
			return { view, view };
		}

		case TreeItemType::SymbolsFunctions:
			symType = SymViewType::Function;
		case TreeItemType::SymbolsTypes:
			if (symType == SymViewType::None)
				symType = SymViewType::UDT;
		case TreeItemType::SymbolsEnums:
			if (symType == SymViewType::None)
				symType = SymViewType::Enum;
		case TreeItemType::SymbolsGlobalData:
			if (symType == SymViewType::None)
				symType = SymViewType::Data;
		{
			auto view = new CSymbolsView(this, m_Symbols, symType);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::RichHeader:
		{
			auto view = new CRichHeaderView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryIAT:
		{
			auto view = new CIATView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryTLS:
		{
			auto view = new CTlsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryBoundImport:
		{
			auto view = new CBoundImportView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryGlobalPtr:
		{
			auto view = new CGlobalPtrView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryArch:
		{
			// reserved directory - show its raw contents
			auto const& dir = (*m_PE.GetDataDirs())[IMAGE_DIRECTORY_ENTRY_ARCHITECTURE].DataDir;
			auto view = new CHexView(this, L"Architecture");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(m_PE, (uint32_t)m_PE.GetOffsetFromRVA(dir.VirtualAddress), dir.Size);
			return { view, view };
		}

		case TreeItemType::Anomalies:
		{
			auto view = new CAnomalyView(this, m_Anomalies);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		// an object file, or a member of a library that is an object
		case TreeItemType::ObjectHeader:
		case TreeItemType::ObjectSections:
		case TreeItemType::ObjectSymbols:
		case TreeItemType::ObjectRelocations:
		case TreeItemType::ObjectLineNumbers:
		{
			auto member = ObjectItemMember(type);
			auto obj = FindObject(member);
			if (!obj.Object)
				return {};
			auto item = type & TreeItemType::ItemMask;
			auto kind = item == TreeItemType::ObjectHeader ? ObjectViewKind::Header : item == TreeItemType::ObjectSections ? ObjectViewKind::Sections :
				item == TreeItemType::ObjectSymbols ? ObjectViewKind::Symbols : item == TreeItemType::ObjectRelocations ? ObjectViewKind::Relocations :
				ObjectViewKind::LineNumbers;
			auto view = new CObjectView(this, *obj.Object, kind, member, obj.Owner);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::CodeView:
		case TreeItemType::CodeViewSymbols:
		case TreeItemType::CodeViewLines:
		case TreeItemType::CodeViewFiles:
		case TreeItemType::CodeViewTypes:
		{
			auto member = ObjectItemMember(type);
			auto obj = FindObject(member);
			if (!obj.Object)
				return {};
			auto item = type & TreeItemType::ItemMask;
			auto kind = item == TreeItemType::CodeView ? DebugInfoViewKind::Subsections : item == TreeItemType::CodeViewSymbols ? DebugInfoViewKind::Symbols :
				item == TreeItemType::CodeViewLines ? DebugInfoViewKind::Lines : item == TreeItemType::CodeViewFiles ? DebugInfoViewKind::Files :
				DebugInfoViewKind::Types;
			auto view = new CDebugInfoView(this, *obj.Object, *obj.CodeView, kind, member, obj.Owner);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		// the data of a section of an object
		case TreeItemType::ObjectSection:
		{
			auto obj = FindObject(ObjectItemMember(type));
			auto index = (size_t)ObjectItemSection(type);
			if (!obj.Object || index >= obj.Object->Sections().size())
				return {};
			auto const& sec = obj.Object->Sections()[index];
			auto title = std::format(L"{} ({}, Section)", std::wstring(sec.Name.begin(), sec.Name.end()), index + 1);
			if (obj.Owner)
				title = std::format(L"{}: {}", obj.Owner, title);
			auto view = new CHexView(this, CString(title.c_str()));
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(obj.Object->SectionData(index));
			view->ShowInspector(true);
			return { view, view };
		}

		case TreeItemType::ElfHeader:
		case TreeItemType::ElfProgramHeaders:
		case TreeItemType::ElfSections:
		case TreeItemType::ElfSymbols:
		case TreeItemType::ElfDynamic:
		case TreeItemType::ElfRelocations:
		case TreeItemType::ElfNotes:
		case TreeItemType::ElfDebugInfo:
		case TreeItemType::ElfCompileUnits:
		case TreeItemType::ElfFunctions:
		{
			static const std::pair<TreeItemType, ElfViewKind> kinds[] = {
				{ TreeItemType::ElfHeader, ElfViewKind::Header }, { TreeItemType::ElfProgramHeaders, ElfViewKind::ProgramHeaders },
				{ TreeItemType::ElfSections, ElfViewKind::Sections }, { TreeItemType::ElfSymbols, ElfViewKind::Symbols },
				{ TreeItemType::ElfDynamic, ElfViewKind::Dynamic }, { TreeItemType::ElfRelocations, ElfViewKind::Relocations },
				{ TreeItemType::ElfNotes, ElfViewKind::Notes }, { TreeItemType::ElfDebugInfo, ElfViewKind::DebugInfo },
				{ TreeItemType::ElfCompileUnits, ElfViewKind::CompileUnits }, { TreeItemType::ElfFunctions, ElfViewKind::Functions },
			};
			auto item = type & TreeItemType::ItemMask;
			auto kind = std::ranges::find(kinds, item, &std::pair<TreeItemType, ElfViewKind>::first)->second;
			auto view = new CElfView(this, m_Elf, kind, &m_ElfDebug);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		// the data of a section of an ELF file (the index is one more than the section's: 0 is not an index)
		case TreeItemType::ElfSection:
		{
			auto index = (size_t)((int64_t)type >> ItemShift) - 1;
			if (!m_Elf || index >= m_Elf.Sections().size())
				return {};
			auto const& name = m_Elf.Sections()[index].Name;
			auto view = new CHexView(this, CString(std::format(L"{} (Section)", std::wstring(name.begin(), name.end())).c_str()));
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(m_Elf.SectionData(index));
			view->ShowInspector(true);
			return { view, view };
		}

		case TreeItemType::ElfFileInHex:
		{
			auto view = new CHexView(this, L"File in Hex");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(m_Elf.Data());
			view->ShowInspector(true);
			return { view, view };
		}

		case TreeItemType::GuardTable:
		{
			auto index = (size_t)((int64_t)type >> ItemShift) - 1;
			if (index >= m_GuardTables.size())
				return {};
			auto view = new CGuardTableView(this, m_PE, m_GuardTables[index]);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::Strings:
		{
			auto view = m_Elf ? new CStringsView(this, m_Elf) : new CStringsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::Overlay:
		{
			auto view = new COverlayView(this, m_PE, m_Overlay);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryCLR:
		{
			auto view = new CClrView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryDelayImport:
		{
			auto view = new CDelayImportView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryLoadConfig:
		{
			auto view = new CLoadConfigView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::Headers:
		{
			auto view = new CHexView(this, L"Headers");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetData(m_PE, 0, m_PE.GetFileInfo()->IsPE64 ?
				m_PE.GetNTHeader()->NTHdr64.OptionalHeader.SizeOfHeaders : m_PE.GetNTHeader()->NTHdr32.OptionalHeader.SizeOfHeaders);
			view->SetPE(m_PE, BuildPERegions(m_PE));
			return { view, view };
		}

		case TreeItemType::DOSHeader:
		{
			auto sym = GetSymbolForName(L"ntdll.dll", L"_IMAGE_DOS_HEADER");
			if (!sym && m_Symbols) {
				auto symbols = m_Symbols.FindChildren(L"_IMAGE_DOS_HEADER");
				if (!symbols.empty())
					sym = symbols[0];
			}
			if (!sym)
				return {};

			auto view = new CStructView(this, sym, L"DOS Header");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetValue(const_cast<IMAGE_DOS_HEADER*>(m_PE.GetMSDOSHeader()));
			return { view, view };
		}

		case TreeItemType::NTHeader:
		{
			auto name = m_PE.GetFileInfo()->IsPE32 ? L"_IMAGE_NT_HEADERS" : L"_IMAGE_NT_HEADERS64";
			auto sym = GetSymbolForName(L"ntdll.dll", name);
			if (!sym && m_Symbols) {
				auto symbols = m_Symbols.FindChildren(name);
				if (!symbols.empty())
					sym = symbols[0];
			}
			if (!sym)		// temporary
				return {};

			auto view = new CStructView(this, sym, L"NT Header");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetPEOffset(m_PE, m_PE.GetNTHeader()->dwOffset);
			return { view, view };
		}

		case TreeItemType::Sections:
		{
			auto view = new CSectionsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectorySecurity:
		{
			auto view = new CSecurityView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryReloc:
		{
			auto view = new CRelocationsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryExports:
		{
			auto view = new CExportsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryImports:
		{
			auto view = new CImportsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryExceptions:
		{
			auto view = new CExceptionsView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryDebug:
		{
			auto view = new CDebugView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::Directories:
		{
			auto view = new CDataDirectoriesView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::DirectoryResources:
		{
			auto view = new CResourcesView(this, m_PE);
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			return { view, view };
		}

		case TreeItemType::Section:
		{
			auto const& sec = m_PE.GetSecHeaders()->at(((size_t)type >> ItemShift) - 1);
			auto view = new CHexView(this, CString(sec.SectionName.c_str()) + L" (Section)");
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetData(m_PE, sec.SecHdr.PointerToRawData, sec.SecHdr.SizeOfRawData);
			return { view, view };
		}

		case TreeItemType::Resource:
			return CreateResourceView(type);

		case TreeItemType::ResourceHex:
		{
			auto index = (size_t)((int64_t)type >> ItemShift) - 1;
			if (index >= m_FlatResources.size())
				return {};
			auto const& res = m_FlatResources[index];
			auto view = new CHexView(this, std::format(L"{} ({}, Hex)", res.Name, res.Type).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			SetResourceHexData(*view, res);
			auto resItem = TreeItemWithIndex(TreeItemType::Resource, (int64_t)(index + 1) << ItemShift);
			if (auto hParent = FindTreeItem(resItem))
				view->SetHTreeItem(InsertTreeItem(m_Tree, L"Hex", GetIconIndex(IDI_BINARY), type, hParent));
			view->SetDeleteFromTree(true);
			return { view, view };
		}
	}
	return {};
}

// The data of a resource, at its offset in the file (so that the hex view and the inspector show where it is), or from 0 if
// the bytes there are not those of the resource
void CMainFrame::SetResourceHexData(CHexView& view, FlatResource const& res) const {
	if (m_PE && !res.Data.empty()) {
		uint64_t offset = res.Offset;
		auto size = res.Data.size();
		if (offset > 0 && offset + size <= m_PE.GetFileSize() && memcmp(m_PE.GetData() + offset, res.Data.data(), size) == 0) {
			view.SetData(m_PE, (uint32_t)offset, (uint32_t)size);
			return;
		}
	}
	view.SetData(res.Data);
}

namespace {
	// The numbers of the standard resource types (the RT_ macros are pointers, which cannot be case labels)
	enum ResourceTypeId : WORD {
		ResCursor = 1, ResBitmap = 2, ResIcon = 3, ResMenu = 4, ResDialog = 5, ResString = 6, ResAccelerator = 9,
		ResMessageTable = 11, ResGroupCursor = 12, ResGroupIcon = 14, ResVersion = 16, ResAniCursor = 21, ResAniIcon = 22,
		ResManifest = 24,
	};
}

std::pair<IView*, CMessageMap*> CMainFrame::CreateResourceView(TreeItemType type) {
	auto& res = m_FlatResources[((uint32_t)type >> ItemShift) - 1];
	switch (res.TypeID) {
		case ResVersion:
		{
			auto view = new CVersionView(this, (res.Name + L" (Version)").c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetData(res.Data);
			return { view, view };
		}

		case ResDialog:
		{
			auto view = new CDialogView(this, (res.Name + L" (Dialog)").c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			if (view->SetData(res.Data))
				return { view, view };
			view->DestroyWindow();
			break;	// malformed template - fall back to the hex view below
		}

		case ResMenu:
		{
			auto view = new CMenuView(this, (res.Name + L" (Menu)").c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			if (view->SetData(res.Data))
				return { view, view };
			view->DestroyWindow();
			break;
		}

		case ResBitmap:
		{
			auto view = new CBitmapView(this, (res.Name + L" (Bitmap)").c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetData(res.Data);
			return { view, view };
		}

		case ResAccelerator:
		{
			auto view = new CAcceleratorTableView(this, (res.Name + L" (Accel)").c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->AddAccelTable(res.Data);
			return { view, view };
		}

		case ResManifest:
		{
			auto view = new CScintillaView(this, m_PE, (res.Name + L" (Manifest)").c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetLanguage(LexLanguage::Xml);
			view->SetText(CStringA((PCSTR)res.Data.data(), (int)res.Data.size()));
			view->GetCtrl().SetReadOnly(true);
			return { view, view };
		}

		case ResString:
		case ResMessageTable:
		{
			auto st = res.TypeID == ResString;
			auto view = new CStringMessageTableView(this, (res.Name + (st ? L" (String Table)" : L" (Message Table)")).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			if (st)
				view->SetStringTableData(res.Data, res.NameID);
			else
				view->SetMessageTableData(res.Data);
			return { view, view };
		}

		case ResAniCursor:
		case ResAniIcon:
		{
			auto view = new CAnimatedCursorView(this, (res.Name + (res.TypeID == ResAniCursor ? L" (Animated Cursor)" : L" (Animated Icon)")).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			if (view->SetData(res.Data))
				return { view, view };
			view->DestroyWindow();
			break;	// not a usable .ani file: it is shown as raw data below
		}

		case ResIcon:
		case ResCursor:
		{
			auto icon = res.TypeID == ResIcon;
			auto view = new CIconsView(this, (res.Name + (icon ? L" (Icon)" : L" (Cursor)")).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetIconData(res.Data, icon);
			return { view, view };
		}

		case ResGroupIcon:
		case ResGroupCursor:
		{
			auto view = new CIconsView(this, (res.Name + (res.TypeID == ResGroupIcon ? L" (Icon Group)" : L" (Cursor Group)")).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetGroupIconData(res.Data);
			return { view, view };
		}
	}

	// the instrumentation manifest of ETW providers
	if (res.TypeStr == L"WEVT_TEMPLATE")
		if (auto manifest = ParseEventManifest(res.Data)) {
			auto view = new CEventManifestView(this, (res.Name + L" (Event Manifest)").c_str(), std::move(*manifest), m_PE.GetPath());
			if (!view->DoCreate(m_Tabs))
				return {};
			return { view, view };
		}

	//
	// the other types: what the data looks like decides how it is shown
	//
	auto title = std::format(L"{} ({})", res.Name, res.Type);
	auto content = DetectResourceContent(res.Data, res.TypeID, res.TypeStr);
	auto showText = [&](std::string const& utf8, LexLanguage lang) -> std::pair<IView*, CMessageMap*> {
		auto view = new CScintillaView(this, m_PE, title.c_str());
		if (!view->DoCreate(m_Tabs))
			return {};
		view->GetCtrl().SetCodePage(SC_CP_UTF8);
		view->SetLanguage(lang);
		view->SetText(utf8.c_str());
		view->GetCtrl().SetReadOnly(true);
		return { view, view };
	};
	switch (content) {
		case ResourceContent::Image:
		{
			auto view = new CBitmapView(this, title.c_str());
			if (!view->DoCreate(m_Tabs))
				return {};
			if (view->SetImage(res.Data))
				return { view, view };
			view->DestroyWindow();
			break;
		}

		case ResourceContent::Xml: return showText(ResourceTextToUtf8(res.Data), LexLanguage::Xml);
		case ResourceContent::Html: return showText(ResourceTextToUtf8(res.Data), LexLanguage::Html);
		case ResourceContent::Inf: return showText(ResourceTextToUtf8(res.Data), LexLanguage::Inf);
		case ResourceContent::Text:
		case ResourceContent::RegistryScript:
			return showText(ResourceTextToUtf8(res.Data), LexLanguage::Text);

		case ResourceContent::TypeLib:
		{
			std::wstring error;
			auto text = DescribeTypeLib(res.Data, error);
			if (text.empty())
				break;
			return showText((PCSTR)CW2A(text.c_str(), CP_UTF8), LexLanguage::Text);
		}

		case ResourceContent::Font:
		{
			auto view = new CFontView(this, title.c_str());
			if (!view->DoCreate(m_Tabs))
				return {};
			view->SetData(res.Data);	// a font that cannot be loaded says so
			return { view, view };
		}

		case ResourceContent::Executable:
			title = std::format(L"{} ({}, Executable)", res.Name, res.Type);
			break;
	}

	//
	// all other resources - use a hex view
	//
	auto view = new CHexView(this, title.c_str());
	if (!view->DoCreate(m_Tabs))
		return {};

	SetResourceHexData(*view, res);
	return { view, view };
}

bool CMainFrame::CreateAssemblyView(std::span<const std::byte> code, uint64_t address, uint32_t rva, PCWSTR title, TreeItemType parent) {
	auto it = m_Views.find(parent);
	if (it == m_Views.end()) {
		//
		// parent node does not exist
		//
		ATLASSERT(false);
		return false;
	}

	auto hParent = it->second->GetHTreeItem();
	//
	// check for duplicate
	//
	auto hItem = FindChild(m_Tree, hParent, title);
	if (hItem) {
		ShowView(hItem);
		return true;
	}

	auto arch = ArchOf(m_PE);
	if (!arch) {
		AtlMessageBox(m_hWnd, L"The code of this machine is not disassembled (x86, x64 and ARM64 are)", IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}
	auto view = new CScintillaView(this, m_PE, title);
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return false;
	}

	view->SetLanguage(LexLanguage::Asm);
	view->SetAsmCode(code, address, *arch);
	view->GetCtrl().SetReadOnly(true);
	view->SetDeleteFromTree(true);

	auto image = GetIconIndex(IDI_BINARY);
	auto itemType = TreeItemWithIndex(TreeItemType::Disassembly, (int64_t)rva << ItemShift);
	hItem = InsertTreeItem(m_Tree, title, image, itemType, hParent, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });

	return true;
}

bool CMainFrame::ShowXrefs(uint64_t va) {
	if (!m_PE)
		return false;
	auto base = m_PE.GetImageBase();
	if (va < base || va - base > 0xFFFFFFFFULL)
		return false;

	auto refs = GetXrefs().To(va);
	if (refs.empty()) {
		AtlMessageBox(m_hWnd, std::format(L"Nothing in the code refers to 0x{:X}.", va).c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}

	auto parent = m_Views.find(TreeItemType::Image);
	if (parent == m_Views.end())
		return false;
	auto hParent = parent->second->GetHTreeItem();

	auto name = ResolveVa(va);
	auto title = std::format(L"Xrefs to {}", name.empty() ? std::format(L"0x{:X}", va) : name);
	if (auto hItem = FindChild(m_Tree, hParent, title.c_str())) {
		ShowView(hItem);
		return true;
	}

	auto view = new CXrefView(this, m_PE, va, refs, title.c_str());
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return false;
	}
	view->SetDeleteFromTree(true);

	auto image = GetIconIndex(IDI_BINARY);
	auto itemType = TreeItemWithIndex(TreeItemType::Xrefs, (int64_t)(va - base) << ItemShift);
	auto hItem = InsertTreeItem(m_Tree, title.c_str(), image, itemType, hParent, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return true;
}

bool CMainFrame::ShowFlowGraph(uint64_t va) {
	if (!m_PE)
		return false;
	auto base = m_PE.GetImageBase();
	if (va < base || va - base > 0xFFFFFFFFULL)
		return false;

	auto const& xrefs = GetXrefs();
	auto function = FindFunctionStart(m_PE, xrefs, va);

	// next to the disassembly of the function: under Exports for an exported function, otherwise under the image
	auto parentType = TreeItemType::Image;
	if (auto exports = m_PE.GetExport(); exports && m_Views.contains(TreeItemType::DirectoryExports))
		for (auto const& f : exports->Funcs)
			if (f.ForwarderName.empty() && base + f.FuncRVA == function) {
				parentType = TreeItemType::DirectoryExports;
				break;
			}
	auto parent = m_Views.find(parentType);
	if (parent == m_Views.end())
		return false;
	auto hParent = parent->second->GetHTreeItem();

	// the graph of the function may be open already: found by its item, not by its title (the disassembly of an export
	// has the same title, under the same parent)
	auto itemType = TreeItemWithIndex(TreeItemType::FlowGraph, (int64_t)(function - base) << ItemShift);
	if (auto it = m_Views.find(itemType); it != m_Views.end()) {
		ShowView(it->second->GetHTreeItem());
		return true;
	}

	auto name = ResolveVa(function);
	auto title = std::format(L"{}", name.empty() ? std::format(L"0x{:X}", function) : name);

	auto view = new CFlowGraphView(this, m_PE, function, title.c_str());
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		delete view;
		return false;
	}
	if (!view->Build()) {
		view->DestroyWindow();
		AtlMessageBox(m_hWnd, std::format(L"There is no code at 0x{:X} that can be shown as a graph.", function).c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}
	view->SetDeleteFromTree(true);

	auto image = GetIconIndex(IDI_FLOW);
	auto hItem = InsertTreeItem(m_Tree, title.c_str(), image, itemType, hParent, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return true;
}
