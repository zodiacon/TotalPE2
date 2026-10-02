// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "AboutDlg.h"
#include "MainFrm.h"
#include <IconHelper.h>
#include "SecurityHelper.h"
#include <ToolbarHelper.h>
#include "PEStrings.h"
#include "AppSettings.h"
#include "SymbolSettingsDlg.h"
#include "GoToDlg.h"
#include <thread>
#include <WTLHelper.h>

const int WindowMenuPosition = 5;

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (m_pFindDlg && m_pFindDlg->IsDialogMessageW(pMsg))
		return TRUE;

	if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	return m_Tabs.PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UIUpdateToolBar();
	return FALSE;
}

void CMainFrame::OnFinalMessage(HWND) {
	delete this;
}

bool CMainFrame::OnTreeDoubleClick(HWND, HTREEITEM hItem) {
	return ShowView(hItem);
}

void CMainFrame::UpdateUI() {
	auto const fi = m_PE ? m_PE.GetFileInfo() : nullptr;
	UIEnable(ID_PE_DISASSEMBLEENTRYPOINT, fi != nullptr);
	UIEnable(ID_VIEW_EXPORTS, fi && fi->HasExport);
	UIEnable(ID_VIEW_IMPORTS, fi && fi->HasImport);
	UIEnable(ID_VIEW_RESOURCES, fi && fi->HasResource);
	UIEnable(ID_VIEW_DEBUG, fi && fi->HasDebug);
	UIEnable(ID_VIEW_DIRECTORIES, fi && fi->HasDataDirs);
	UIEnable(ID_VIEW_SECTIONS, fi && fi->HasSections);
	UIEnable(ID_PE_SECURITY, fi && fi->HasSecurity);
	UIEnable(ID_FILE_CLOSE, fi != nullptr);
	UIEnable(ID_FILE_SAVE, fi != nullptr);
	UIEnable(ID_VIEW_MANIFEST, fi && m_hResManifest != nullptr);
	UIEnable(ID_VIEW_VERSION, fi && m_hResVersion != nullptr);
	UIEnable(ID_VIEW_OVERLAY, fi && m_hOverlay != nullptr);
	UIEnable(ID_FILE_OPENINANEWWINDOW, fi != nullptr);
	UIEnable(ID_EDIT_COPY, FALSE);
	UIEnable(ID_EDIT_FIND, fi && m_Tabs.GetActivePage() >= 0);
	UIEnable(ID_PE_ENTIREFILEINHEX, fi != nullptr);
}

int CMainFrame::GetResourceIconIndex(WORD resType) const {
	return ResourceTypeToIcon(resType);
}

void CMainFrame::InitMenu(HMENU hMenu) {
	MenuItemData commands[] {
		{ ID_EDIT_COPY, IDI_COPY },
		{ ID_EDIT_PASTE, IDI_PASTE },
		{ ID_FILE_OPEN, IDI_OPEN },
		{ ID_FILE_SAVE, IDI_SAVE },
		{ ID_ICON_EXPORT, IDI_SAVE },
		{ ID_VIEW_SECTIONS, IDI_SECTIONS },
		{ ID_VIEW_DIRECTORIES, IDI_DIR_OPEN },
		{ ID_VIEW_RESOURCES, IDI_RESOURCE },
		{ ID_VIEW_EXPORTS, IDI_EXPORTS },
		{ ID_VIEW_IMPORTS, IDI_IMPORTS },
		{ ID_VIEW_MANIFEST, IDI_MANIFEST },
		{ ID_VIEW_VERSION, IDI_VERSION },
		{ ID_VIEW_DISASSEMBLE, IDI_BINARY },
		{ ID_PE_SECURITY, IDI_SECURITY },
		{ ID_VIEW_DEBUG, IDI_DEBUG },
		{ ID_EDIT_FIND, IDI_FIND },
		{ ID_EDIT_FIND_PREVIOUS, IDI_FIND_PREV },
		{ ID_EDIT_FIND_NEXT, IDI_FIND_NEXT },
		{ ID_FILE_RUNASADMINISTRATOR, 0, IconHelper::GetShieldIcon() },
		{ ID_FILE_CLOSE, IDI_CLOSE },
		{ ID_WINDOW_CLOSE, IDI_WIN_CLOSE },
		{ ID_WINDOW_CLOSE_ALL, IDI_WIN_CLOSEALL },

	};
	WTLHelper::InitMenu(hMenu, commands, _countof(commands));
}

TreeItemType CMainFrame::GetIndex(TreeItemType value) {
	return TreeItemType();
}

DiaSymbol CMainFrame::GetSymbolForName(PCWSTR mod, PCWSTR name) const {
	auto& symbols = m_SymbolsForModules;
	auto it = symbols.find(mod);
	if (it == symbols.end()) {
		DiaSession session;
		session.SetSymbolPath(AppSettings::Get().BuildSymbolSearchPath().c_str());
		WCHAR path[MAX_PATH];
		if (m_PE.GetFileInfo()->IsPE32)
			::GetSystemWow64Directory(path, _countof(path));
		else
			::GetSystemDirectory(path, _countof(path));
		wcscat_s(path, L"\\");
		wcscat_s(path, mod);
		if (!session.OpenImage(path))
			return DiaSymbol::Empty;
		it = symbols.insert({ mod, std::move(session) }).first;
	}

	auto const& session = it->second;
	auto sym = session.FindChildren(name);
	return sym.empty() ? DiaSymbol::Empty : sym[0];
}

bool CMainFrame::AddToolBar(HWND tb) {
	return UIAddToolBar(tb);
}

bool CMainFrame::DeleteTreeItem(HTREEITEM hItem) {
	return m_Tree.DeleteItem(hItem);
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	DragAcceptFiles();
	auto& settings = AppSettings::Get();
	m_RecentFiles.Set(settings.RecentFiles());
	UpdateRecentFilesMenu();

	CreateSimpleStatusBar();
	m_StatusBar.SubclassWindow(m_hWndStatusBar);
	int parts[] = { 200, 560, 760, 960, 1160 };
	m_StatusBar.SetParts(_countof(parts), parts);

	ToolBarButtonInfo const buttons[] = {
		{ ID_FILE_OPEN, IDI_OPEN },
		{ 0 },
		{ ID_EDIT_COPY, IDI_COPY },
		{ 0 },
		{ ID_VIEW_SECTIONS, IDI_SECTIONS },
		{ ID_VIEW_DIRECTORIES, IDI_DIR_OPEN },
		{ ID_VIEW_RESOURCES, IDI_RESOURCE },
		{ ID_VIEW_EXPORTS, IDI_EXPORTS },
		{ ID_VIEW_IMPORTS, IDI_IMPORTS },
		{ ID_VIEW_MANIFEST, IDI_MANIFEST },
		{ ID_VIEW_VERSION, IDI_VERSION },
		{ ID_VIEW_DEBUG, IDI_DEBUG},
		{ ID_PE_SECURITY, IDI_SECURITY },
		{ 0 },
		{ ID_EDIT_FIND, IDI_FIND},
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	auto tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons));

	AddSimpleReBarBand(tb);
	UIAddToolBar(tb);

	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);
	m_Tabs.m_bTabCloseButton = true;
	m_Tabs.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT);

	m_Splitter.SetSplitterPanes(m_Tree, m_Tabs);
	m_Splitter.SetSplitterPosPct(15);

	// register object for message filtering and idle updates
	auto pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	CMenuHandle menuMain = GetMenu();

	CMenuHandle hMenu = GetMenu();
	if (SecurityHelper::IsRunningElevated()) {
		hMenu.GetSubMenu(0).DeleteMenu(ID_FILE_RUNASADMINISTRATOR, MF_BYCOMMAND);
		hMenu.GetSubMenu(0).DeleteMenu(0, MF_BYPOSITION);
		CString text;
		GetWindowText(text);
		SetWindowText(text + L" (Administrator)");
	}
	auto hWinMenu = menuMain.GetSubMenu(WindowMenuPosition);
	//m_Tabs.m_bWindowsMenuItem = true;
	m_Tabs.SetWindowMenu(hWinMenu);

	InitMenu(hMenu);
	UIAddMenu(hMenu);
	UIAddMenu(IDR_CONTEXT);

	if (settings.DarkMode()) {
		UISetCheck(ID_OPTIONS_DARKMODE, true);
	}

	UISetCheck(ID_VIEW_STATUS_BAR, settings.ViewStatusBar());
	SetAlwaysOnTop(settings.AlwaysOnTop());

	if(s_Frames == 1)
		ParseCommandLine();
	UpdateUI();

	return 0;
}

bool CMainFrame::ShowView(TreeItemType data, HTREEITEM hItem, UINT icon) {
	if (hItem) {
		m_Tree.EnsureVisible(hItem);
		m_Tree.SelectItem(hItem);
	}
	if (auto it = m_Views.find(data); it != m_Views.end()) {
		int count = m_Tabs.GetPageCount();
		for (int i = 0; i < count; i++) {
			if (m_Tabs.GetPageHWND(i) == it->second->GetHwnd()) {
				m_Tabs.SetActivePage(i);
				return true;
			}
		}
	}
	else {
		auto [view, map] = CreateView(data);
		int image;
		if (view) {
			if (icon)
				image = GetIconIndex(icon);
			else {
				int dummy;
				m_Tree.GetItemImage(hItem, image, dummy);
			}
			if (!hItem)
				hItem = view->GetHTreeItem();
			view->SetHTreeItem(hItem);
			m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, map);
			m_Views.insert({ data, view });
			m_Views2.insert({ view->GetHwnd(), data });
			return true;
		}
	}
	return false;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	if (--s_Frames == 0) {
		WINDOWPLACEMENT wp{ sizeof(wp) };
		GetWindowPlacement(&wp);
		auto& settings = AppSettings::Get();
		settings.MainWindowPlacement(wp);
		settings.AlwaysOnTop(GetExStyle() & WS_EX_TOPMOST);

		bHandled = FALSE;
	}

	// unregister message filtering and idle updates
	auto pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	return 1;
}

bool CMainFrame::ShowView(HTREEITEM hItem) {
	auto data = GetItemData<TreeItemType>(m_Tree, hItem);
	return ShowView(data, hItem);
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

HIMAGELIST CMainFrame::GetImageList() const {
	return m_TreeImages.m_hImageList;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto bVisible = !::IsWindowVisible(m_hWndStatusBar);
	::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) const {
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnNewWindow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto frame = new CMainFrame;
	frame->CreateEx();
	frame->ShowWindow(SW_SHOWDEFAULT);
	return 0;
}

LRESULT CMainFrame::OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (int page = m_Tabs.GetActivePage(); page >= 0) {
		CloseTab(page);
		m_Tabs.RemovePage(page);
	}
	else
		::MessageBeep((UINT)-1);

	return 0;
}

int CMainFrame::GetTreeIcon(UINT id) {
	ATLASSERT(s_ImageIndices.contains(id));
	return s_ImageIndices[id];
}

LRESULT CMainFrame::OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Tabs.RemoveAllPages();
	m_Views.clear();
	m_Views2.clear();

	return 0;
}

LRESULT CMainFrame::OnWindowActivate(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int nPage = wID - ID_WINDOW_TABFIRST;
	m_Tabs.SetActivePage(nPage);

	return 0;
}

LRESULT CMainFrame::OnRunAsAdmin(WORD, WORD, HWND, BOOL&) {
	if (SecurityHelper::RunElevated(m_PE.GetPath().c_str(), true)) {
		s_Frames = 1;
		PostMessage(WM_CLOSE);
	}

	return 0;
}

CFindReplaceDialog* CMainFrame::GetFindDialog() {
	return m_pFindDlg;
}

void CMainFrame::BuildNamedRvas() {
	m_NamedRvas.clear();
	auto widen = [](std::string const& s) { return std::wstring(s.begin(), s.end()); };

	if (auto exp = m_PE.GetExport()) {
		for (auto& f : exp->Funcs) {
			if (f.FuncRVA == 0 || !f.ForwarderName.empty())
				continue;
			m_NamedRvas.emplace_back(f.FuncRVA, f.FuncName.empty() ? std::format(L"Ordinal#{}", f.Ordinal) : widen(f.FuncName));
		}
	}

	if (auto imports = m_PE.GetImport()) {
		auto ptrSize = m_PE.GetFileInfo()->IsPE64 ? 8u : 4u;
		for (auto& imp : *imports) {
			DWORD slot = imp.ImportDesc.FirstThunk;
			if (slot == 0)
				continue;
			auto module = widen(imp.ModuleName);
			for (auto& fn : imp.ImportFunc) {
				m_NamedRvas.emplace_back(slot, module + L"!" + (fn.FuncName.empty() ? L"(ordinal)" : widen(fn.FuncName)));
				slot += ptrSize;
			}
		}
	}

	auto const& hdr = *m_PE.GetNTHeader();
	auto entry = m_PE.GetFileInfo()->IsPE64 ? hdr.NTHdr64.OptionalHeader.AddressOfEntryPoint : hdr.NTHdr32.OptionalHeader.AddressOfEntryPoint;
	if (entry)
		m_NamedRvas.emplace_back(entry, L"<entry point>");

	// stable: exports win over import slots, which win over the entry point marker
	std::stable_sort(m_NamedRvas.begin(), m_NamedRvas.end(), [](auto& a, auto& b) { return a.first < b.first; });
}

std::wstring CMainFrame::ResolveRva(DWORD rva) const {
	if (rva == 0)
		return {};

	if (m_Symbols) {
		long disp = 0;
		auto sym = m_Symbols.GetSymbolByRVA(rva, SymbolTag::Null, &disp);
		if (sym) {
			auto name = sym.Name();
			if (!name.empty()) {
				auto undecorated = PEStrings::UndecorateName(name.c_str());
				if (!undecorated.empty())
					name = std::move(undecorated);
				return disp > 0 ? std::format(L"{}+0x{:X}", name, disp) : name;
			}
		}
	}

	auto it = std::lower_bound(m_NamedRvas.begin(), m_NamedRvas.end(), rva, [](auto& p, DWORD v) { return p.first < v; });
	return it != m_NamedRvas.end() && it->first == rva ? it->second : std::wstring();
}

std::wstring CMainFrame::ResolveVa(ULONGLONG va) const {
	auto base = m_PE.GetImageBase();
	if (va < base || va - base > 0xFFFFFFFFULL)
		return {};
	return ResolveRva((DWORD)(va - base));
}

DiaSession const& CMainFrame::GetSymbols() const {
	return m_Symbols;
}

int CMainFrame::DirectoryIndexToIcon(int index) {
	static const int icons[] = {
		GetTreeIcon(IDI_EXPORTS),
		GetTreeIcon(IDI_IMPORTS),
		GetTreeIcon(IDI_RESOURCE),
		GetTreeIcon(IDI_EXCEPTION),
		GetTreeIcon(IDI_SECURITY),
		GetTreeIcon(IDI_RELOC),
		GetTreeIcon(IDI_DEBUG),
		-1,
		-1,
		GetTreeIcon(IDI_THREAD),
		-1,
		-1,
		-1,
		GetTreeIcon(IDI_DELAY_IMPORT),
		GetTreeIcon(IDI_COMPONENT),
		-1,
	};
	static_assert(_countof(icons) == 16);

	return icons[index] < 0 ? GetTreeIcon(IDI_DIR_OPEN) : icons[index];
}

int CMainFrame::ResourceTypeToIcon(WORD resType) {
	static const std::unordered_map<WORD, int> map{
		{ (WORD)(LONG_PTR)RT_BITMAP, GetTreeIcon(IDI_BITMAP) },
		{ (WORD)(LONG_PTR)RT_MANIFEST, GetTreeIcon(IDI_MANIFEST) },
		{ (WORD)(LONG_PTR)RT_CURSOR, GetTreeIcon(IDI_CURSOR) },
		{ (WORD)(LONG_PTR)RT_ANICURSOR, GetTreeIcon(IDI_CURSOR) },
		{ (WORD)(LONG_PTR)RT_GROUP_CURSOR, GetTreeIcon(IDI_CURSOR) },
		{ (WORD)(LONG_PTR)RT_ICON, GetTreeIcon(IDI_ICON) },
		{ (WORD)(LONG_PTR)RT_ANIICON, GetTreeIcon(IDI_ICON) },
		{ (WORD)(LONG_PTR)RT_GROUP_ICON, GetTreeIcon(IDI_ICON) },
		{ (WORD)(LONG_PTR)RT_FONT, GetTreeIcon(IDI_FONT) },
		{ (WORD)(LONG_PTR)RT_FONTDIR, GetTreeIcon(IDI_FONT) },
		{ (WORD)(LONG_PTR)RT_MESSAGETABLE, GetTreeIcon(IDI_MESSAGE) },
		{ (WORD)(LONG_PTR)RT_VERSION, GetTreeIcon(IDI_VERSION) },
		{ (WORD)(LONG_PTR)RT_STRING, GetTreeIcon(IDI_TEXT) },
		{ (WORD)(LONG_PTR)RT_ACCELERATOR, GetTreeIcon(IDI_KEYBOARD) },
		{ (WORD)(LONG_PTR)RT_DIALOG, GetTreeIcon(IDI_FORM) },
	};

	if (auto it = map.find(resType); it != map.end())
		return it->second;

	return GetTreeIcon(IDI_RESOURCE);
}

LRESULT CMainFrame::OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto hDrop = (HDROP)wParam;
	WCHAR path[MAX_PATH];
	if (::DragQueryFile(hDrop, 0, path, _countof(path)))
		OpenPE(path);
	::DragFinish(hDrop);
	return 0;
}

bool CMainFrame::OpenPE(PCWSTR path) {
	CWaitCursor wait;
	int bitness = (m_PE && m_PE.GetFileInfo()->IsPE64) * 2 + (m_PE && m_PE.GetFileInfo()->IsPE32);

	m_PE.Close();
	if (!m_PE.Open(path)) {
		AtlMessageBox(m_hWnd, L"Error parsing file", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}

	//
	// clear symbol cache if bitness of old PE and new PE differ
	//
	if (bitness == 1 && m_PE.GetFileInfo()->IsPE64 || bitness == 2 && m_PE.GetFileInfo()->IsPE32)
		m_SymbolsForModules.clear();

	m_Symbols.Close();
	BuildNamedRvas();
	m_Anomalies = FindAnomalies(m_PE);
	m_Overlay = FindOverlay(m_PE);
	m_Xrefs.Clear();
	ResetNavigation();
	StartSymbolLoad(path);	// the window is usable at once; symbols arrive later (WM_SYMBOLS_LOADED)
	m_Views.clear();
	m_Views2.clear();
	m_Tabs.RemoveAllPages();

	BuildTree(16);

	CString ftitle;
	ftitle.LoadString(IDR_MAINFRAME);
	if (SecurityHelper::IsRunningElevated())
		ftitle += L" (Administrator)";
	CString spath(path);
	CString title = spath.Mid(spath.ReverseFind(L'\\') + 1) + L" - " + ftitle;
	SetWindowText(title);
	m_RecentFiles.AddFile(path);
	AppSettings::Get().RecentFiles(m_RecentFiles.Files());
	UpdateRecentFilesMenu();

	ShowView(m_hRoot);
	UpdateUI();

	return true;
}

TreeItemType CMainFrame::TreeItemWithIndex(TreeItemType type, int64_t index) {
	return static_cast<TreeItemType>((int64_t)type + index);
}

void CMainFrame::BuildTree(int iconSize) {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Tree.SetItemHeight(iconSize + 2);

	if (BuildTreeImageList(iconSize)) {
		m_Tree.SetImageList(m_TreeImages);
		m_Tabs.SetImageList(m_TreeImages);
	}

	auto& path = m_PE.GetPath();

	auto root = InsertTreeItem(m_Tree, path.substr(path.rfind(L'\\') + 1).c_str(), 0, TreeItemType::Image);
	auto headers = InsertTreeItem(m_Tree, L"Headers", GetTreeIcon(IDI_HEADERS), TreeItemType::Headers, root);
	InsertTreeItem(m_Tree, L"DOS Header", GetTreeIcon(IDI_MSDOS), TreeItemType::DOSHeader, headers);
	InsertTreeItem(m_Tree, L"NT Header", GetTreeIcon(IDI_FILE_HEADER), TreeItemType::NTHeader, headers);
	if (m_PE.GetFileInfo()->HasRichHdr)
		InsertTreeItem(m_Tree, L"Rich Header", GetTreeIcon(IDI_RICH_HEADER), TreeItemType::RichHeader, headers);
	m_Tree.Expand(headers, TVE_EXPAND);

	auto sections = InsertTreeItem(m_Tree, L"Sections", GetTreeIcon(IDI_SECTIONS), TreeItemType::Sections, root);
	int i = 0;
	for (auto const& sec : *m_PE.GetSecHeaders()) {
		CString name = sec.SectionName.c_str();
		if (name.IsEmpty())
			name = CString((PCSTR)sec.SecHdr.Name, 8);
		InsertTreeItem(m_Tree, name, GetTreeIcon(IDI_SECTION), TreeItemWithIndex(TreeItemType::Section, (i + 1) << ItemShift), sections);
		i++;
	}
	if (m_PE.GetSecHeaders()->size() < 15)
		m_Tree.Expand(sections, TVE_EXPAND);

	auto directories = InsertTreeItem(m_Tree, L"Data Directories", GetTreeIcon(IDI_DIRS), TreeItemType::Directories, root);
	i = 0;
	for (auto const& dir : *m_PE.GetDataDirs()) {
		if (dir.DataDir.Size || (i == IMAGE_DIRECTORY_ENTRY_GLOBALPTR && dir.DataDir.VirtualAddress)) {
			InsertTreeItem(m_Tree, PEStrings::GetDataDirectoryName(i), DirectoryIndexToIcon(i),
				TreeItemWithIndex(TreeItemType::Directory, i), directories);
		}
		i++;
	}
	m_Tree.Expand(directories, TVE_EXPAND);

	InsertTreeItem(m_Tree, std::format(L"Anomalies ({})", m_Anomalies.size()).c_str(), GetTreeIcon(IDI_EXCEPTION), TreeItemType::Anomalies, root);
	m_hOverlay = nullptr;
	if (!m_Overlay.Empty())
		m_hOverlay = InsertTreeItem(m_Tree, L"Overlay", GetTreeIcon(IDI_BINARY), TreeItemType::Overlay, root);

	if (m_PE.GetFileInfo()->HasResource) {
		auto resources = InsertTreeItem(m_Tree, L"Resources", GetTreeIcon(IDI_RESOURCE), TreeItemType::Resources, root);
		std::unordered_map<std::wstring, HTREEITEM> typeItems;
		auto const& fresources = m_PE.GetFlatResources();
		i = 0;
		m_hResVersion = m_hResManifest = nullptr;
		m_FlatResources.clear();
		m_FlatResources.reserve(fresources.size());
		for (auto const& res : fresources) {
			std::wstring type = res.TypeStr.empty() ? PEStrings::ResourceTypeToString(res.TypeID) : std::wstring(res.TypeStr);
			if (type.empty())
				type = std::format(L"#{}", res.TypeID);
			auto it = typeItems.find(type);
			if (it == typeItems.end()) {
				auto hItem = InsertTreeItem(m_Tree, type.c_str(), ResourceTypeToIcon(res.TypeID),
					TreeItemWithIndex(TreeItemType::ResourceTypeName, (i + 1) << ItemShift), resources, TVI_SORT);
				it = typeItems.insert({ type, hItem }).first;
			}
			auto name = res.NameID == 0 ? res.NameStr.data() : (L"#" + std::to_wstring(res.NameID));
			auto lang = res.LangStr.empty() ? PEStrings::LanguageToString(res.LangID) : std::wstring(res.LangStr);
			FlatResource fres(res);
			auto hResItem = InsertTreeItem(m_Tree, std::format(L"{} ({})", name, lang).c_str(), ResourceTypeToIcon(res.TypeID),
				TreeItemWithIndex(TreeItemType::Resource, (i + 1) << ItemShift), it->second, TVI_SORT);

			auto resTypeId = MAKEINTRESOURCE(res.TypeID);
			if (resTypeId == RT_MANIFEST && m_hResManifest == nullptr)
				m_hResManifest = hResItem;
			else if (resTypeId == RT_VERSION && m_hResVersion == nullptr)
				m_hResVersion = hResItem;

			fres.Name = std::move(name);
			fres.Type = std::move(type);
			fres.Language = std::move(lang);
			m_FlatResources.emplace_back(std::move(fres));
			i++;
		}
		m_Tree.Expand(resources, TVE_EXPAND);
	}

	if (m_Symbols) {
		m_Symbols.LoadAddress(m_PE.GetImageBase());
		//auto symbols = InsertTreeItem(m_Tree, L"Symbols", GetTreeIcon(IDI_SYMBOLS), TreeItemType::Symbols, root);
		//InsertTreeItem(m_Tree, L"Functions", GetTreeIcon(IDI_FUNCTION), TreeItemType::SymbolsFunctions, symbols, TVI_SORT);
		//InsertTreeItem(m_Tree, L"Global Data", GetTreeIcon(IDI_DATA), TreeItemType::SymbolsGlobalData, symbols, TVI_SORT);
		//InsertTreeItem(m_Tree, L"Types", GetTreeIcon(IDI_TYPE), TreeItemType::SymbolsTypes, symbols, TVI_SORT);
		//InsertTreeItem(m_Tree, L"Enums", GetTreeIcon(IDI_ENUM), TreeItemType::SymbolsEnums, symbols, TVI_SORT);
	}

	m_hRoot = root;

	m_Tree.Expand(root, TVE_EXPAND);
	m_Tree.SelectItem(root);
	m_Tree.SetRedraw();
	m_Tree.SetFocus();
}

HWND CMainFrame::GetHwnd() const {
	return m_hWnd;
}

BOOL CMainFrame::ShowContextMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd) {
	InitMenu(hMenu);
	return TrackPopupMenu(hMenu, flags, x, y, 0, hWnd ? hWnd : m_hWnd, nullptr);
}

CUpdateUIBase& CMainFrame::GetUI() {
	return *this;
}

LRESULT CMainFrame::OnFileClose(WORD, WORD, HWND, BOOL&) {
	m_Tabs.RemoveAllPages();
	m_Views.clear();
	m_Views2.clear();
	m_PE.Close();
	++m_SymbolGeneration;	// a symbol load in progress belongs to the file that was just closed
	m_Symbols.Close();
	m_Anomalies.clear();
	m_Overlay = {};
	m_hOverlay = nullptr;
	m_Xrefs.Clear();
	ResetNavigation();
	m_Tree.DeleteAllItems();
	UpdateUI();
	CString ftitle;
	ftitle.LoadString(IDR_MAINFRAME);
	if (SecurityHelper::IsRunningElevated())
		ftitle += L" (Administrator)";
	SetWindowText(ftitle);

	return 0;
}

int CMainFrame::GetDataDirectoryIconIndex(int index) const {
	return DirectoryIndexToIcon(index);
}

LRESULT CMainFrame::OnFileOpen(WORD, WORD, HWND, BOOL&) {
	auto path = DoFileOpen();
	if (path.IsEmpty())
		return 0;

	OpenPE(path);
	return 0;
}

LRESULT CMainFrame::OnFileOpenNewWindow(WORD, WORD, HWND, BOOL&) {
	auto file = DoFileOpen();
	if (!file.IsEmpty()) {
		auto frame = new CMainFrame;
		frame->CreateEx();
		frame->ShowWindow(SW_SHOWDEFAULT);
		if (frame->OpenPE(file))
			frame->ShowWindow(SW_SHOWDEFAULT);
		else
			frame->SendMessage(WM_CLOSE);
	}
	return LRESULT();
}

CString CMainFrame::DoFileOpen() const {
	CSimpleFileDialog dlg(TRUE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING,
		L"All PE Files\0*.exe;*.dll;*.efi;*.ocx;*.cpl;*.sys;*.mui;*.mun;*.scr\0All Files\0*.*\0");
	WTLHelper::SuspendHook();
	auto path = IDOK == dlg.DoModal() ? dlg.m_szFileName : L"";
	WTLHelper::ResumeHook();
	return path;
}

int CMainFrame::GetIconIndex(UINT icon) const {
	return s_ImageIndices.at(icon);
}

bool CMainFrame::BuildTreeImageList(int iconSize) {
	//
	// return true if image list existed already
	//
	bool exist = m_TreeImages != nullptr;

	if(!exist)
		m_TreeImages.Create(iconSize, iconSize, ILC_COLOR32, 32, 16);

	WORD icon = 0;
	CString path = m_PE.GetPath().c_str();
	auto hIcon = ::ExtractAssociatedIcon(_Module.GetModuleInstance(), path.GetBuffer(), &icon);
	if (!hIcon)
		hIcon = AtlLoadSysIcon(IDI_APPLICATION);
	if(!exist)
		m_TreeImages.AddIcon(hIcon);
	else {
		m_TreeImages.ReplaceIcon(0, hIcon);
		return false;
	}

	UINT const icons[] = {
		IDI_SECTIONS, IDI_DIR_OPEN, IDI_RESOURCE, IDI_HEADERS, IDI_DIRS, IDI_SECURITY,
		IDI_SECTION, IDI_EXPORTS, IDI_IMPORTS, IDI_DEBUG, IDI_FONT, IDI_ICON, IDI_CURSOR,
		IDI_MANIFEST, IDI_VERSION, IDI_TYPE, IDI_BITMAP, IDI_MESSAGE, IDI_TEXT,
		IDI_KEYBOARD, IDI_FORM, IDI_EXCEPTION, IDI_DELAY_IMPORT, IDI_RELOC,
		IDI_THREAD, IDI_RICH_HEADER, IDI_MSDOS, IDI_FILE_HEADER, IDI_COMPONENT,
		IDI_FUNCTION, IDI_FUNC_FORWARD, IDI_INTERFACE, IDI_DLL_IMPORT, IDI_FUNCTION2, IDI_SYMBOLS,
		IDI_TYPE, IDI_ENUM, IDI_BITFIELD, IDI_FIELD, IDI_ARRAY, IDI_UNION, IDI_BINARY,
		IDI_DATA,
	};

	bool insert = s_ImageIndices.empty();
	for (auto icon : icons) {
		int image = m_TreeImages.AddIcon(AtlLoadIconImage(icon, 0, iconSize, iconSize));
		if (insert)
			s_ImageIndices.insert({ icon, image });
	}
	return true;
}

void CMainFrame::SetStatusText(int index, PCWSTR text) {
	m_StatusBar.SetText(index, text);
}

LRESULT CMainFrame::OnEditFind(WORD, WORD, HWND, BOOL&) {
	ATLASSERT(m_Tabs.GetPageCount() > 0);

	if (m_pFindDlg == nullptr) {
		m_pFindDlg = new CFindReplaceDialog;
		m_pFindDlg->Create(TRUE, m_SearchText, nullptr, FR_DOWN | FR_HIDEWHOLEWORD, m_hWnd);
		m_pFindDlg->ShowWindow(SW_SHOW);
	}

	return 0;
}

LRESULT CMainFrame::OnFind(UINT msg, WPARAM wp, LPARAM lp, BOOL&) {
	if (m_pFindDlg->IsTerminating()) {
		m_pFindDlg = nullptr;
		return 0;
	}
	m_pFindDlg->SetFocus();

	if (auto page = m_Tabs.GetActivePage(); page >= 0) {
		m_SearchText = m_pFindDlg->GetFindString();
		::SendMessage(m_Tabs.GetPageHWND(page), msg, wp, lp);
	}
	return 0;
}

void CMainFrame::UpdateRecentFilesMenu() {
	if (m_RecentFiles.IsEmpty())
		return;

	auto menu = ((CMenuHandle)GetMenu()).GetSubMenu(0);
	CString text;
	int i = 0;
	for (; ; i++) {
		menu.GetMenuString(i, text, MF_BYPOSITION);
		if (text == L"&Recent Files")
			break;
	}
	menu = menu.GetSubMenu(i);
	while (menu.DeleteMenu(0, MF_BYPOSITION))
		;

	i = 0;
	for (auto& file : m_RecentFiles.Files()) {
		menu.AppendMenu(MF_BYPOSITION, ATL_IDS_MRU_FILE + i++, file.c_str());
	}
}

std::vector<FlatResource> const& CMainFrame::GetFlatResources() const {
	return m_FlatResources;
}

LRESULT CMainFrame::OnRecentFile(WORD, WORD id, HWND, BOOL&) {
	auto& path = m_RecentFiles.Files()[id - ATL_IDS_MRU_FILE];
	if (path != m_PE.GetPath())
		OpenPE(path.c_str());
	return 0;
}

void CMainFrame::ParseCommandLine() {
	int count;
	auto args = ::CommandLineToArgvW(::GetCommandLine(), &count);
	if (args && count > 1) {
		OpenPE(args[1]);
	}
	if (args)
		::LocalFree(args);
}

void CMainFrame::CloseTab(int page) {
	auto hWnd = m_Tabs.GetPageHWND(page);
	auto type = m_Views2.at(hWnd);
	m_Views.erase(type);
	m_Views2.erase(hWnd);
}

void CMainFrame::RegisterContextMenu(TreeItemType type, UINT menuId, int subMenu) {
	m_ContextMenus.try_emplace(type, menuId, subMenu);
}

LRESULT CMainFrame::OnViewExports(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::DirectoryExports, nullptr, IDI_EXPORTS);
	return 0;
}

LRESULT CMainFrame::OnViewImports(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::DirectoryImports, nullptr, IDI_IMPORTS);
	return 0;
}

LRESULT CMainFrame::OnViewVersion(WORD, WORD, HWND, BOOL&) {
	ShowView(m_hResVersion);
	return 0;
}

LRESULT CMainFrame::OnViewOverlay(WORD, WORD, HWND, BOOL&) {
	if (m_hOverlay)
		ShowView(m_hOverlay);
	return 0;
}

LRESULT CMainFrame::OnViewManifest(WORD, WORD, HWND, BOOL&) {
	ShowView(m_hResManifest);
	return 0;
}

LRESULT CMainFrame::OnViewResources(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::DirectoryResources, nullptr, IDI_RESOURCE);
	return 0;
}

LRESULT CMainFrame::OnViewSections(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::Sections, nullptr, IDI_SECTIONS);
	return 0;
}

LRESULT CMainFrame::OnViewDebug(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::DirectoryDebug, nullptr, IDI_DEBUG);
	return 0;
}

LRESULT CMainFrame::OnViewSecurity(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::DirectorySecurity, nullptr, IDI_SECURITY);
	return 0;
}

LRESULT CMainFrame::OnViewDataDirs(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::Directories, nullptr, IDI_DIRS);
	return 0;
}

LRESULT CMainFrame::OnShowWindow(UINT, WPARAM show, LPARAM, BOOL&) {
	bool static shown = false;
	if (show && !shown) {
		shown = true;
		auto wp = AppSettings::Get().MainWindowPlacement();
		if (wp.showCmd != SW_HIDE) {
			SetWindowPlacement(&wp);
			UpdateLayout();
		}
		if (AppSettings::Get().AlwaysOnTop())
			SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	return 0;
}

void CMainFrame::SetAlwaysOnTop(bool onTop) {
	SetWindowPos(onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	UISetCheck(ID_OPTIONS_ALWAYSONTOP, onTop);
}

LRESULT CMainFrame::OnMenuSelect(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) const {
	return 0;
}

LRESULT CMainFrame::OnDisassembleEntryPoint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!ShowView(TreeItemType::AsmEntryPoint, nullptr, IDI_BINARY))
		AtlMessageBox(m_hWnd, L"No entry point defined for this PE", IDR_MAINFRAME, MB_ICONWARNING);
	return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto onTop = !AppSettings::Get().AlwaysOnTop();
	AppSettings::Get().AlwaysOnTop(onTop);
	SetAlwaysOnTop(onTop);

	return 0;
}

void CMainFrame::InitDarkTheme() const {
	s_DarkTheme.BackColor = s_DarkTheme.SysColors[COLOR_WINDOW] = RGB(32, 32, 32);
	s_DarkTheme.TextColor = s_DarkTheme.SysColors[COLOR_WINDOWTEXT] = RGB(248, 248, 248);
	s_DarkTheme.SysColors[COLOR_HIGHLIGHT] = RGB(10, 10, 160);
	s_DarkTheme.SysColors[COLOR_HIGHLIGHTTEXT] = RGB(240, 240, 240);
	s_DarkTheme.SysColors[COLOR_MENUTEXT] = s_DarkTheme.TextColor;
	s_DarkTheme.SysColors[COLOR_CAPTIONTEXT] = s_DarkTheme.TextColor;
	s_DarkTheme.SysColors[COLOR_BTNFACE] = s_DarkTheme.BackColor;
	s_DarkTheme.SysColors[COLOR_BTNTEXT] = s_DarkTheme.TextColor;
	s_DarkTheme.SysColors[COLOR_3DLIGHT] = RGB(192, 192, 192);
	s_DarkTheme.SysColors[COLOR_BTNHIGHLIGHT] = RGB(192, 192, 192);
	s_DarkTheme.SysColors[COLOR_CAPTIONTEXT] = s_DarkTheme.TextColor;
	s_DarkTheme.SysColors[COLOR_3DSHADOW] = s_DarkTheme.TextColor;
	s_DarkTheme.SysColors[COLOR_SCROLLBAR] = s_DarkTheme.BackColor;
	s_DarkTheme.SysColors[COLOR_APPWORKSPACE] = s_DarkTheme.BackColor;
	s_DarkTheme.StatusBar.BackColor = RGB(16, 0, 16);
	s_DarkTheme.StatusBar.TextColor = s_DarkTheme.TextColor;

	s_DarkTheme.Name = L"Dark";
	s_DarkTheme.Menu.BackColor = s_DarkTheme.BackColor;
	s_DarkTheme.Menu.TextColor = s_DarkTheme.TextColor;
}

LRESULT CMainFrame::OnUpdateDarkMode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();

	WTLHelper::SwitchToMode(settings.DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd);
	UISetCheck(ID_OPTIONS_DARKMODE, settings.DarkMode());
	InitMenu(GetMenu());
	DrawMenuBar();
	SendMessageToDescendants(WM_UPDATE_DARKMODE);
	SendMessageToDescendants(::RegisterWindowMessage(L"WTLHelperUpdateTheme"));

	return 0;
}

LRESULT CMainFrame::OnToggleDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	settings.DarkMode(!settings.DarkMode());
	UISetCheck(ID_OPTIONS_DARKMODE, settings.DarkMode());
	::EnumThreadWindows(::GetCurrentThreadId(), [](auto hWnd, auto) {
		::PostMessage(hWnd, WM_UPDATE_DARKMODE, 0, 0);
		return TRUE;
		}, 0);

	return 0;
}

LRESULT CMainFrame::OnSymbolSettings(WORD, WORD, HWND, BOOL&) {
	CSymbolSettingsDlg dlg;
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;

	// symbols already loaded (including those for other modules) used the old path
	m_SymbolsForModules.clear();
	if (m_PE && AtlMessageBox(m_hWnd, L"Reload the current file to apply the new symbol settings?",
		IDR_MAINFRAME, MB_ICONQUESTION | MB_YESNO) == IDYES) {
		std::wstring path(m_PE.GetPath());	// OpenPE closes the file, so work on a copy
		OpenPE(path.c_str());
	}
	return 0;
}

LRESULT CMainFrame::OnPageActivated(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	RecordNavigation();
	return 0;
}

//
// symbols in the background
//

void CMainFrame::StartSymbolLoad(std::wstring path) {
	auto generation = ++m_SymbolGeneration;
	auto searchPath = AppSettings::Get().BuildSymbolSearchPath();	// settings are only read on the UI thread
	SetStatusText(0, L"Loading symbols...");

	std::thread([hWnd = m_hWnd, generation, path = std::move(path), searchPath = std::move(searchPath)] {
		::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		auto session = std::make_unique<DiaSession>();
		session->SetSymbolPath(searchPath.c_str());
		bool ok = session->OpenImage(path.c_str());
		// ownership of the session moves to the UI thread, unless the window is gone
		if (!::PostMessage(hWnd, WM_SYMBOLS_LOADED, generation, ok ? (LPARAM)session.get() : 0))
			return;
		if (ok)
			session.release();
	}).detach();
}

LRESULT CMainFrame::OnSymbolsLoaded(UINT, WPARAM generation, LPARAM lParam, BOOL&) {
	std::unique_ptr<DiaSession> session((DiaSession*)lParam);
	if ((uint32_t)generation != m_SymbolGeneration || !m_PE)
		return 0;	// the file was closed or another one opened in the meantime

	if (!session) {
		SetStatusText(0, L"No symbols");
		return 0;
	}
	m_Symbols = *session;
	m_Symbols.LoadAddress(m_PE.GetImageBase());
	SetStatusText(0, L"Symbols loaded");
	RefreshViews();
	return 0;
}

// Views that were opened before the symbols arrived show addresses without names. Create them again.
void CMainFrame::RefreshViews() {
	struct Entry {
		TreeItemType Type;
		HTREEITEM Item;
		int Page;
		bool Active;
	};
	std::vector<Entry> entries;
	int active = m_Tabs.GetActivePage();
	for (int i = 0; i < m_Tabs.GetPageCount(); i++) {
		auto it = m_Views2.find(m_Tabs.GetPageHWND(i));
		if (it == m_Views2.end())
			continue;
		auto view = m_Views.find(it->second);
		// views that were created on request (disassembly, the file in hex) keep what they show
		if (view == m_Views.end() || view->second->DeleteFromTree())
			continue;
		entries.push_back({ it->second, view->second->GetHTreeItem(), i, i == active });
	}
	if (entries.empty())
		return;

	m_Navigating = true;	// not a navigation the user made
	auto selected = m_Tree.GetSelectedItem();
	m_Tree.SetRedraw(FALSE);
	for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
		CloseTab(it->Page);
		m_Tabs.RemovePage(it->Page);
	}
	for (auto& e : entries)
		ShowView(e.Type, e.Item);
	for (auto& e : entries)
		if (e.Active)
			ShowView(e.Type, e.Item);
	if (selected)
		m_Tree.SelectItem(selected);
	m_Tree.SetRedraw(TRUE);
	m_Navigating = false;

	ResetNavigation();
	RecordNavigation();
}

//
// Go To, Back and Forward
//

void CMainFrame::ResetNavigation() {
	m_History.clear();
	m_HistoryIndex = -1;
	UpdateNavigationUI();
}

void CMainFrame::UpdateNavigationUI() {
	UIEnable(ID_NAV_GOTO, m_PE ? TRUE : FALSE);
	UIEnable(ID_NAV_BACK, m_HistoryIndex > 0);
	UIEnable(ID_NAV_FORWARD, m_HistoryIndex + 1 < (int)m_History.size());
}

// Remembers the page and position the user is at, unless that is already the current entry.
void CMainFrame::RecordNavigation() {
	if (m_Navigating)
		return;
	int page = m_Tabs.GetActivePage();
	if (page < 0)
		return;

	auto hWnd = m_Tabs.GetPageHWND(page);
	int64_t position = -1;
	if (auto it = m_Views2.find(hWnd); it != m_Views2.end())
		if (auto view = m_Views.find(it->second); view != m_Views.end())
			position = view->second->GetNavigationPosition();

	if (m_HistoryIndex >= 0 && m_HistoryIndex < (int)m_History.size()) {
		auto const& current = m_History[m_HistoryIndex];
		if (current.Page == hWnd && current.Position == position)
			return;
	}
	m_History.resize(m_HistoryIndex + 1);	// going somewhere new discards what was "forward"
	m_History.push_back({ hWnd, position });
	if (m_History.size() > 100)
		m_History.erase(m_History.begin());
	m_HistoryIndex = (int)m_History.size() - 1;
	UpdateNavigationUI();
}

bool CMainFrame::NavigateHistory(int delta) {
	int index = m_HistoryIndex + delta;
	while (index >= 0 && index < (int)m_History.size()) {
		auto entry = m_History[index];
		int page = -1;
		for (int i = 0; i < m_Tabs.GetPageCount(); i++)
			if (m_Tabs.GetPageHWND(i) == entry.Page)
				page = i;

		if (page >= 0) {
			m_Navigating = true;
			m_Tabs.SetActivePage(page);
			if (entry.Position >= 0)
				if (auto it = m_Views2.find(entry.Page); it != m_Views2.end())
					if (auto view = m_Views.find(it->second); view != m_Views.end())
						view->second->SetNavigationPosition(entry.Position);
			m_Navigating = false;
			m_HistoryIndex = index;
			UpdateNavigationUI();
			return true;
		}
		// the tab was closed: forget the entry and try the next one in the same direction
		m_History.erase(m_History.begin() + index);
		if (delta < 0) {
			index--;
			m_HistoryIndex--;
		}
	}
	UpdateNavigationUI();
	return false;
}

LRESULT CMainFrame::OnNavBack(WORD, WORD, HWND, BOOL&) {
	NavigateHistory(-1);
	return 0;
}

LRESULT CMainFrame::OnNavForward(WORD, WORD, HWND, BOOL&) {
	NavigateHistory(1);
	return 0;
}

bool CMainFrame::ResolveGoTo(GoToOptions const& options, int64_t& offset, std::wstring& error) const {
	if (!m_PE) {
		error = L"No file is open.";
		return false;
	}
	const uint64_t fileSize = m_PE.GetFileSize();
	if (options.Kind == GoToKind::FileOffset) {
		if (options.Value >= fileSize) {
			error = std::format(L"The offset 0x{:X} is beyond the end of the file (0x{:X} bytes).", options.Value, fileSize);
			return false;
		}
		offset = (int64_t)options.Value;
		return true;
	}

	uint64_t rva = options.Value;
	if (options.Kind == GoToKind::Va) {
		auto base = m_PE.GetImageBase();
		if (options.Value < base || options.Value - base > 0xFFFFFFFFULL) {
			error = std::format(L"The address 0x{:X} is outside the image (which starts at 0x{:X}).", options.Value, base);
			return false;
		}
		rva = options.Value - base;
	}
	else if (rva > 0xFFFFFFFFULL) {
		error = L"An RVA is at most 32 bits.";
		return false;
	}

	auto const& nt = *m_PE.GetNTHeader();
	auto sizeOfHeaders = m_PE.GetFileInfo()->IsPE64 ? nt.NTHdr64.OptionalHeader.SizeOfHeaders : nt.NTHdr32.OptionalHeader.SizeOfHeaders;
	if (rva < sizeOfHeaders && rva < fileSize) {
		offset = (int64_t)rva;	// the headers are mapped one to one
		return true;
	}
	auto mapped = m_PE.GetOffsetFromRVA(rva);
	if (mapped == 0 || mapped >= fileSize) {
		error = std::format(L"The RVA 0x{:X} is not backed by data in the file: it is outside the image or in uninitialized data.", rva);
		return false;
	}
	offset = (int64_t)mapped;
	return true;
}

LRESULT CMainFrame::OnNavGoTo(WORD, WORD, HWND, BOOL&) {
	if (!m_PE)
		return 0;
	CGoToDlg dlg(m_GoTo);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;

	int64_t offset;
	std::wstring error;
	if (!ResolveGoTo(m_GoTo, offset, error)) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	GoToFileOffset(offset, m_GoTo.Disassemble);
	return 0;
}

bool CMainFrame::GoToFileOffset(int64_t offset, bool disassemble) {
	if (!m_PE || offset < 0 || offset >= (int64_t)m_PE.GetFileSize())
		return false;

	RecordNavigation();	// so that Back returns to where the user was
	if (disassemble) {
		PESectionHeader const* host = nullptr;
		for (auto const& s : *m_PE.GetSecHeaders()) {
			auto& h = s.SecHdr;
			if (offset >= h.PointerToRawData && offset < (int64_t)h.PointerToRawData + h.SizeOfRawData) {
				host = &s;
				break;
			}
		}
		if (host && (host->SecHdr.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))) {
			auto& h = host->SecHdr;
			auto rva = (uint32_t)(h.VirtualAddress + (offset - h.PointerToRawData));
			auto va = m_PE.GetImageBase() + rva;
			auto size = (uint32_t)std::min<int64_t>(0x2000, (int64_t)h.PointerToRawData + h.SizeOfRawData - offset);
			auto title = std::format(L"Code at 0x{:X}", va);
			if (CreateAssemblyView(m_PE.GetSpan((uint32_t)offset, size), va, rva, title.c_str(), TreeItemType::Image)) {
				RecordNavigation();
				return true;
			}
		}
		else {
			AtlMessageBox(m_hWnd, L"That address is not in a code section. It is shown in the hex view instead.", IDR_MAINFRAME, MB_ICONINFORMATION);
		}
	}

	if (!ShowView(TreeItemType::FileInHex, nullptr, IDI_BINARY))
		return false;
	if (auto it = m_Views.find(TreeItemType::FileInHex); it != m_Views.end())
		it->second->SetNavigationPosition(offset);
	RecordNavigation();
	return true;
}

XrefMap const& CMainFrame::GetXrefs() {
	if (m_PE && !m_Xrefs.Built()) {
		CWaitCursor wait;
		m_Xrefs.Build(m_PE);
	}
	return m_Xrefs;
}

bool CMainFrame::GoToVa(uint64_t va) {
	if (!m_PE)
		return false;

	// a disassembly that already shows the address
	RecordNavigation();
	for (auto const& [type, view] : m_Views)
		if (view->GoToAddress(va)) {
			ShowView(type, view->GetHTreeItem());
			RecordNavigation();
			return true;
		}

	GoToOptions options;
	options.Kind = GoToKind::Va;
	options.Value = va;
	int64_t offset;
	std::wstring error;
	if (!ResolveGoTo(options, offset, error)) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}

	bool code = false;
	auto rva = (uint32_t)(va - m_PE.GetImageBase());
	for (auto const& s : *m_PE.GetSecHeaders()) {
		auto& h = s.SecHdr;
		if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData))
			code = (h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) != 0;
	}
	return GoToFileOffset(offset, code);
}

std::vector<Anomaly> const& CMainFrame::GetAnomalies() const {
	return m_Anomalies;
}

LRESULT CMainFrame::OnViewFileInHex(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ShowView(TreeItemType::FileInHex, nullptr, IDI_BINARY);
	return 0;
}

LRESULT CMainFrame::OnPageCloseButton(int, LPNMHDR hdr, BOOL&) {
	CloseTab((int)hdr->idFrom);

	return 0;
}

LRESULT CMainFrame::OnWindowsProperties(WORD, WORD, HWND, BOOL&) {
	std::thread([this]() { ::ShellAbout(m_hWnd, L"Windows", nullptr, nullptr); }).detach();

	return 0;
}
