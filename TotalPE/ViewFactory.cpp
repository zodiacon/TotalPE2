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

std::pair<IView*, CMessageMap*> CMainFrame::CreateView(TreeItemType type) {
	CWaitCursor wait;
	auto symType = SymViewType::None;

	CString title;
	switch (type & TreeItemType::ItemMask) {
		case TreeItemType::AsmEntryPoint:
		{
			bool is32Bit = m_PE.GetFileInfo()->IsPE32;
			auto entry = is32Bit ? m_PE.GetNTHeader()->NTHdr32.OptionalHeader.AddressOfEntryPoint : m_PE.GetNTHeader()->NTHdr64.OptionalHeader.AddressOfEntryPoint;
			if (entry == 0)
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
			view->SetAsmCode(m_PE.GetSpan((uint32_t)offset, size), offset + imageBase, is32Bit);
			view->GetCtrl().SetReadOnly(true);
			auto hItem = InsertTreeItem(m_Tree, view->GetTitle(), GetIconIndex(IDI_BINARY), type, m_Views.at(TreeItemType::Image)->GetHTreeItem(), TVI_SORT);
			view->SetDeleteFromTree(true);
			view->SetHTreeItem(hItem);

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
	}
	return {};
}

std::pair<IView*, CMessageMap*> CMainFrame::CreateResourceView(TreeItemType type) {
	auto& res = m_FlatResources[((uint32_t)type >> ItemShift) - 1];
	auto const resId = MAKEINTRESOURCE(res.TypeID);
	if (resId == RT_VERSION) {
		auto view = new CVersionView(this, (res.Name + L" (Version)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		view->SetData(res.Data);
		return { view, view };
	}
	if (resId == RT_DIALOG) {
		auto view = new CDialogView(this, (res.Name + L" (Dialog)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		if (view->SetData(res.Data))
			return { view, view };
		view->DestroyWindow();
		// malformed template - fall back to the hex view below
	}
	else if (resId == RT_MENU) {
		auto view = new CMenuView(this, (res.Name + L" (Menu)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		if (view->SetData(res.Data))
			return { view, view };
		view->DestroyWindow();
	}

	if (resId == RT_BITMAP) {
		auto view = new CBitmapView(this, (res.Name + L" (Bitmap)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		view->SetData(res.Data);
		return { view, view };
	}
	else if (resId == RT_ACCELERATOR) {
		auto view = new CAcceleratorTableView(this, (res.Name + L" (Accel)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		view->AddAccelTable(res.Data);
		return { view, view };
	}
	else if (resId == RT_MANIFEST) {
		auto view = new CScintillaView(this, m_PE, (res.Name + L" (Manifest)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		view->SetLanguage(LexLanguage::Xml);
		view->SetText(CStringA((PCSTR)res.Data.data(), (int)res.Data.size()));
		view->GetCtrl().SetReadOnly(true);
		return { view, view };
	}
	else if (resId == RT_STRING || resId == RT_MESSAGETABLE) {
		auto st = resId == RT_STRING;
		auto view = new CStringMessageTableView(this, (res.Name + (st ? L" (String Table)" : L" (Manifest)")).c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		if (st)
			view->SetStringTableData(res.Data, res.NameID);
		else
			view->SetMessageTableData(res.Data);
		return { view, view };

	}

	if (resId == RT_ANICURSOR || resId == RT_ANIICON) {
		auto view = new CAnimatedCursorView(this, (res.Name + (resId == RT_ANICURSOR ? L" (Animated Cursor)" : L" (Animated Icon)")).c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		if (view->SetData(res.Data))
			return { view, view };
		view->DestroyWindow();
		// not a usable .ani file: it is shown as raw data below
	}

	bool icon = resId == RT_ICON || resId == RT_GROUP_ICON;
	bool group = resId == RT_GROUP_ICON || resId == RT_GROUP_CURSOR;
	if (icon || group || resId == RT_CURSOR) {
		if (!group) {
			auto view = new CIconsView(this, (res.Name + (icon ? L" (Icon)" : L" (Cursor)")).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetIconData(res.Data, icon);
			return { view, view };
		}
		else {
			auto view = new CIconsView(this, (res.Name + (icon ? L" (Icon Group)" : L" (Cursor Group)")).c_str());
			if (!view->DoCreate(m_Tabs))
				return {};

			view->SetGroupIconData(res.Data);
			return { view, view };
		}
	}
	//
	// all other resources - use a hex view
	//
	auto view = new CHexView(this, std::format(L"{} ({})", res.Name, res.Type).c_str());
	if (!view->DoCreate(m_Tabs))
		return {};

	view->SetData(res.Data);
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

	auto view = new CScintillaView(this, m_PE, title);
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return false;
	}

	view->SetLanguage(LexLanguage::Asm);
	view->SetAsmCode(code, address, m_PE.GetFileInfo()->IsPE32);
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

	auto name = ResolveVa(function);
	auto title = std::format(L"{}", name.empty() ? std::format(L"0x{:X}", function) : name);
	if (auto hItem = FindChild(m_Tree, hParent, title.c_str())) {
		ShowView(hItem);
		return true;
	}

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
	auto itemType = TreeItemWithIndex(TreeItemType::FlowGraph, (int64_t)(function - base) << ItemShift);
	auto hItem = InsertTreeItem(m_Tree, title.c_str(), image, itemType, hParent, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return true;
}
