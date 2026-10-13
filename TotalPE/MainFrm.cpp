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
#include "ApiSetMap.h"
#include "AppSettings.h"
#include "SymbolSettingsDlg.h"
#include "GoToDlg.h"
#include "SymbolsView.h"
#include "VtKeyDlg.h"
#include "SaveData.h"
#include "ScintillaView.h"
#include "SideBySideView.h"
#include "DecompilerThread.h"
#include "HexView.h"
#include "CompareView.h"
#include "ItaniumDemangle.h"
#include <DbgHelp.h>
#include <thread>
#include <WTLHelper.h>

const int WindowMenuPosition = 5;

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (m_pFindDlg && m_pFindDlg->IsDialogMessageW(pMsg))
		return TRUE;
	if (m_SearchDlg.IsWindow() && m_SearchDlg.IsDialogMessageW(pMsg))
		return TRUE;

	// the docking area's shortcuts (Ctrl+Tab, Ctrl+F4, Ctrl+F6...) before the accelerators
	if (m_Tabs.PreTranslateMessage(pMsg))
		return TRUE;

	return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UpdateSaveUI();
	UpdateDockUI();
	UIUpdateToolBar();
	return FALSE;
}

void CMainFrame::OnFinalMessage(HWND) {
	delete this;
}

bool CMainFrame::OnTreeDoubleClick(HWND, HTREEITEM hItem) {
	return ShowView(hItem);
}

// The menu of a flow graph is also that of its tree item: the item is selected, which shows the graph that the commands apply to
bool CMainFrame::OnTreeRightClick(HWND, HTREEITEM hItem, POINT const& pt) {
	auto type = (TreeItemType)m_Tree.GetItemData(hItem);
	auto kind = type & TreeItemType::ItemMask;
	// a section or a resource can be saved to a file, and a resource be shown in hex (whatever view it has)
	if (kind == TreeItemType::Section || kind == TreeItemType::Resource) {
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_DATA_SAVE, L"&Save Data...");
		if (kind == TreeItemType::Resource)
			menu.AppendMenu(MF_STRING, ID_VIEW_RESOURCEHEX, L"Open as &Hex");
		switch (ShowContextMenu(menu, TPM_RETURNCMD, pt.x, pt.y, m_hWnd)) {
			case ID_DATA_SAVE:
				SaveTreeItemData(type);
				break;
			case ID_VIEW_RESOURCEHEX:
				ShowResourceHex((int)(((int64_t)type >> ItemShift) - 1));
				break;
		}
		return true;
	}
	if (kind == TreeItemType::ElfSection && m_Elf) {
		auto index = (int)(((int64_t)type >> ItemShift) - 1);
		bool code = ArchOfElfMachine(m_Elf.Machine()).has_value();
		if (index <= 0 || index >= (int)m_Elf.Sections().size() || !(m_Elf.Sections()[index].Flags & 4) || !code)
			return false;
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_VIEW_DISASSEMBLE, L"&Disassemble");
		if (ShowContextMenu(menu, TPM_RETURNCMD, pt.x, pt.y, m_hWnd) == ID_VIEW_DISASSEMBLE)
			ShowElfSection(index, 0, true);
		return true;
	}
	if (kind != TreeItemType::FlowGraph)
		return false;
	ShowView(hItem);
	UIEnable(ID_ICON_EXPORT, TRUE);
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return ShowContextMenu(menu.GetSubMenu(9), 0, pt.x, pt.y, m_hWnd);
}

void CMainFrame::UpdateUI() {
	auto const fi = m_PE ? m_PE.GetFileInfo() : nullptr;
	UIEnable(ID_PE_DISASSEMBLEENTRYPOINT, fi != nullptr || (m_Elf && m_Elf.Entry()));
	UIEnable(ID_PE_DISASSEMBLEFUNCTION, fi != nullptr || m_Elf);
	UIEnable(ID_PE_DECOMPILEFUNCTION, fi != nullptr);
	UIEnable(ID_PE_SIDEBYSIDE, fi != nullptr);
	UIEnable(ID_VIEW_EXPORTS, fi && fi->HasExport);
	UIEnable(ID_VIEW_IMPORTS, fi && fi->HasImport);
	UIEnable(ID_VIEW_RESOURCES, fi && fi->HasResource);
	UIEnable(ID_VIEW_DEBUG, fi && fi->HasDebug);
	UIEnable(ID_VIEW_DIRECTORIES, fi && fi->HasDataDirs);
	UIEnable(ID_VIEW_SECTIONS, fi && fi->HasSections);
	UIEnable(ID_PE_SECURITY, fi && fi->HasSecurity);
	UIEnable(ID_FILE_CLOSE, fi != nullptr || m_Archive || m_Object || m_Elf);
	m_SaveUIPage = (HWND)-1;	// Save and Export List: from the active view, on the next idle
	UIEnable(ID_PE_STRINGS, fi != nullptr || m_Elf);
	UIEnable(ID_PE_FUNCTIONS, fi && ArchOf(m_PE));
	UIEnable(ID_PE_CLRTYPES, fi && FindTreeItem(TreeItemType::ClrTypes) != nullptr);
	UIEnable(ID_PE_SECURITYSUMMARY, fi != nullptr);
	UIEnable(ID_VIEW_MANIFEST, fi && m_hResManifest != nullptr);
	UIEnable(ID_VIEW_VERSION, fi && m_hResVersion != nullptr);
	UIEnable(ID_VIEW_OVERLAY, fi && m_hOverlay != nullptr);
	UIEnable(ID_PE_VIRUSTOTAL, fi && !m_Vt.Running());
	UIEnable(ID_FILE_OPENINANEWWINDOW, fi != nullptr || m_Archive || m_Object || m_Elf);
	UIEnable(ID_EDIT_COPY, FALSE);
	UIEnable(ID_EDIT_FIND, (fi || m_Archive || m_Object || m_Elf) && m_Tabs.GetActivePage() >= 0);
	UIEnable(ID_EDIT_SEARCHALL, fi || m_Archive || m_Object || m_Elf);
	UIEnable(ID_NAV_GOANYWHERE, fi || m_Archive || m_Object || m_Elf);
	UIEnable(ID_FILE_COMPARE, fi != nullptr);
	UIEnable(ID_PE_ENTIREFILEINHEX, fi != nullptr);
	UIEnable(ID_VIEW_RESOURCEHEX, FALSE);
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
		{ ID_EXPORT_FLOWGRAPH, IDI_FLOW },
		{ ID_ASSEMBLY_FLOWGRAPH, IDI_FLOW },
		{ ID_PE_STRINGS, IDI_TEXT },

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
	if (!m_Tree.DeleteItem(hItem))
		return false;
	// the node of disassembled functions goes away with its last function
	if (auto hFunctions = FindTreeItem(TreeItemType::DisassemblyFunctions); hFunctions && !m_Tree.GetChildItem(hFunctions))
		m_Tree.DeleteItem(hFunctions);
	return true;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	DragAcceptFiles();
	auto& settings = AppSettings::Get();
	m_RecentFiles.Set(settings.RecentFiles());
	UpdateRecentFilesMenu();

	CreateSimpleStatusBar();
	m_StatusBar.SubclassWindow(m_hWndStatusBar);
	int parts[] = { 200, 560, 760, 1160, -1 };	// (the last two: the work in the background)
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
		{ ID_PE_STRINGS, IDI_TEXT },
		{ 0 },
		{ ID_EDIT_FIND, IDI_FIND},
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	auto tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons));

	AddSimpleReBarBand(tb);
	UIAddToolBar(tb);

	// the tree and the views in a docking area. The tree is made a child of the frame: a tree view sends its notifications
	// (double click, right click...) to the window it was created in, whichever window of the docking area holds it later.
	m_hWndClient = m_Tabs.Create(m_hWnd);
	m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT);
	m_Tabs.SetExplorer(m_Tree, L"Explorer");
	m_Tabs.LoadLayout(LayoutPath(), ToolViewTitle);	// as the window was last arranged (the default if there is no file)

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
			// opened from a menu: the view belongs to the tree item of its type, which the views created under it need as their parent
			if (!hItem)
				hItem = FindTreeItem(data);
			view->SetHTreeItem(hItem);
			m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, map,
				IsToolView(data) ? CDockViews::Placement::Tool : CDockViews::Placement::Document, ToolViewId(data));
			m_Views.insert({ data, view });
			m_Views2.insert({ view->GetHwnd(), data });
			return true;
		}
	}
	return false;
}

HTREEITEM CMainFrame::FindTreeItem(TreeItemType type) const {
	for (auto h = m_Tree.GetRootItem(); h;) {
		if (m_Tree.GetItemData(h) == (DWORD_PTR)type)
			return h;
		if (auto child = m_Tree.GetChildItem(h))
			h = child;
		else {
			while (h && !m_Tree.GetNextSiblingItem(h))
				h = m_Tree.GetParentItem(h);
			if (h)
				h = m_Tree.GetNextSiblingItem(h);
		}
	}
	return nullptr;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	CloseDecompiler();
	CancelXrefs();
	m_Tabs.SaveLayout(LayoutPath());	// the docking area still has its windows (they go after the frame's WM_DESTROY)

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

bool CMainFrame::IsToolView(TreeItemType type) {
	switch (type & TreeItemType::ItemMask) {
		case TreeItemType::Symbols:
		case TreeItemType::SymbolsFunctions:
		case TreeItemType::SymbolsClasses:
		case TreeItemType::SymbolsStructs:
		case TreeItemType::SymbolsEnums:
		case TreeItemType::SymbolsTypes:
		case TreeItemType::SymbolsGlobalData:
		case TreeItemType::Xrefs:
			return true;
	}
	return false;
}

namespace {
	struct ToolKind {
		TreeItemType Type;
		PCWSTR Id;
		PCWSTR Title;
	};
	ToolKind const ToolKinds[] = {
		{ TreeItemType::Symbols, L"tool:symbols", L"Symbols" },
		{ TreeItemType::SymbolsClasses, L"tool:class", L"Class" },
		{ TreeItemType::SymbolsStructs, L"tool:struct", L"Struct" },
		{ TreeItemType::SymbolsFunctions, L"tool:function", L"Function" },
		{ TreeItemType::SymbolsEnums, L"tool:enum", L"Enum" },
		{ TreeItemType::SymbolsTypes, L"tool:types", L"Types" },
		{ TreeItemType::SymbolsGlobalData, L"tool:data", L"Data" },
	};
}

std::wstring CMainFrame::FunctionName(std::wstring const& name) {
	if (name.starts_with(L'?')) {
		WCHAR undecorated[1024];
		if (::UnDecorateSymbolNameW(name.c_str(), undecorated, _countof(undecorated), UNDNAME_NAME_ONLY))
			return undecorated;
		return name;
	}
	auto text = name;
	if (name.starts_with(L"_Z")) {
		std::string mangled;	// ASCII
		for (auto ch : name)
			mangled += (char)ch;
		auto demangled = DemangleItanium(mangled);
		if (!demangled.empty())
			text = std::wstring(demangled.begin(), demangled.end());
	}

	// "operator" at a position: the start of an operator's name ("operator()", "operator new", "operator int"...)
	auto isOperator = [&](size_t pos) {
		return text.compare(pos, 8, L"operator") == 0 && (pos == 0 || text[pos - 1] == L' ' || text[pos - 1] == L':')
			&& (pos + 8 == text.size() || !iswalnum(text[pos + 8]) && text[pos + 8] != L'_');
	};

	// the parameters: the first '(' outside template arguments and quoted names, but not the one of "operator()"
	int depth = 0;
	size_t end = std::wstring::npos;
	for (size_t i = 0; i < text.size() && end == std::wstring::npos; i++) {
		switch (text[i]) {
			case L'<': case L'`': depth++; break;
			case L'>': case L'\'': if (depth > 0) depth--; break;
			case L'(':
				if (depth == 0) {
					auto before = text.find_last_not_of(L' ', i == 0 ? 0 : i - 1);
					if (before != std::wstring::npos && before >= 7 && isOperator(before - 7) && text.compare(i, 2, L"()") == 0)
						i++;	// "operator()": its parameters follow
					else
						end = i;
				}
				break;
		}
	}
	if (end == std::wstring::npos)
		return text;	// no signature: a name as it is ("Code at 0x..." as well)
	text = text.substr(0, end);
	while (!text.empty() && text.back() == L' ')
		text.pop_back();

	// what comes before the name: the last space outside template arguments, unless the name is an operator's
	depth = 0;
	size_t start = 0, operatorAt = std::wstring::npos;
	for (size_t i = 0; i < text.size(); i++) {
		switch (text[i]) {
			case L'<': case L'`': depth++; break;
			case L'>': case L'\'': if (depth > 0) depth--; break;
			case L' ':
				if (depth == 0 && operatorAt == std::wstring::npos)
					start = i + 1;
				break;
			default:
				if (depth == 0 && operatorAt == std::wstring::npos && isOperator(i))
					operatorAt = i;
				break;
		}
	}
	auto result = text.substr(start);
	while (!result.empty() && (result.front() == L'*' || result.front() == L'&'))
		result.erase(0, 1);
	return result.empty() ? name : result;
}

std::wstring CMainFrame::ToolViewId(TreeItemType type) {
	for (auto& kind : ToolKinds)
		if (kind.Type == (type & TreeItemType::ItemMask))
			return kind.Id;
	return {};
}

std::wstring CMainFrame::ToolViewTitle(std::wstring const& id) {
	for (auto& kind : ToolKinds)
		if (id == kind.Id)
			return kind.Title;
	return id;
}

std::wstring CMainFrame::LayoutPath() {
	WCHAR folder[MAX_PATH];
	::ExpandEnvironmentStrings(L"%LOCALAPPDATA%\\TotalPE", folder, _countof(folder));
	::CreateDirectory(folder, nullptr);
	return std::wstring(folder) + L"\\Layout.json";
}

void CMainFrame::UpdateDockUI() {
	using WTLDock::DockCommand;
	int pages = m_Tabs.GetPageCount();
	UIEnable(ID_WINDOW_CLOSEOTHERS, pages > 1 && m_Tabs.GetActivePage() >= 0);
	UIEnable(ID_WINDOW_NEWHGROUP, m_Tabs.CanExecute(DockCommand::NewHorizontalGroup));
	UIEnable(ID_WINDOW_NEWVGROUP, m_Tabs.CanExecute(DockCommand::NewVerticalGroup));
	UIEnable(ID_WINDOW_NEXTGROUP, m_Tabs.CanExecute(DockCommand::MoveToNextGroup));
	UIEnable(ID_WINDOW_PREVGROUP, m_Tabs.CanExecute(DockCommand::MoveToPreviousGroup));
	UIEnable(ID_WINDOW_WINDOWS, pages > 0);
}

// All the views but the active one
LRESULT CMainFrame::OnWindowCloseOthers(WORD, WORD, HWND, BOOL&) {
	int active = m_Tabs.GetActivePage();
	if (active < 0) {
		::MessageBeep((UINT)-1);
		return 0;
	}
	for (int page = m_Tabs.GetPageCount() - 1; page >= 0; page--) {
		if (page != active) {
			CloseTab(page);
			m_Tabs.RemovePage(page);
		}
	}
	return 0;
}

LRESULT CMainFrame::OnWindowDockCommand(WORD, WORD id, HWND, BOOL&) {
	using WTLDock::DockCommand;
	auto command = id == ID_WINDOW_NEWHGROUP ? DockCommand::NewHorizontalGroup : id == ID_WINDOW_NEWVGROUP ? DockCommand::NewVerticalGroup
		: id == ID_WINDOW_NEXTGROUP ? DockCommand::MoveToNextGroup : DockCommand::MoveToPreviousGroup;
	if (!m_Tabs.Execute(command))
		::MessageBeep((UINT)-1);
	return 0;
}

LRESULT CMainFrame::OnWindowWindows(WORD, WORD, HWND, BOOL&) {
	m_Tabs.ShowWindowsDialog(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnWindowResetLayout(WORD, WORD, HWND, BOOL&) {
	m_Tabs.ResetLayout();
	return 0;
}

LRESULT CMainFrame::OnViewExplorer(WORD, WORD, HWND, BOOL&) {
	m_Tabs.ShowExplorer();
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
	if (m_Elf) {
		// the function or data that contains the address
		auto it = std::upper_bound(m_ElfNames.begin(), m_ElfNames.end(), va, [](ULONGLONG a, ElfName const& n) { return a < n.Address; });
		if (it == m_ElfNames.begin())
			return {};
		--it;
		if (it->Address == va)
			return it->Name;
		if (va - it->Address < it->Size)
			return std::format(L"{}+0x{:X}", it->Name, va - it->Address);
		return {};
	}
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

std::wstring CMainFrame::CurrentPath() const {
	return m_Archive ? m_Archive.Path() : m_Object ? m_Object.Path() : m_Elf ? m_Elf.Path() : m_PE.GetPath();
}

bool CMainFrame::OpenPE(PCWSTR path) {
	if (LibArchive::IsArchiveFile(path))
		return OpenArchive(path);
	if (CoffObject::IsObjectFile(path))
		return OpenObject(path);
	if (ElfFile::IsElfFile(path))
		return OpenElf(path);

	CWaitCursor wait;
	int bitness = (m_PE && m_PE.GetFileInfo()->IsPE64) * 2 + (m_PE && m_PE.GetFileInfo()->IsPE32);

	CloseDecompiler();	// (before the file it reads goes)
	CancelXrefs();
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
	CancelVirusTotal();
	m_Overlay = FindOverlay(m_PE);
	m_GuardTables = ReadGuardTables(m_PE);
	StartXrefs();
	ResetNavigation();
	StartSymbolLoad(path);	// the window is usable at once; symbols arrive later (WM_SYMBOLS_LOADED)
	m_Views.clear();
	m_Views2.clear();
	m_Tabs.RemoveAllPages();
	m_Archive.Close();
	m_Object.Close();
	m_ObjectCodeView = {};
	m_MemberObjects.clear();
	m_Elf.Close();
	m_ElfNames.clear();
	m_ElfDebug = {};

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
	SearchTargetChanged();

	return true;
}

// A library: the members, the index of the symbols and the records of an import library are shown. The PE file that was open is closed.
bool CMainFrame::OpenArchive(PCWSTR path) {
	CWaitCursor wait;
	LibArchive archive;
	if (!archive.Open(path)) {
		AtlMessageBox(m_hWnd, L"Error parsing the library", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}

	ResetFileState();
	m_Archive = std::move(archive);
	SetStatusText(0, L"");

	BuildArchiveTree(16);

	CString ftitle;
	ftitle.LoadString(IDR_MAINFRAME);
	if (SecurityHelper::IsRunningElevated())
		ftitle += L" (Administrator)";
	CString spath(path);
	SetWindowText(spath.Mid(spath.ReverseFind(L'\\') + 1) + L" - " + ftitle);
	m_RecentFiles.AddFile(path);
	AppSettings::Get().RecentFiles(m_RecentFiles.Files());
	UpdateRecentFilesMenu();

	ShowView(m_hRoot);
	UpdateUI();
	SearchTargetChanged();
	return true;
}

void CMainFrame::BuildArchiveTree(int iconSize) {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Tree.SetItemHeight(iconSize + 2);
	if (BuildTreeImageList(iconSize)) {
		m_Tree.SetImageList(m_TreeImages);
		m_Tabs.SetImageList(m_TreeImages);
	}

	auto& path = m_Archive.Path();
	auto root = InsertTreeItem(m_Tree, path.substr(path.rfind(L'\\') + 1).c_str(), 0, TreeItemType::ArchiveMembers);
	if (!m_Archive.Symbols().empty())
		InsertTreeItem(m_Tree, std::format(L"Symbols ({})", m_Archive.Symbols().size()).c_str(), GetTreeIcon(IDI_SYMBOLS), TreeItemType::ArchiveSymbols, root);
	if (!m_Archive.Imports().empty())
		InsertTreeItem(m_Tree, std::format(L"Import Records ({})", m_Archive.Imports().size()).c_str(), GetTreeIcon(IDI_DLL_IMPORT), TreeItemType::ArchiveImports, root);

	m_hRoot = root;
	m_Tree.Expand(root, TVE_EXPAND);
	m_Tree.SelectItem(root);
	m_Tree.SetRedraw();
	m_Tree.SetFocus();
}

// What a PE file leaves behind, when a library or an object file is opened instead
void CMainFrame::ResetFileState() {
	m_Tabs.RemoveAllPages();
	CloseDecompiler();
	CancelXrefs();
	m_Views.clear();
	m_Views2.clear();
	m_PE.Close();
	m_Archive.Close();
	m_Object.Close();
	m_ObjectCodeView = {};
	m_MemberObjects.clear();
	m_Elf.Close();
	m_ElfNames.clear();
	m_ElfDebug = {};
	m_Symbols.Close();
	++m_SymbolGeneration;
	m_SymbolsForModules.clear();
	m_Anomalies.clear();
	CancelVirusTotal();
	m_Overlay = {};
	m_hOverlay = nullptr;
	m_hResVersion = m_hResManifest = nullptr;
	m_FlatResources.clear();
	m_NamedRvas.clear();
	m_GuardTables.clear();
	m_Xrefs.Clear();
	ResetNavigation();
}

// An object file: its header, sections, symbols and relocations. The PE file that was open is closed.
bool CMainFrame::OpenObject(PCWSTR path) {
	CWaitCursor wait;
	CoffObject obj;
	if (!obj.Open(path)) {
		AtlMessageBox(m_hWnd, L"Error parsing the object file", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}

	ResetFileState();
	m_Object = std::move(obj);
	m_ObjectCodeView = ReadCodeView(m_Object);
	SetStatusText(0, L"");
	BuildObjectTree(16);

	CString ftitle;
	ftitle.LoadString(IDR_MAINFRAME);
	if (SecurityHelper::IsRunningElevated())
		ftitle += L" (Administrator)";
	CString spath(path);
	SetWindowText(spath.Mid(spath.ReverseFind(L'\\') + 1) + L" - " + ftitle);
	m_RecentFiles.AddFile(path);
	AppSettings::Get().RecentFiles(m_RecentFiles.Files());
	UpdateRecentFilesMenu();

	ShowView(m_hRoot);
	UpdateUI();
	SearchTargetChanged();
	return true;
}

void CMainFrame::BuildObjectTree(int iconSize) {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Tree.SetItemHeight(iconSize + 2);
	if (BuildTreeImageList(iconSize)) {
		m_Tree.SetImageList(m_TreeImages);
		m_Tabs.SetImageList(m_TreeImages);
	}

	auto& path = m_Object.Path();
	auto root = InsertTreeItem(m_Tree, path.substr(path.rfind(L'\\') + 1).c_str(), 0, TreeItemType::ObjectHeader);
	InsertObjectItems(root, m_Object, m_ObjectCodeView, -1);

	m_hRoot = root;
	m_Tree.Expand(root, TVE_EXPAND);
	m_Tree.SelectItem(root);
	m_Tree.SetRedraw();
	m_Tree.SetFocus();
}

void CMainFrame::InsertObjectItems(HTREEITEM hObject, CoffObject const& obj, CodeViewInfo const& cv, int member) {
	auto sections = InsertTreeItem(m_Tree, std::format(L"Sections ({})", obj.Sections().size()).c_str(), GetTreeIcon(IDI_SECTIONS),
		ObjectItem(TreeItemType::ObjectSections, member), hObject);
	// a bigobj can have tens of thousands: they are in the list of the sections then
	if (obj.Sections().size() <= 1000) {
		int i = 0;
		for (auto const& sec : obj.Sections()) {
			InsertTreeItem(m_Tree, std::format(L"{} ({})", std::wstring(sec.Name.begin(), sec.Name.end()), i + 1).c_str(), GetTreeIcon(IDI_SECTION),
				ObjectItem(TreeItemType::ObjectSection, member, i), sections);
			i++;
		}
	}
	if (!obj.Symbols().empty())
		InsertTreeItem(m_Tree, std::format(L"Symbols ({})", obj.Symbols().size()).c_str(), GetTreeIcon(IDI_SYMBOLS),
			ObjectItem(TreeItemType::ObjectSymbols, member), hObject);
	if (!obj.Relocations().empty())
		InsertTreeItem(m_Tree, std::format(L"Relocations ({})", obj.Relocations().size()).c_str(), GetTreeIcon(IDI_RELOC),
			ObjectItem(TreeItemType::ObjectRelocations, member), hObject);
	if (!obj.LineNumbers().empty())
		InsertTreeItem(m_Tree, std::format(L"Line Numbers ({})", obj.LineNumbers().size()).c_str(), GetTreeIcon(IDI_TEXT),
			ObjectItem(TreeItemType::ObjectLineNumbers, member), hObject);
	if (cv.Empty())
		return;

	// the CodeView information (/Z7, /Zi): its subsections, then what is in them
	auto hcv = InsertTreeItem(m_Tree, L"CodeView", GetTreeIcon(IDI_DEBUG), ObjectItem(TreeItemType::CodeView, member), hObject);
	if (!cv.Symbols.empty())
		InsertTreeItem(m_Tree, std::format(L"Symbols ({})", cv.Symbols.size()).c_str(), GetTreeIcon(IDI_SYMBOLS),
			ObjectItem(TreeItemType::CodeViewSymbols, member), hcv);
	if (!cv.Lines.empty())
		InsertTreeItem(m_Tree, std::format(L"Source Lines ({})", cv.Lines.size()).c_str(), GetTreeIcon(IDI_TEXT),
			ObjectItem(TreeItemType::CodeViewLines, member), hcv);
	if (!cv.Files.empty())
		InsertTreeItem(m_Tree, std::format(L"Source Files ({})", cv.Files.size()).c_str(), GetTreeIcon(IDI_DIR_OPEN),
			ObjectItem(TreeItemType::CodeViewFiles, member), hcv);
	if (!cv.Types.empty())
		InsertTreeItem(m_Tree, std::format(L"Types ({})", cv.Types.size()).c_str(), GetTreeIcon(IDI_TYPE),
			ObjectItem(TreeItemType::CodeViewTypes, member), hcv);
}

CMainFrame::ObjectRef CMainFrame::FindObject(int member) const {
	if (member < 0)
		return m_Object ? ObjectRef{ &m_Object, &m_ObjectCodeView, nullptr } : ObjectRef{};
	auto it = m_MemberObjects.find(member);
	if (it == m_MemberObjects.end())
		return {};
	return { &it->second.Object, &it->second.CodeView, it->second.Name.c_str() };
}

TreeItemType CMainFrame::ObjectItem(TreeItemType type, int member, int section) {
	return static_cast<TreeItemType>((int64_t)type | ((int64_t)(member + 1) << ItemShift) | ((int64_t)(section + 1) << 32));
}

int CMainFrame::ObjectItemMember(TreeItemType type) {
	return (int)(((int64_t)type >> ItemShift) & 0xFFFFFF) - 1;
}

int CMainFrame::ObjectItemSection(TreeItemType type) {
	return (int)((int64_t)type >> 32) - 1;
}

// An ELF file: its header, segments, sections, symbols, dynamic section, relocations and notes. The PE file that was open is closed.
bool CMainFrame::OpenElf(PCWSTR path) {
	CWaitCursor wait;
	ElfFile elf;
	if (!elf.Open(path)) {
		AtlMessageBox(m_hWnd, L"Error parsing the ELF file", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}

	ResetFileState();
	m_Elf = std::move(elf);
	m_ElfCodeViews = 0;
	m_ElfCodeItems.clear();
	m_ElfDebug = LoadElfDebugInfo(m_Elf);
	// the names the disassembly shows: in a relocatable object the values are offsets in sections, not addresses.
	// C++ names are demangled; the functions of the DWARF name what the symbols do not (a stripped file with its debug file)
	if (m_Elf.Type() != 1) {
		auto name = [](std::string const& mangled) {
			auto d = DemangleItanium(mangled);
			return d.empty() ? std::wstring(mangled.begin(), mangled.end()) : std::wstring(d.begin(), d.end());
		};
		for (auto const& s : m_Elf.Symbols()) {
			bool defined = s.SectionIndex != 0 && s.SectionIndex < 0xFF00;
			if (defined && s.Value && !s.Name.empty() && (s.Type == 1 || s.Type == 2 || s.Type == 10))
				m_ElfNames.push_back({ s.Value, s.Size, name(s.Name) });
		}
		for (auto const& f : m_ElfDebug.Dwarf.Functions) {
			if (f.LowPc && (!f.LinkageName.empty() || !f.Name.empty()))
				m_ElfNames.push_back({ f.LowPc, f.HighPc - f.LowPc, f.LinkageName.empty() ? name(f.Name) : name(f.LinkageName) });
		}
		// the symbols come first at the same address
		std::ranges::stable_sort(m_ElfNames, {}, &ElfName::Address);
		// the same symbol is often in .symtab and .dynsym
		auto dup = std::ranges::unique(m_ElfNames, [](auto const& a, auto const& b) { return a.Address == b.Address; });
		m_ElfNames.erase(dup.begin(), dup.end());
	}
	SetStatusText(0, L"");
	BuildElfTree(16);

	CString ftitle;
	ftitle.LoadString(IDR_MAINFRAME);
	if (SecurityHelper::IsRunningElevated())
		ftitle += L" (Administrator)";
	CString spath(path);
	SetWindowText(spath.Mid(spath.ReverseFind(L'\\') + 1) + L" - " + ftitle);
	m_RecentFiles.AddFile(path);
	AppSettings::Get().RecentFiles(m_RecentFiles.Files());
	UpdateRecentFilesMenu();

	ShowView(m_hRoot);
	UpdateUI();
	SearchTargetChanged();
	return true;
}

void CMainFrame::BuildElfTree(int iconSize) {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Tree.SetItemHeight(iconSize + 2);
	if (BuildTreeImageList(iconSize)) {
		m_Tree.SetImageList(m_TreeImages);
		m_Tabs.SetImageList(m_TreeImages);
	}

	auto& path = m_Elf.Path();
	auto root = InsertTreeItem(m_Tree, path.substr(path.rfind(L'\\') + 1).c_str(), 0, TreeItemType::ElfHeader);
	if (!m_Elf.ProgramHeaders().empty())
		InsertTreeItem(m_Tree, std::format(L"Program Headers ({})", m_Elf.ProgramHeaders().size()).c_str(), GetTreeIcon(IDI_HEADERS),
			TreeItemType::ElfProgramHeaders, root);
	if (!m_Elf.Sections().empty()) {
		auto sections = InsertTreeItem(m_Tree, std::format(L"Sections ({})", m_Elf.Sections().size()).c_str(), GetTreeIcon(IDI_SECTIONS),
			TreeItemType::ElfSections, root);
		if (m_Elf.Sections().size() <= 1000) {
			// the first is the null section
			for (int i = 1; i < (int)m_Elf.Sections().size(); i++) {
				auto const& name = m_Elf.Sections()[i].Name;
				InsertTreeItem(m_Tree, name.empty() ? std::format(L"({})", i).c_str() : std::wstring(name.begin(), name.end()).c_str(),
					GetTreeIcon(IDI_SECTION), TreeItemWithIndex(TreeItemType::ElfSection, (int64_t)(i + 1) << ItemShift), sections);
			}
		}
	}
	if (!m_Elf.Symbols().empty())
		InsertTreeItem(m_Tree, std::format(L"Symbols ({})", m_Elf.Symbols().size()).c_str(), GetTreeIcon(IDI_SYMBOLS), TreeItemType::ElfSymbols, root);
	// the debug information: in the file, in its debug file, or where it should be
	auto debug = InsertTreeItem(m_Tree, L"Debug Info", GetTreeIcon(IDI_DEBUG), TreeItemType::ElfDebugInfo, root);
	if (!m_ElfDebug.Dwarf.Units.empty())
		InsertTreeItem(m_Tree, std::format(L"Compile Units ({})", m_ElfDebug.Dwarf.Units.size()).c_str(), GetTreeIcon(IDI_TEXT), TreeItemType::ElfCompileUnits, debug);
	if (!m_ElfDebug.Dwarf.Functions.empty())
		InsertTreeItem(m_Tree, std::format(L"Functions ({})", m_ElfDebug.Dwarf.Functions.size()).c_str(), GetTreeIcon(IDI_FUNCTION), TreeItemType::ElfFunctions, debug);
	if (!m_Elf.Dynamic().empty())
		InsertTreeItem(m_Tree, std::format(L"Dynamic ({})", m_Elf.Dynamic().size()).c_str(), GetTreeIcon(IDI_DLL_IMPORT), TreeItemType::ElfDynamic, root);
	if (!m_Elf.Relocations().empty())
		InsertTreeItem(m_Tree, std::format(L"Relocations ({})", m_Elf.Relocations().size()).c_str(), GetTreeIcon(IDI_RELOC), TreeItemType::ElfRelocations, root);
	if (!m_Elf.Notes().empty())
		InsertTreeItem(m_Tree, std::format(L"Notes ({})", m_Elf.Notes().size()).c_str(), GetTreeIcon(IDI_TEXT), TreeItemType::ElfNotes, root);
	InsertTreeItem(m_Tree, L"Strings", GetTreeIcon(IDI_TEXT), TreeItemType::Strings, root);
	InsertTreeItem(m_Tree, L"File in Hex", GetTreeIcon(IDI_BINARY), TreeItemType::ElfFileInHex, root);

	m_hRoot = root;
	m_Tree.Expand(root, TVE_EXPAND);
	m_Tree.SelectItem(root);
	m_Tree.SetRedraw();
	m_Tree.SetFocus();
}

bool CMainFrame::ShowElfAddress(uint64_t address, bool code) {
	if (!m_Elf)
		return false;
	if (auto section = m_Elf.SectionOfAddress(address); section >= 0)
		return ShowElfSection(section, (int64_t)(address - m_Elf.Sections()[section].Address), code);
	if (auto offset = m_Elf.OffsetOfAddress(address); offset >= 0)
		return ShowElfFileOffset(offset);
	AtlMessageBox(m_hWnd, std::format(L"The address 0x{:X} is not in the file", address).c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
	return false;
}

bool CMainFrame::ShowElfSection(int section, int64_t offset, bool code) {
	if (!m_Elf || section <= 0 || section >= (int)m_Elf.Sections().size())
		return false;
	auto data = m_Elf.SectionData(section);
	if (data.empty()) {
		AtlMessageBox(m_hWnd, L"The section has no data in the file", IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}
	if (offset >= (int64_t)data.size())
		offset = -1;
	// code: x86, x64 and ARM64 are disassembled
	if (code && ArchOfElfMachine(m_Elf.Machine()) && (m_Elf.Sections()[section].Flags & 4))
		return ShowElfCode(section, offset < 0 ? 0 : (uint64_t)offset);

	RecordNavigation();
	auto type = TreeItemWithIndex(TreeItemType::ElfSection, (int64_t)(section + 1) << ItemShift);
	if (!ShowView(type, nullptr, IDI_SECTION))
		return false;
	if (offset >= 0)
		if (auto it = m_Views.find(type); it != m_Views.end())
			it->second->SetNavigationPosition(offset);
	RecordNavigation();
	return true;
}

bool CMainFrame::ShowElfFileOffset(int64_t offset) {
	if (!m_Elf || offset < 0 || offset >= (int64_t)m_Elf.Data().size())
		return false;
	RecordNavigation();
	if (!ShowView(TreeItemType::ElfFileInHex, nullptr, IDI_BINARY))
		return false;
	if (auto it = m_Views.find(TreeItemType::ElfFileInHex); it != m_Views.end())
		it->second->SetNavigationPosition(offset);
	RecordNavigation();
	return true;
}

// The code from an offset in a section to the end of the section (0x2000 bytes at most), in a view of its own
bool CMainFrame::ShowElfCode(int section, uint64_t offset) {
	auto arch = ArchOfElfMachine(m_Elf.Machine());
	if (!arch)
		return false;
	auto const& sec = m_Elf.Sections()[section];
	auto data = m_Elf.SectionData(section);
	auto address = sec.Address + offset;
	auto name = ResolveVa(address);
	std::wstring title = m_Elf.Type() == 1 ? std::format(L"{}+0x{:X}", std::wstring(sec.Name.begin(), sec.Name.end()), offset)
		: name.empty() || name.find(L'+') != std::wstring::npos ? std::format(L"Code at 0x{:X}", address) : FunctionName(name);
	// open already: found by its address (functions that are overloads have the same title)
	if (auto it = m_ElfCodeItems.find(address); it != m_ElfCodeItems.end()) {
		if (auto view = m_Views.find(it->second); view != m_Views.end()) {
			ShowView(view->second->GetHTreeItem());
			return true;
		}
		m_ElfCodeItems.erase(it);
	}

	RecordNavigation();
	auto view = new CScintillaView(this, m_PE, title.c_str());
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return false;
	}
	view->SetLanguage(LexLanguage::Asm);
	view->SetAsmCode(data.subspan((size_t)offset, (size_t)std::min<uint64_t>(0x2000, data.size() - offset)), address, *arch);
	view->GetCtrl().SetReadOnly(true);
	view->SetDeleteFromTree(true);

	auto image = GetIconIndex(IDI_BINARY);
	auto itemType = TreeItemWithIndex(TreeItemType::ElfCode, (int64_t)(++m_ElfCodeViews) << ItemShift);
	m_ElfCodeItems[address] = itemType;
	auto hItem = InsertTreeItem(m_Tree, title.c_str(), image, itemType, m_hRoot, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return true;
}

bool CMainFrame::ShowObjectSection(int section, int64_t offset, int member) {
	auto obj = FindObject(member).Object;
	if (!obj || section < 0 || section >= (int)obj->Sections().size())
		return false;
	if (obj->SectionData(section).empty()) {
		AtlMessageBox(m_hWnd, L"The section has no data in the file", IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}
	RecordNavigation();
	auto type = ObjectItem(TreeItemType::ObjectSection, member, section);
	if (!ShowView(type, nullptr, IDI_SECTION))
		return false;
	if (offset >= 0)
		if (auto it = m_Views.find(type); it != m_Views.end())
			it->second->SetNavigationPosition(offset);
	RecordNavigation();
	return true;
}

// An object member gets the items of an object file under its own (once); anything else is shown in the hex view
bool CMainFrame::ShowArchiveMember(int member) {
	if (!m_Archive || member < 0 || member >= (int)m_Archive.Members().size())
		return false;
	auto hexType = TreeItemWithIndex(TreeItemType::ArchiveMember, (int64_t)(member + 1) << ItemShift);
	auto const& info = m_Archive.Members()[member];
	if (info.Kind != ArchiveMemberKind::Object)
		return ShowView(hexType, nullptr, IDI_BINARY);

	auto it = m_MemberObjects.find(member);
	if (it == m_MemberObjects.end()) {
		CWaitCursor wait;
		MemberObject obj;
		if (!obj.Object.Parse(m_Archive.MemberData(member)))
			return ShowView(hexType, nullptr, IDI_BINARY);
		obj.CodeView = ReadCodeView(obj.Object);
		// the name of a member is often the path of the object when the library was built
		std::wstring name(info.Name.begin(), info.Name.end());
		if (auto slash = name.find_last_of(L"\\/"); slash != std::wstring::npos && slash + 1 < name.size())
			name = name.substr(slash + 1);
		obj.Name = name;
		it = m_MemberObjects.emplace(member, std::move(obj)).first;

		auto& added = it->second;
		m_Tree.SetRedraw(FALSE);
		added.hItem = InsertTreeItem(m_Tree, std::format(L"{} (Member {})", name, member).c_str(), 0,
			ObjectItem(TreeItemType::ObjectHeader, member), m_hRoot);
		InsertObjectItems(added.hItem, added.Object, added.CodeView, member);
		InsertTreeItem(m_Tree, L"Member in Hex", GetTreeIcon(IDI_BINARY), hexType, added.hItem);
		m_Tree.SetRedraw();
	}
	RecordNavigation();
	auto shown = ShowView(ObjectItem(TreeItemType::ObjectHeader, member), it->second.hItem);
	m_Tree.Expand(it->second.hItem, TVE_EXPAND);
	RecordNavigation();
	return shown;
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
			auto hDir = InsertTreeItem(m_Tree, PEStrings::GetDataDirectoryName(i), DirectoryIndexToIcon(i),
				TreeItemWithIndex(TreeItemType::Directory, i), directories);
			if (i == IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR)
				InsertTreeItem(m_Tree, L".NET Types", GetTreeIcon(IDI_CLASS), TreeItemType::ClrTypes, hDir);
			if (i == IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG) {
				int t = 0;
				for (auto const& table : m_GuardTables) {
					InsertTreeItem(m_Tree, std::format(L"{} ({})", GuardTableName(table.Kind), table.Count).c_str(), GetTreeIcon(IDI_FUNCTION),
						TreeItemWithIndex(TreeItemType::GuardTable, (int64_t)(t + 1) << ItemShift), hDir);
					t++;
				}
			}
		}
		i++;
	}
	m_Tree.Expand(directories, TVE_EXPAND);

	InsertTreeItem(m_Tree, std::format(L"Anomalies ({})", m_Anomalies.size()).c_str(), GetTreeIcon(IDI_EXCEPTION), TreeItemType::Anomalies, root);
	InsertTreeItem(m_Tree, L"Security Summary", GetTreeIcon(IDI_SECURITY), TreeItemType::SecuritySummary, root);
	m_hOverlay = nullptr;
	if (!m_Overlay.Empty())
		m_hOverlay = InsertTreeItem(m_Tree, L"Overlay", GetTreeIcon(IDI_BINARY), TreeItemType::Overlay, root);
	InsertTreeItem(m_Tree, L"Strings", GetTreeIcon(IDI_TEXT), TreeItemType::Strings, root);
	if (ArchOf(m_PE))
		InsertTreeItem(m_Tree, L"Functions", GetTreeIcon(IDI_FUNCTION), TreeItemType::Functions, root);

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
		InsertSymbolsNode(root);
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

// Compares the open PE file with another: the comparison is a view of its own (and a tree item while it is open)
LRESULT CMainFrame::OnFileCompare(WORD, WORD, HWND, BOOL&) {
	if (!m_PE)
		return 0;
	CSimpleFileDialog dlg(TRUE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING | OFN_FILEMUSTEXIST,
		L"All PE Files\0*.exe;*.dll;*.efi;*.ocx;*.cpl;*.sys;*.mui;*.mun;*.scr\0All Files\0*.*\0");
	dlg.m_ofn.lpstrTitle = L"Compare With";
	WTLHelper::SuspendHook();
	auto ok = IDOK == dlg.DoModal();
	WTLHelper::ResumeHook();
	if (!ok)
		return 0;

	CWaitCursor wait;
	PEFile other;
	if (!other.Open(dlg.m_szFileName)) {
		AtlMessageBox(m_hWnd, L"The file is not a PE file", IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	auto view = new CCompareView(this, this, ComparePEFiles(m_PE, other), dlg.m_szFileName);
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return 0;
	}
	view->SetDeleteFromTree(true);
	auto image = GetIconIndex(IDI_LOOK);
	auto itemType = TreeItemWithIndex(TreeItemType::Comparison, (int64_t)(++m_Comparisons) << ItemShift);
	auto hItem = InsertTreeItem(m_Tree, view->GetTitle(), image, itemType, m_hRoot);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return 0;
}

LRESULT CMainFrame::OnFileClose(WORD, WORD, HWND, BOOL&) {
	m_Tabs.RemoveAllPages();
	CloseDecompiler();
	CancelXrefs();
	m_Views.clear();
	m_Views2.clear();
	m_PE.Close();
	m_Archive.Close();
	m_Object.Close();
	m_ObjectCodeView = {};
	m_MemberObjects.clear();
	m_Elf.Close();
	m_ElfNames.clear();
	m_ElfDebug = {};
	m_GuardTables.clear();
	++m_SymbolGeneration;	// a symbol load in progress belongs to the file that was just closed
	m_Symbols.Close();
	m_Anomalies.clear();
	CancelVirusTotal();
	m_Overlay = {};
	m_hOverlay = nullptr;
	m_Xrefs.Clear();
	ResetNavigation();
	m_Tree.DeleteAllItems();
	UpdateUI();
	SearchTargetChanged();
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

// Save: a view that knows how to save what it shows does so (the hex view its bytes, a flow graph as SVG), and the command
// does not get here; for the others, a list is saved as CSV or tab separated text and a text view as text.
// Save is disabled in the views that have none of these (see UpdateSaveUI); the accelerator gets here anyway.
LRESULT CMainFrame::OnFileSave(WORD, WORD, HWND, BOOL&) {
	if (int page = m_Tabs.GetActivePage(); page >= 0) {
		auto hView = m_Tabs.GetPageHWND(page);
		auto name = ToFileName(m_Tabs.GetPageTitle(page));
		if (auto hList = FindViewControl(hView, WC_LISTVIEW)) {
			SaveListView(m_hWnd, hList, name);
			return 0;
		}
		if (auto hText = FindViewControl(hView, L"Scintilla")) {
			SaveText(hText, name);
			return 0;
		}
	}
	::MessageBeep((UINT)-1);
	return 0;
}

// Export List: the list of the active view, whatever Save does in that view
LRESULT CMainFrame::OnFileExportList(WORD, WORD, HWND, BOOL&) {
	int page = m_Tabs.GetActivePage();
	HWND hList = page >= 0 ? FindViewControl(m_Tabs.GetPageHWND(page), WC_LISTVIEW) : nullptr;
	if (!hList) {
		AtlMessageBox(m_hWnd, L"The active view has no list", IDR_MAINFRAME, MB_ICONINFORMATION);
		return 0;
	}
	SaveListView(m_hWnd, hList, ToFileName(m_Tabs.GetPageTitle(page)));
	return 0;
}

// Save and Export List are enabled by what the active view has: Save if the view saves what it shows (CanSave), or has a list or
// text; Export List if it has a list. Updated when another page becomes active.
void CMainFrame::UpdateSaveUI() {
	int page = m_Tabs.GetActivePage();
	auto hPage = page >= 0 ? m_Tabs.GetPageHWND(page) : nullptr;
	if (hPage == m_SaveUIPage)
		return;
	m_SaveUIPage = hPage;

	bool list = hPage && FindViewControl(hPage, WC_LISTVIEW);
	bool save = list || (hPage && FindViewControl(hPage, L"Scintilla"));
	if (!save && hPage) {
		if (auto it = m_Views2.find(hPage); it != m_Views2.end())
			if (auto view = m_Views.find(it->second); view != m_Views.end())
				save = view->second->CanSave();
	}
	UIEnable(ID_FILE_EXPORTLIST, list);
	UIEnable(ID_FILE_SAVE, save);

	// a resource that is shown other than in hex
	bool resource = false;
	if (ActiveResource() >= 0)
		resource = dynamic_cast<CHexView*>(m_Views.at(m_Views2.at(hPage))) == nullptr;
	UIEnable(ID_VIEW_RESOURCEHEX, resource);
}

// The index of the resource that the active view shows (in m_FlatResources), -1 if it shows something else
int CMainFrame::ActiveResource() {
	int page = m_Tabs.GetActivePage();
	if (page < 0)
		return -1;
	auto it = m_Views2.find(m_Tabs.GetPageHWND(page));
	if (it == m_Views2.end() || (it->second & TreeItemType::ItemMask) != TreeItemType::Resource || !m_Views.contains(it->second))
		return -1;
	auto index = ((int64_t)it->second >> ItemShift) - 1;
	return index >= 0 && index < (int64_t)m_FlatResources.size() ? (int)index : -1;
}

// The data of a resource in a hex view of its own, under the item of the resource while it is open
bool CMainFrame::ShowResourceHex(int index) {
	if (index < 0 || index >= (int)m_FlatResources.size())
		return false;
	auto type = TreeItemWithIndex(TreeItemType::ResourceHex, (int64_t)(index + 1) << ItemShift);
	RecordNavigation();
	if (!ShowView(type, nullptr, IDI_BINARY))
		return false;
	if (auto hItem = m_Views.at(type)->GetHTreeItem()) {
		m_Tree.EnsureVisible(hItem);
		m_Tree.SelectItem(hItem);
	}
	RecordNavigation();
	return true;
}

LRESULT CMainFrame::OnViewResourceHex(WORD, WORD, HWND, BOOL&) {
	ShowResourceHex(ActiveResource());
	return 0;
}

// Save Data on a section or a resource in the tree
bool CMainFrame::SaveTreeItemData(TreeItemType type) {
	auto index = (size_t)((int64_t)type >> ItemShift) - 1;
	switch (type & TreeItemType::ItemMask) {
		case TreeItemType::Section:
			if (index < m_PE.GetSecHeaders()->size())
				return SaveSectionData(m_hWnd, m_PE, m_PE.GetSecHeaders()->at(index));
			break;
		case TreeItemType::Resource:
			if (index < m_FlatResources.size())
				return SaveResourceFiles(m_hWnd, { &m_FlatResources[index] }, m_FlatResources);
			break;
	}
	return false;
}

bool CMainFrame::SaveText(HWND hText, CString const& name) {
	auto path = AskSaveFile(m_hWnd, L"Save Text", L"txt", name + L".txt", L"Text Files (*.txt)\0*.txt\0Assembly Files (*.asm)\0*.asm\0All Files\0*.*\0");
	if (path.IsEmpty())
		return false;

	auto length = (size_t)::SendMessage(hText, SCI_GETLENGTH, 0, 0);
	std::string text(length + 1, '\0');
	::SendMessage(hText, SCI_GETTEXT, length + 1, (LPARAM)text.data());
	text.resize(length);
	if (!WriteFileData(path, text.data(), text.size())) {
		AtlMessageBox(m_hWnd, L"Failed to save the file", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}
	return true;
}

CString CMainFrame::DoFileOpen() const {
	CSimpleFileDialog dlg(TRUE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING,
		L"All PE Files\0*.exe;*.dll;*.efi;*.ocx;*.cpl;*.sys;*.mui;*.mun;*.scr\0Libraries\0*.lib;*.a\0Object Files\0*.obj;*.o\0All Files\0*.*\0");
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
	CString path = CurrentPath().c_str();
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
		IDI_DATA, IDI_FLOW, IDI_LOOK, IDI_CLASS, IDI_PROPERTY, IDI_EVENT,
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
	CancelDecompiles(hWnd);
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

LRESULT CMainFrame::OnViewStrings(WORD, WORD, HWND, BOOL&) {
	ShowView(TreeItemType::Strings, nullptr, IDI_TEXT);
	return 0;
}

LRESULT CMainFrame::OnViewFunctions(WORD, WORD, HWND, BOOL&) {
	if (auto hItem = FindTreeItem(TreeItemType::Functions))
		ShowView(hItem);
	return 0;
}

LRESULT CMainFrame::OnViewSecuritySummary(WORD, WORD, HWND, BOOL&) {
	if (auto hItem = FindTreeItem(TreeItemType::SecuritySummary))
		ShowView(hItem);
	return 0;
}

LRESULT CMainFrame::OnViewClrTypes(WORD, WORD, HWND, BOOL&) {
	if (auto hItem = FindTreeItem(TreeItemType::ClrTypes))
		ShowView(hItem);
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
	if (m_Elf) {
		if (!m_Elf.Entry() || !ShowElfAddress(m_Elf.Entry(), true))
			AtlMessageBox(m_hWnd, L"The file has no entry point", IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	if (!ShowView(TreeItemType::AsmEntryPoint, nullptr, IDI_BINARY))
		AtlMessageBox(m_hWnd, L"No entry point defined for this PE", IDR_MAINFRAME, MB_ICONWARNING);
	return 0;
}

// A function by its name (the exports first, then the symbols) or by its address (hexadecimal):
// in a PE, an address at the image base or above is a VA, otherwise an RVA
bool CMainFrame::ResolveFunction(std::wstring const& text, uint64_t& address, std::wstring& error) const {
	if (m_Elf) {
		for (auto const& n : m_ElfNames) {
			if (n.Name == text) {
				address = n.Address;
				return true;
			}
		}
		if (ParseHexNumber(text, address))
			return true;
		error = std::format(L"No function named \"{}\" was found.", text);
		return false;
	}

	auto base = m_PE.GetImageBase();
	if (auto exp = m_PE.GetExport()) {
		for (auto const& f : exp->Funcs) {
			if (f.FuncRVA && f.ForwarderName.empty() && !f.FuncName.empty()
				&& std::equal(f.FuncName.begin(), f.FuncName.end(), text.begin(), text.end())) {
				address = base + f.FuncRVA;
				return true;
			}
		}
	}
	if (m_Symbols) {
		// functions (private symbols) first; public symbols may be decorated
		std::pair<SymbolTag, CompareOptions> const lookups[] = {
			{ SymbolTag::Function, CompareOptions::CaseSensitive },
			{ SymbolTag::PublicSymbol, CompareOptions::CaseSensitive },
			{ SymbolTag::PublicSymbol, CompareOptions::CaseSensitive | CompareOptions::UndecoratedName },
			{ SymbolTag::Function, CompareOptions::CaseInsensitive },
			{ SymbolTag::PublicSymbol, CompareOptions::CaseInsensitive | CompareOptions::UndecoratedName },
		};
		for (auto& [tag, options] : lookups) {
			for (auto const& sym : m_Symbols.FindChildren(text.c_str(), tag, options)) {
				if (sym.Location() == LocationKind::Static && sym.VirtualAddress()) {
					address = sym.VirtualAddress();
					return true;
				}
			}
		}
	}

	if (!ParseHexNumber(text, address)) {
		error = std::format(L"No function named \"{}\" was found in the exports{}.", text,
			m_Symbols ? L" or the symbols" : L" (and no symbols are loaded)");
		return false;
	}
	if (address < base)
		address += base;	// an RVA
	if (address - base > 0xFFFFFFFFULL) {
		error = std::format(L"The address 0x{:X} is outside the image.", address);
		return false;
	}
	return true;
}

LRESULT CMainFrame::OnDecompileFunction(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_PE)
		return 0;
	CDisassembleFunctionDlg dlg(m_DisassembleFunction, L"Decompile Function", L"Decompile");
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;

	uint64_t address;
	std::wstring error;
	if (!ResolveFunction(m_DisassembleFunction, address, error)) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	DecompileFunction(address);
	return 0;
}

bool CMainFrame::ReadImage(uint64_t va, uint8_t* buffer, size_t size) const {
	auto base = m_PE.GetImageBase();
	auto const& nt = *m_PE.GetNTHeader();
	bool pe64 = m_PE.GetFileInfo()->IsPE64;
	uint64_t imageSize = pe64 ? nt.NTHdr64.OptionalHeader.SizeOfImage : nt.NTHdr32.OptionalHeader.SizeOfImage;
	uint64_t headers = pe64 ? nt.NTHdr64.OptionalHeader.SizeOfHeaders : nt.NTHdr32.OptionalHeader.SizeOfHeaders;
	if (va < base || va - base + size > imageSize)
		return false;

	uint64_t fileSize = m_PE.GetFileSize();
	// the bytes of a part of the image: in the file up to its size, zeros after
	auto copy = [&](uint64_t fileOffset, uint64_t available, uint64_t count, uint8_t* out) {
		uint64_t inFile = std::min(count, available);
		if (fileOffset >= fileSize)
			inFile = 0;
		else
			inFile = std::min(inFile, fileSize - fileOffset);
		if (inFile)
			memcpy(out, m_PE.GetData() + fileOffset, (size_t)inFile);
		memset(out + inFile, 0, (size_t)(count - inFile));
	};

	uint64_t rva = va - base;
	while (size > 0) {
		uint64_t count = 0;
		if (rva < headers) {
			count = std::min<uint64_t>(size, headers - rva);
			copy(rva, headers - rva, count, buffer);
		}
		else {
			for (auto const& s : *m_PE.GetSecHeaders()) {
				auto const& h = s.SecHdr;
				uint64_t extent = std::max(h.Misc.VirtualSize, h.SizeOfRawData);
				if (rva >= h.VirtualAddress && rva < h.VirtualAddress + extent) {
					uint64_t offset = rva - h.VirtualAddress;
					count = std::min<uint64_t>(size, extent - offset);
					copy(h.PointerToRawData + offset, h.SizeOfRawData > offset ? h.SizeOfRawData - offset : 0, count, buffer);
					break;
				}
			}
			if (count == 0)
				return false;	// between sections: nothing there
		}
		buffer += count;
		rva += count;
		size -= (size_t)count;
	}
	return true;
}

namespace {
	std::string Narrow(std::wstring const& text) {
		return std::string(CW2A(text.c_str(), CP_UTF8));
	}

	// a DLL as the decompiler shows it: an API set with the DLL that implements it on this system
	std::string ShownModule(std::string const& module) {
		if (auto host = ApiSetMap::System().Resolve(module))
			return module + " (" + *host + ")";
		return module;
	}

	// the value of an enum's constant as its bits
	uint64_t EnumBits(VARIANT const& value) {
		switch (value.vt) {
			case VT_I1: return (uint64_t)(int64_t)value.cVal;
			case VT_I2: return (uint64_t)(int64_t)value.iVal;
			case VT_I4: case VT_INT: return (uint64_t)(int64_t)value.lVal;
			case VT_I8: return (uint64_t)value.llVal;
			case VT_UI1: return value.bVal;
			case VT_UI2: return value.uiVal;
			case VT_UI4: case VT_UINT: return value.ulVal;
			case VT_UI8: return value.ullVal;
		}
		return 0;
	}
}

bool CMainFrame::DescribeType(uint32_t id, GhidraDecompiler::Type& type) const {
	using Kind = GhidraDecompiler::TypeKind;
	auto sym = m_Symbols.GetSymbolById(id);
	if (!sym)
		return false;
	type.Size = (uint32_t)sym.Length();
	switch (sym.Tag()) {
		case SymbolTag::BaseType:
			switch (sym.Simple()) {
				case SimpleType::Void: type.Kind = Kind::Void; break;
				case SimpleType::Char: case SimpleType::Char8: case SimpleType::WChar: case SimpleType::Char16: case SimpleType::Char32:
					type.Kind = Kind::Char;
					break;
				case SimpleType::Bool: type.Kind = Kind::Boolean; break;
				case SimpleType::Float: type.Kind = Kind::Float; break;
				case SimpleType::Int: case SimpleType::Int4B: case SimpleType::Hresult:
					type.Kind = Kind::Integer;
					break;
				case SimpleType::UInt: case SimpleType::UInt4B:
					type.Kind = Kind::Integer;
					type.Signed = false;
					break;
				default: type.Kind = Kind::Unknown; break;
			}
			break;

		case SymbolTag::PointerType:
			type.Kind = Kind::Pointer;		// a reference too
			if (auto target = sym.Type())
				type.Target = target.Id();
			break;

		case SymbolTag::ArrayType:
			type.Kind = Kind::Array;
			type.Count = sym.Count();
			if (auto element = sym.Type())
				type.Target = element.Id();
			break;

		case SymbolTag::UDT:
			type.Kind = sym.UdtKind() == UdtType::Union ? Kind::Union : Kind::Struct;
			type.Name = Narrow(sym.Name());
			// the data members (not static), the base classes and the vtable pointer
			for (auto const& child : sym.FindChildren()) {
				switch (child.Tag()) {
					case SymbolTag::Data:
						if (child.Location() == LocationKind::ThisRel) {
							if (auto ft = child.Type())
								type.Fields.push_back({ Narrow(child.Name()), (uint32_t)child.Offset(), ft.Id() });
						}
						else if (child.Location() == LocationKind::BitField) {
							if (auto ft = child.Type())
								type.Fields.push_back({ Narrow(child.Name()), (uint32_t)child.Offset(), ft.Id(), (uint32_t)child.Length(),
									child.BitPosition() });
						}
						break;
					case SymbolTag::BaseClass:
						if (!child.IsVirtual()) {
							auto base = child.Type() ? child.Type() : child;
							type.Fields.push_back({ "__base_" + Narrow(base.Name()), (uint32_t)child.Offset(), base.Id() });
						}
						break;
					case SymbolTag::VTable:
						if (auto vt = child.Type())
							type.Fields.push_back({ "__vftable", (uint32_t)child.Offset(), vt.Id() });
						break;
				}
			}
			break;

		case SymbolTag::Enum:
			type.Kind = Kind::Enum;
			type.Name = Narrow(sym.Name());
			if (auto underlying = sym.Type()) {
				auto simple = underlying.Simple();
				type.Signed = simple == SimpleType::Int || simple == SimpleType::Int4B || simple == SimpleType::Char;
			}
			for (auto const& value : sym.FindChildren(nullptr, SymbolTag::Data)) {
				auto v = value.Value();
				type.Values.emplace_back(Narrow(value.Name()), EnumBits(v));
				::VariantClear(&v);
			}
			break;

		case SymbolTag::Typedef:
			type.Kind = Kind::Typedef;
			type.Name = Narrow(sym.Name());
			if (auto target = sym.Type())
				type.Target = target.Id();
			break;

		case SymbolTag::FunctionType:
			type.Kind = Kind::Function;
			if (auto ret = sym.Type())
				type.Target = ret.Id();
			if (auto self = sym.ObjectPointerType())	// a member function: "this" first
				type.Parameters.push_back(self.Id());
			for (auto const& arg : sym.FindChildren(nullptr, SymbolTag::FunctionArgType)) {
				auto at = arg.Type();
				if (at && at.Tag() == SymbolTag::BaseType && at.Simple() == SimpleType::NoType)
					type.Variadic = true;	// "..."
				else if (at)
					type.Parameters.push_back(at.Id());
			}
			break;

		default:
			type.Kind = Kind::Unknown;
			break;
	}
	return true;
}

uint64_t CMainFrame::StubTarget(uint64_t va) const {
	uint8_t code[5];
	if (!ReadImage(va, code, sizeof(code)) || code[0] != 0xE9)
		return 0;
	int32_t displacement;
	memcpy(&displacement, code + 1, sizeof(displacement));
	return va + 5 + displacement;
}

// The name of a function from its symbol (a public symbol's undecorated), or the name of an export. An incremental linking
// stub ("@ILT+1234(name)") is the function it jumps to.
std::string CMainFrame::DecompilerFunctionName(uint64_t va) const {
	auto base = m_PE.GetImageBase();
	if (va < base || va - base > 0xFFFFFFFFULL)
		return {};
	auto name = ResolveRva((DWORD)(va - base));
	if (name.starts_with(L"@ILT+")) {
		auto open = name.find(L'('), close = name.rfind(L')');
		name = open != std::wstring::npos && close > open ? name.substr(open + 1, close - open - 1) : std::wstring();
	}
	if (name.empty() || name[0] == L'<' || name.find(L"+0x") != std::wstring::npos)
		return {};		// none, a marker ("<entry point>") or a place inside a function
	return Narrow(FunctionName(name));
}

// The prototype of a function the symbols have (with types: private symbols): its type, and the names of its parameters
// ("this" of a member function first). An incremental linking stub has the prototype of the function it jumps to.
bool CMainFrame::FunctionPrototype(uint64_t va, uint32_t& type, std::vector<std::string>& parameterNames) const {
	auto base = m_PE.GetImageBase();
	if (!m_Symbols || va < base || va - base > 0xFFFFFFFFULL)
		return false;
	long displacement = 0;
	auto function = m_Symbols.GetSymbolByRVA((DWORD)(va - base), SymbolTag::Function, &displacement);
	if (!function || displacement != 0) {
		auto target = StubTarget(va);
		if (target && target != va && target - base <= 0xFFFFFFFFULL) {
			function = m_Symbols.GetSymbolByRVA((DWORD)(target - base), SymbolTag::Function, &displacement);
			if (function && displacement != 0)
				return false;
		}
	}
	if (!function || displacement != 0)
		return false;
	auto ft = function.Type();
	if (!ft || ft.Tag() != SymbolTag::FunctionType)
		return false;
	type = ft.Id();
	for (auto const& data : function.FindChildren(nullptr, SymbolTag::Data)) {
		auto kind = data.Kind();
		if (kind == DataItemKind::ObjectPtr || kind == DataItemKind::Param)
			parameterNames.push_back(Narrow(data.Name()));
	}
	return true;
}

bool CMainFrame::X64Frame(uint32_t rva, uint32_t& size, uint32_t& frameRegister, int64_t& frameOffset) const {
	auto exceptions = m_PE.GetExceptions();
	if (!exceptions)
		return false;
	auto it = std::ranges::find_if(*exceptions, [&](auto const& e) { return e.RuntimeFuncEntry.BeginAddress == rva; });
	if (it == exceptions->end() || it->RuntimeFuncEntry.UnwindInfoAddress == 0)
		return false;
	auto base = m_PE.GetImageBase();
	uint64_t info = base + it->RuntimeFuncEntry.UnwindInfoAddress;
	uint8_t header[4];
	if (!ReadImage(info, header, sizeof(header)))
		return false;
	int version = header[0] & 7;
	int count = header[2];
	std::vector<uint16_t> codes(count);
	if (count && !ReadImage(info + 4, (uint8_t*)codes.data(), count * sizeof(uint16_t)))
		return false;

	// the codes are in the reverse order of the prologue: what is allocated before the frame register is set is after its code
	uint32_t total = 0, beforeFrame = 0;
	bool frameSet = false;
	for (int i = 0; i < count; ) {
		int op = (codes[i] >> 8) & 0xF, opInfo = codes[i] >> 12;
		uint32_t allocated = 0;
		int slots = 1;
		switch (op) {
			case 0: allocated = 8; break;													// UWOP_PUSH_NONVOL
			case 1:																			// UWOP_ALLOC_LARGE
				if (opInfo == 0) {
					slots = 2;
					allocated = i + 1 < count ? codes[i + 1] * 8u : 0;
				}
				else {
					slots = 3;
					allocated = i + 2 < count ? codes[i + 1] | ((uint32_t)codes[i + 2] << 16) : 0;
				}
				break;
			case 2: allocated = opInfo * 8 + 8; break;										// UWOP_ALLOC_SMALL
			case 3: frameSet = true; break;													// UWOP_SET_FPREG
			case 4: slots = 2; break;														// UWOP_SAVE_NONVOL
			case 5: slots = 3; break;														// UWOP_SAVE_NONVOL_FAR
			case 6: slots = version >= 2 ? 1 : 2; break;									// UWOP_EPILOG (2), UWOP_SAVE_XMM (1)
			case 7: slots = version >= 2 ? 1 : 3; break;									// UWOP_SPARE_CODE (2), UWOP_SAVE_XMM_FAR (1)
			case 8: slots = 2; break;														// UWOP_SAVE_XMM128
			case 9: slots = 3; break;														// UWOP_SAVE_XMM128_FAR
			case 10: allocated = opInfo ? 48 : 40; break;									// UWOP_PUSH_MACHFRAME
			default: return false;
		}
		total += allocated;
		if (frameSet && op != 3)
			beforeFrame += allocated;
		i += slots;
	}
	size = total;
	frameRegister = 0;
	frameOffset = 0;
	if (int reg = header[3] & 0xF; reg && frameSet) {
		// the unwind numbers of the registers (RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8...) as CodeView numbers
		static constexpr uint32_t registers[] = { 328, 330, 331, 329, 335, 334, 332, 333 };
		frameRegister = reg < 8 ? registers[reg] : 336 + (reg - 8);
		frameOffset = -(int64_t)beforeFrame + (header[3] >> 4) * 16;
	}
	return true;
}

// Only the locals the stack pointer (or the frame pointer) locates are given (not those in registers): with their offsets
// from the stack pointer at the entry, which the frame the prologue makes tells
bool CMainFrame::FunctionLocals(uint64_t va, std::vector<GhidraDecompiler::Local>& locals) const {
	auto base = m_PE.GetImageBase();
	if (!m_Symbols || va < base || va - base > 0xFFFFFFFFULL)
		return false;
	auto rva = (uint32_t)(va - base);
	long displacement = 0;
	auto function = m_Symbols.GetSymbolByRVA(rva, SymbolTag::Function, &displacement);
	if (!function || displacement != 0)
		return false;

	// the stack pointer at the entry, relative to the register that locals are relative to
	auto machine = m_PE.GetFileInfo()->IsPE64 ? m_PE.GetNTHeader()->NTHdr64.FileHeader.Machine : m_PE.GetNTHeader()->NTHdr32.FileHeader.Machine;
	std::unordered_map<uint32_t, int64_t> origins;
	switch (machine) {
		case IMAGE_FILE_MACHINE_AMD64:
		{
			uint32_t size, frameRegister;
			int64_t frameOffset;
			if (!X64Frame(rva, size, frameRegister, frameOffset))
				return false;
			origins[CV_AMD64_RSP] = -(int64_t)size;
			if (frameRegister)
				origins[frameRegister] = frameOffset;
			break;
		}
		case IMAGE_FILE_MACHINE_I386:
			// EBP after "push ebp, mov ebp, esp", and the frame that the symbols describe as one of EBP when there is none
			origins[CV_REG_EBP] = -4;
			origins[CV_ALLREG_VFRAME] = -4;
			break;
		case IMAGE_FILE_MACHINE_ARM64:
			// the frame of the symbols is all that the prologue allocates, the registers it saves too
			if (auto size = function.FrameSize())
				origins[CV_ARM64_SP] = -(int64_t)size;
			break;
		default:
			return false;
	}

	// those of the function and of its blocks (an inner block's local may be in the place of another's)
	std::function<void(DiaSymbol const&)> collect = [&](DiaSymbol const& parent) {
		for (auto const& child : parent.FindChildren()) {
			if (child.Tag() == SymbolTag::Block) {
				collect(child);
				continue;
			}
			if (child.Tag() != SymbolTag::Data || child.Kind() != DataItemKind::Local || child.Location() != LocationKind::RegRel)
				continue;
			auto origin = origins.find(child.RegisterId());
			auto type = child.Type();
			if (origin == origins.end() || !type)
				continue;
			locals.push_back({ Narrow(child.Name()), type.Id(), (int64_t)child.Offset() + origin->second });
		}
	};
	collect(function);
	return !locals.empty();
}

std::shared_ptr<CMainFrame::DecompilerSession> CMainFrame::Decompiler(std::wstring& error) {
	if (m_Decompiler)
		return m_Decompiler;

	auto machine = m_PE.GetFileInfo()->IsPE64 ? m_PE.GetNTHeader()->NTHdr64.FileHeader.Machine : m_PE.GetNTHeader()->NTHdr32.FileHeader.Machine;
	PCSTR target;
	switch (machine) {
		case IMAGE_FILE_MACHINE_AMD64: target = "x86:LE:64:default:windows"; break;
		case IMAGE_FILE_MACHINE_I386: target = "x86:LE:32:default:windows"; break;
		case IMAGE_FILE_MACHINE_ARM64: target = "AARCH64:LE:64:v8A:windows"; break;
		default:
			error = L"The decompiler handles x64, x86 and ARM64 code only.";
			return nullptr;
	}

	auto session = std::make_shared<DecompilerSession>();
	session->Frame = this;
	session->Window = m_hWnd;
	session->Target = target;
	// the language definitions are next to the executable
	WCHAR folder[MAX_PATH];
	::GetModuleFileName(nullptr, folder, _countof(folder));
	*wcsrchr(folder, L'\\') = 0;
	session->Languages = std::wstring(folder) + L"\\Decompiler\\languages";

	// Calls by name: of the functions the symbols know (asked for when a call is seen: a debug build has hundreds of
	// thousands), of the exports, and of the imports (through their entries in the import address table)
	auto base = m_PE.GetImageBase();
	if (auto exp = m_PE.GetExport()) {
		for (auto const& f : exp->Funcs)
			if (f.FuncRVA && f.ForwarderName.empty() && !f.FuncName.empty())
				session->Functions.emplace_back(base + f.FuncRVA, f.FuncName);
	}
	auto imports = m_PE.GetImport();
	auto delayImports = m_PE.GetDelayImport();
	if ((imports && !imports->empty()) || (delayImports && !delayImports->empty())) {
		bool pe64 = m_PE.GetFileInfo()->IsPE64;
		uint64_t imageSize = pe64 ? m_PE.GetNTHeader()->NTHdr64.OptionalHeader.SizeOfImage : m_PE.GetNTHeader()->NTHdr32.OptionalHeader.SizeOfImage;
		session->ExternalArea = base + ((imageSize + 0xFFFF) & ~0xFFFFULL);	// past the image
		// an import by ordinal: named after its module ("WS2_32_Ordinal_23")
		auto moduleOf = [](std::string const& moduleName) {
			auto module = moduleName.substr(0, moduleName.rfind('.'));
			for (auto& ch : module)
				if (!isalnum((unsigned char)ch))
					ch = '_';
			return module;
		};
		if (imports) {
			for (auto const& imp : *imports) {
				uint64_t slot = imp.ImportDesc.FirstThunk;
				if (slot == 0)
					continue;
				auto module = moduleOf(imp.ModuleName);
				auto dll = ShownModule(imp.ModuleName);
				for (auto const& fn : imp.ImportFunc) {
					auto name = fn.FuncName;
					if (name.empty()) {
						auto ordinal = pe64 ? fn.unThunk.Thunk64.u1.Ordinal & 0xFFFF : fn.unThunk.Thunk32.u1.Ordinal & 0xFFFF;
						name = module + "_Ordinal_" + std::to_string(ordinal);
					}
					session->Imports.push_back({ base + slot, name, dll });
					slot += pe64 ? 8 : 4;
				}
			}
		}
		// Delay loaded imports: a call through an entry of the delay load import address table (which points to the code
		// that loads the module, until it is loaded) is a call of the function
		if (delayImports) {
			for (auto const& imp : *delayImports) {
				auto const& desc = imp.DelayImpDesc;
				// the addresses of a descriptor of the oldest kind are virtual addresses, not relative
				uint64_t rebase = (desc.grAttrs & dlattrRva) ? 0 : base;
				if (desc.rvaIAT == 0 || desc.rvaIAT < rebase)
					continue;
				uint64_t slot = desc.rvaIAT - rebase, names = desc.rvaINT ? desc.rvaINT - rebase : 0;
				auto module = moduleOf(imp.ModuleName);
				auto dll = ShownModule(imp.ModuleName) + " (delay loaded)";
				for (auto const& fn : imp.DelayImpFunc) {
					auto name = fn.FuncName;
					if (name.empty() && names) {
						// the import name table has the ordinal
						uint64_t entry = 0;
						if (ReadImage(base + names, (uint8_t*)&entry, pe64 ? 8 : 4))
							name = module + "_Ordinal_" + std::to_string(entry & 0xFFFF);
					}
					if (!name.empty())
						session->Imports.push_back({ base + slot, name, dll });
					slot += pe64 ? 8 : 4;
					if (names)
						names += pe64 ? 8 : 4;
				}
			}
		}
	}
	// What is read in the sections that are not written is known (strings, constants), but for the import address table,
	// which the loader writes (calls through it are calls of the imports)
	{
		auto const& dirs = m_PE.GetFileInfo()->IsPE64 ? m_PE.GetNTHeader()->NTHdr64.OptionalHeader.DataDirectory : m_PE.GetNTHeader()->NTHdr32.OptionalHeader.DataDirectory;
		uint64_t iat = dirs[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress, iatEnd = iat + dirs[IMAGE_DIRECTORY_ENTRY_IAT].Size;
		for (auto const& sec : *m_PE.GetSecHeaders()) {
			auto const& h = sec.SecHdr;
			if (h.Characteristics & IMAGE_SCN_MEM_WRITE)
				continue;
			uint64_t start = h.VirtualAddress, end = start + std::max(h.Misc.VirtualSize, h.SizeOfRawData);
			auto add = [&](uint64_t from, uint64_t to) {
				if (to > from)
					session->ReadOnly.emplace_back(base + from, to - from);
			};
			if (iat < iatEnd && iat < end && iatEnd > start) {
				add(start, iat);
				add(iatEnd, end);
			}
			else
				add(start, end);
		}
	}
	// the types of the symbols (of private symbols: prototypes, locals, and the types of global variables)
	if (m_Symbols) {
		session->Symbols = true;
		for (auto const& sym : m_Symbols.FindChildren(nullptr, SymbolTag::Data)) {
			if (sym.Location() != LocationKind::Static)
				continue;
			auto type = sym.Type();
			auto rva = sym.RelativeVirtualAddress();
			if (type && rva)
				session->Globals.push_back({ base + rva, Narrow(sym.Name()), type.Id() });
		}
	}
	m_Decompiler = std::move(session);
	return m_Decompiler;
}

GhidraDecompiler* CMainFrame::DecompilerSession::Open(std::string& error) {
	if (Decompiler)
		return Decompiler.get();
	if (!Error.empty()) {
		error = Error;
		return nullptr;
	}
	auto fail = [&](std::string const& reason) -> GhidraDecompiler* {
		error = Error = "The decompiler could not start: " + reason;
		return nullptr;
	};
	std::string reason;
	if (!GhidraDecompiler::Initialize(Languages, reason))
		return fail(reason);
	auto decompiler = std::make_unique<GhidraDecompiler>(Target, [this](uint64_t va, uint8_t* buffer, size_t size) {
		std::shared_lock lock(Image);
		return !Closed && Frame->ReadImage(va, buffer, size);
		});
	if (!decompiler->Open(reason))
		return fail(reason);

	decompiler->SetNameSource([this](uint64_t va) {
		std::string name;
		Ask([&] { name = Frame->DecompilerFunctionName(va); });
		return name;
		});
	for (auto const& [va, name] : Functions)
		decompiler->AddFunction(va, name);
	for (auto const& [va, size] : ReadOnly)
		decompiler->AddReadOnly(va, size);
	if (ExternalArea) {
		decompiler->SetExternalArea(ExternalArea);
		for (auto const& imp : Imports)
			decompiler->AddImport(imp.Slot, imp.Name, imp.Module);
	}
	if (Symbols) {
		decompiler->SetTypeSource([this](uint32_t id, GhidraDecompiler::Type& type) {
			bool known = false;
			Ask([&] { known = !Stale && Frame->DescribeType(id, type); });
			return known;
			});
		decompiler->SetPrototypeSource([this](uint64_t va, uint32_t& type, std::vector<std::string>& names) {
			bool known = false;
			Ask([&] { known = !Stale && Frame->FunctionPrototype(va, type, names); });
			return known;
			});
		decompiler->SetLocalSource([this](uint64_t va, std::vector<GhidraDecompiler::Local>& locals) {
			bool known = false;
			Ask([&] { known = !Stale && Frame->FunctionLocals(va, locals); });
			return known;
			});
		for (auto const& g : Globals)
			decompiler->AddGlobal(g.Address, g.Name, g.Type);
	}
	Decompiler = std::move(decompiler);
	return Decompiler.get();
}

bool CMainFrame::DecompilerSession::Ask(std::function<void()> const& ask) {
	if (Closed)
		return false;
	// (the frame's thread closes the file: what runs there sees whether it is closed)
	std::function<void()> call = [&] {
		if (!Closed)
			ask();
	};
	return ::SendMessage(Window, WM_DECOMPILER_ASK, 0, (LPARAM)&call) != 0;
}

LRESULT CMainFrame::OnDecompilerAsk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	(*reinterpret_cast<std::function<void()>*>(lParam))();
	return TRUE;
}

void CMainFrame::CloseDecompiler(bool closed) {
	if (closed) {
		for (auto& [id, pending] : m_PendingDecompiles)
			*pending.Cancel = true;
		m_PendingDecompiles.clear();
		UpdateDecompilerStatus();
	}
	if (!m_Decompiler)
		return;
	if (closed) {
		std::unique_lock lock(m_Decompiler->Image);
		m_Decompiler->Closed = true;
	}
	else
		m_Decompiler->Stale = true;
	// the work in progress (and the work posted) has references of its own: the last one goes on the decompiler's thread
	DecompilerThread::Get().Post([session = std::move(m_Decompiler)]() mutable { session.reset(); });
}

void CMainFrame::DecompileAsync(uint64_t address, std::wstring const& name, bool lines, HWND hView, std::function<void(Decompiled&)> done) {
	auto id = ++m_LastDecompile;
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	m_PendingDecompiles[id] = { hView, name.empty() ? std::format(L"0x{:X}", address) : name, std::move(done), cancel };
	UpdateDecompilerStatus();
	DecompilerThread::Get().Post([session = m_Decompiler, hWnd = m_hWnd, id, address, name = std::string(CW2A(name.c_str())), lines, cancel] {
		if (*cancel)
			return;		// its view was closed while it waited
		auto result = std::make_unique<Decompiled>();
		result->Id = id;
		std::string error;
		if (auto decompiler = session->Open(error))
			result->Ok = decompiler->Decompile(address, name, result->Code, result->Error, &result->Functions, lines ? &result->Lines : nullptr);
		else
			result->Error = error;
		if (!session->Closed && ::PostMessage(hWnd, WM_DECOMPILED, 0, (LPARAM)result.get()))
			result.release();
		});
}

void CMainFrame::CancelDecompiles(HWND hView) {
	bool any = false;
	for (auto it = m_PendingDecompiles.begin(); it != m_PendingDecompiles.end(); ) {
		if (it->second.View == hView) {
			*it->second.Cancel = true;
			it = m_PendingDecompiles.erase(it);
			any = true;
		}
		else
			++it;
	}
	if (any)
		UpdateDecompilerStatus();
}

// The function that is decompiled now (the decompiler's thread takes them in order), and how many wait
void CMainFrame::UpdateDecompilerStatus() {
	if (m_PendingDecompiles.empty()) {
		SetStatusText(DecompilerPane, L"");
		return;
	}
	auto first = std::ranges::min_element(m_PendingDecompiles, {}, [](auto const& p) { return p.first; });
	auto text = std::format(L"Decompiling {}...", first->second.Name);
	if (m_PendingDecompiles.size() > 1)
		text += std::format(L" (+{} more)", m_PendingDecompiles.size() - 1);
	SetStatusText(DecompilerPane, text.c_str());
}

LRESULT CMainFrame::OnDecompiled(UINT, WPARAM, LPARAM lParam, BOOL&) {
	std::unique_ptr<Decompiled> result(reinterpret_cast<Decompiled*>(lParam));
	auto it = m_PendingDecompiles.find(result->Id);
	if (it == m_PendingDecompiles.end())
		return 0;
	auto pending = std::move(it->second);
	m_PendingDecompiles.erase(it);
	UpdateDecompilerStatus();
	if (::IsWindow(pending.View) && m_Views2.contains(pending.View))
		pending.Done(*result);
	return 0;
}

bool CMainFrame::DecompileFunction(uint64_t address) {
	if (!m_PE)
		return false;
	std::wstring error;
	auto decompiler = Decompiler(error);
	if (!decompiler) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}
	auto base = m_PE.GetImageBase();
	if (address < base || address - base > 0xFFFFFFFFULL)
		return false;
	auto rva = (uint32_t)(address - base);

	auto itemType = TreeItemWithIndex(TreeItemType::Decompiled, (int64_t)rva << ItemShift);
	if (auto it = m_Views.find(itemType); it != m_Views.end()) {
		ShowView(it->second->GetHTreeItem());
		return true;
	}

	// named as its disassembly is, and next to it
	std::wstring name;
	auto parent = FunctionPlace(rva, name);

	auto hParent = PlaceItem(parent);
	if (!hParent)
		return false;
	auto title = std::format(L"{} (C)", name.empty() ? std::format(L"0x{:X}", address) : name);

	// the view is there at once: the code comes when the decompiler's thread is done with it
	RecordNavigation();
	auto view = new CScintillaView(this, m_PE, title.c_str());
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return false;
	}
	view->SetLanguage(LexLanguage::C);
	view->SetText(DecompilingText(address, name).c_str());
	view->SetFunctionAddress(address);
	view->GetCtrl().SetReadOnly(true);
	view->SetDeleteFromTree(true);
	DecompileAsync(address, name, false, view->GetHwnd(), [=](Decompiled& result) {
		auto& ctrl = view->GetCtrl();
		ctrl.SetReadOnly(false);
		if (result.Ok) {
			view->SetText(result.Code.c_str());
			view->SetFunctionLinks(FunctionLinks(result.Functions));
		}
		else
			view->SetText(NotDecompiledText(address, result.Error).c_str());
		ctrl.SetReadOnly(true);
		ctrl.EmptyUndoBuffer();
		});

	auto image = GetIconIndex(IDI_FUNCTION);
	auto hItem = InsertTreeItem(m_Tree, title.c_str(), image, itemType, hParent, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return true;
}

// What the view of a function's C shows while it is decompiled, and if it could not be
std::string CMainFrame::DecompilingText(uint64_t address, std::wstring const& name) {
	return std::format("/* Decompiling {}... */\n", name.empty() ? std::format("0x{:X}", address) : std::string(CW2A(name.c_str())));
}

std::string CMainFrame::NotDecompiledText(uint64_t address, std::string const& error) {
	return std::format("/* 0x{:X} could not be decompiled: {} */\n", address, error);
}

// Where the views of a function go, and its name (without its signature; empty if it has none): an export under the
// exports (named as the exports view names it), any other function under Disassembly
TreeItemType CMainFrame::FunctionPlace(uint32_t rva, std::wstring& name) const {
	auto parent = TreeItemType::DisassemblyFunctions;
	name.clear();
	if (auto exp = m_PE.GetExport(); exp && FindTreeItem(TreeItemType::DirectoryExports)) {
		auto it = std::ranges::find_if(exp->Funcs, [&](auto const& f) { return f.FuncRVA == rva && f.ForwarderName.empty(); });
		if (it != exp->Funcs.end()) {
			parent = TreeItemType::DirectoryExports;
			name = std::wstring(it->FuncName.begin(), it->FuncName.end());
		}
	}
	if (name.empty())
		name = ResolveRva(rva);
	if (name.find(L'+') != std::wstring::npos)
		name.clear();	// inside a function
	name = name.empty() ? std::wstring() : FunctionName(name);
	return parent;
}

// The tree item of a place of functions' views (Disassembly is made when it is first needed)
HTREEITEM CMainFrame::PlaceItem(TreeItemType parent) {
	if (parent == TreeItemType::DisassemblyFunctions && !FindTreeItem(parent)) {
		auto hRoot = FindTreeItem(TreeItemType::Image);
		if (!hRoot)
			return nullptr;
		InsertTreeItem(m_Tree, L"Disassembly", GetTreeIcon(IDI_BINARY), parent, hRoot);
	}
	return FindTreeItem(parent);
}

// The functions decompiled code refers to, for its context menu: those in the image (an import is not), an incremental
// linking stub as the function it jumps to
std::unordered_map<std::wstring, uint64_t> CMainFrame::FunctionLinks(GhidraDecompiler::FunctionRefs const& functions) const {
	std::unordered_map<std::wstring, uint64_t> links;
	auto base = m_PE.GetImageBase();
	auto pe64 = m_PE.GetFileInfo()->IsPE64;
	uint64_t imageSize = pe64 ? m_PE.GetNTHeader()->NTHdr64.OptionalHeader.SizeOfImage : m_PE.GetNTHeader()->NTHdr32.OptionalHeader.SizeOfImage;
	for (auto const& [fname, va] : functions) {
		if (va < base || va - base >= imageSize)
			continue;
		auto target = StubTarget(va);
		links.try_emplace(std::wstring(CA2W(fname.c_str(), CP_UTF8)), target ? target : va);
	}
	return links;
}

// The bytes of the code at an address (as much as the disassembly of a function examines); false (and the user is
// told why) if the address is not in code that the file has
bool CMainFrame::FunctionCode(uint64_t address, std::span<const std::byte>& code) {
	auto base = m_PE.GetImageBase();
	if (address < base || address - base > 0xFFFFFFFFULL)
		return false;
	auto rva = (uint32_t)(address - base);
	PESectionHeader const* host = nullptr;
	for (auto const& s : *m_PE.GetSecHeaders()) {
		auto& h = s.SecHdr;
		if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData)) {
			host = &s;
			break;
		}
	}
	if (!host || !(host->SecHdr.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))) {
		AtlMessageBox(m_hWnd, std::format(L"0x{:X} is not in a code section.", address).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}
	auto& h = host->SecHdr;
	if (rva - h.VirtualAddress >= h.SizeOfRawData) {
		AtlMessageBox(m_hWnd, std::format(L"0x{:X} is not backed by data in the file.", address).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}
	auto offset = h.PointerToRawData + (rva - h.VirtualAddress);
	// the disassembly stops at the end of the function: this is only the most that is examined
	auto size = std::min<uint64_t>(0x2000, (uint64_t)h.PointerToRawData + h.SizeOfRawData - offset);
	size = std::min<uint64_t>(size, m_PE.GetFileSize() - offset);
	code = m_PE.GetSpan(offset, (uint32_t)size);
	return true;
}

bool CMainFrame::ShowSideBySide(uint64_t address) {
	if (!m_PE)
		return false;
	auto base = m_PE.GetImageBase();
	if (address < base || address - base > 0xFFFFFFFFULL)
		return false;
	auto rva = (uint32_t)(address - base);
	auto itemType = TreeItemWithIndex(TreeItemType::SideBySide, (int64_t)rva << ItemShift);
	if (auto it = m_Views.find(itemType); it != m_Views.end()) {
		ShowView(it->second->GetHTreeItem());
		return true;
	}

	auto arch = ArchOf(m_PE);
	std::wstring error;
	auto decompiler = Decompiler(error);
	if (!decompiler || !arch) {
		AtlMessageBox(m_hWnd, decompiler ? L"The code of this machine is not disassembled (x86, x64 and ARM64 are)" : error.c_str(),
			IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}
	std::span<const std::byte> asmCode;
	if (!FunctionCode(address, asmCode))
		return false;

	std::wstring name;
	auto parent = FunctionPlace(rva, name);
	auto hParent = PlaceItem(parent);
	if (!hParent)
		return false;
	auto title = std::format(L"{} (Side by Side)", name.empty() ? std::format(L"0x{:X}", address) : name);

	RecordNavigation();
	auto view = new CSideBySideView(this, m_PE, title.c_str());
	if (nullptr == view->DoCreate(m_Tabs)) {
		ATLASSERT(false);
		return false;
	}
	// the disassembly at once, the C when the decompiler's thread is done with it
	view->SetAsm(asmCode, address, *arch);
	view->SetC(DecompilingText(address, name), {}, {});
	view->SetDeleteFromTree(true);
	DecompileAsync(address, name, true, view->GetHwnd(), [=](Decompiled& result) {
		if (result.Ok)
			view->SetC(result.Code, std::move(result.Lines), FunctionLinks(result.Functions));
		else
			view->SetC(NotDecompiledText(address, result.Error), {}, {});
		});

	auto image = GetIconIndex(IDI_FUNCTION);
	auto hItem = InsertTreeItem(m_Tree, title.c_str(), image, itemType, hParent, TVI_SORT);
	m_Tree.EnsureVisible(hItem);
	view->SetHTreeItem(hItem);
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
	m_Views.insert({ itemType, view });
	m_Views2.insert({ view->GetHwnd(), itemType });
	RecordNavigation();
	return true;
}

LRESULT CMainFrame::OnSideBySide(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_PE)
		return 0;
	CDisassembleFunctionDlg dlg(m_DisassembleFunction, L"Disassembly and C Side by Side", L"Show");
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	uint64_t address;
	std::wstring error;
	if (!ResolveFunction(m_DisassembleFunction, address, error)) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	ShowSideBySide(address);
	return 0;
}

// A view (or a window in one: a side of a side-by-side view) is replaced by the one that 'open' brings up
bool CMainFrame::ReplaceView(HWND hView, std::function<bool()> const& open) {
	auto pageOf = [&](HWND hWnd) {
		for (int i = 0; i < m_Tabs.GetPageCount(); i++)
			if (auto hPage = m_Tabs.GetPageHWND(i); hPage == hWnd || ::IsChild(hPage, hWnd))
				return i;
		return -1;
	};
	if (auto page = pageOf(hView); page >= 0)
		hView = m_Tabs.GetPageHWND(page);
	if (pageOf(hView) < 0)
		return open();
	if (!open())
		return false;
	int page = m_Tabs.GetActivePage();
	if (page < 0 || m_Tabs.GetPageHWND(page) == hView)
		return true;		// the view itself (it is what was asked for): it stays
	// into the place of the view (the new one is added last), then the view goes
	m_Tabs.MoveToPlaceOf(page, pageOf(hView));
	int old = pageOf(hView);
	CloseTab(old);
	m_Tabs.RemovePage(old);
	return true;
}

LRESULT CMainFrame::OnDisassembleFunction(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_PE && !m_Elf)
		return 0;
	CDisassembleFunctionDlg dlg(m_DisassembleFunction);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;

	uint64_t address;
	std::wstring error;
	if (!ResolveFunction(m_DisassembleFunction, address, error)) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	if (m_Elf)
		ShowElfAddress(address, true);
	else
		DisassembleFunction(address);
	return 0;
}

bool CMainFrame::DisassembleFunction(uint64_t address) {
	if (!m_PE)
		return false;
	auto base = m_PE.GetImageBase();
	if (address < base || address - base > 0xFFFFFFFFULL)
		return false;

	auto rva = (uint32_t)(address - base);
	std::span<const std::byte> code;
	if (!FunctionCode(address, code))
		return false;

	// an export goes under the exports directory (named as the exports view names it); any other function under a node of its own
	std::wstring name;
	auto parent = TreeItemType::DisassemblyFunctions;
	if (auto exp = m_PE.GetExport(); exp && FindTreeItem(TreeItemType::DirectoryExports)) {
		auto it = std::ranges::find_if(exp->Funcs, [&](auto const& f) { return f.FuncRVA == rva && f.ForwarderName.empty(); });
		if (it != exp->Funcs.end()) {
			parent = TreeItemType::DirectoryExports;
			name = std::wstring(it->FuncName.begin(), it->FuncName.end());
		}
	}
	if (name.empty())
		name = ResolveRva(rva);
	auto title = name.empty() || name.find(L'+') != std::wstring::npos ? std::format(L"Code at 0x{:X}", address) : name;

	if (parent == TreeItemType::DisassemblyFunctions && !FindTreeItem(parent)) {
		auto hRoot = FindTreeItem(TreeItemType::Image);
		if (!hRoot)
			return false;
		InsertTreeItem(m_Tree, L"Disassembly", GetTreeIcon(IDI_BINARY), parent, hRoot);
	}
	RecordNavigation();
	if (CreateAssemblyView(code, address, rva, title.c_str(), parent)) {
		RecordNavigation();
		return true;
	}
	if (auto hFunctions = FindTreeItem(TreeItemType::DisassemblyFunctions); hFunctions && !m_Tree.GetChildItem(hFunctions))
		m_Tree.DeleteItem(hFunctions);
	return false;
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

//
// VirusTotal
//

void CMainFrame::CancelVirusTotal() {
	if (m_VtCancel)
		*m_VtCancel = true;	// the thread stops at its next step, and what it posts is ignored
	m_VtCancel.reset();
	++m_VtGeneration;
	m_Vt = {};
}

// The key from the settings, else from the environment, else (if 'ask') from the user, who is then asked to keep it.
std::wstring CMainFrame::GetVirusTotalKey(bool ask) {
	auto& settings = AppSettings::Get();
	auto key = vt::UnprotectKey(settings.VirusTotalApiKey());
	if (vt::LooksLikeKey(key))
		return key;
	if (key = vt::KeyFromEnvironment(); vt::LooksLikeKey(key))
		return key;
	if (!ask)
		return L"";

	CVtKeyDlg dlg;
	if (dlg.DoModal(m_hWnd) != IDOK)
		return L"";
	settings.VirusTotalApiKey(vt::ProtectKey(dlg.GetKey()));
	return dlg.GetKey();
}

LRESULT CMainFrame::OnVirusTotalKey(WORD, WORD, HWND, BOOL&) {
	auto& settings = AppSettings::Get();
	CVtKeyDlg dlg(vt::UnprotectKey(settings.VirusTotalApiKey()));
	if (dlg.DoModal(m_hWnd) == IDOK)
		settings.VirusTotalApiKey(vt::ProtectKey(dlg.GetKey()));
	return 0;
}

LRESULT CMainFrame::OnVirusTotal(WORD, WORD, HWND, BOOL&) {
	if (!m_PE || m_Vt.Running())
		return 0;

	// sending a file to somebody else is the user's decision
	auto path = std::wstring(m_PE.GetPath());
	auto text = std::format(L"Scan {} with VirusTotal?\n\n"
		L"VirusTotal is asked first if it already knows the file (by its SHA-256 hash), and nothing is sent then. "
		L"If it does not, the file itself is uploaded, and VirusTotal shares uploaded files with its customers and the security community.\n\n"
		L"Do not upload files that are confidential.", path.substr(path.find_last_of(L'\\') + 1));
	if (AtlMessageBox(m_hWnd, text.c_str(), L"Upload to VirusTotal", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
		return 0;

	auto key = GetVirusTotalKey(true);
	if (key.empty())
		return 0;

	CancelVirusTotal();
	m_VtCancel = std::make_shared<std::atomic_bool>(false);
	vt::Status start;
	start.Step = vt::Phase::Hashing;
	start.Message = L"Starting...";
	m_Vt = start;
	if (auto it = m_Views.find(TreeItemType::Image); it != m_Views.end()) {
		it->second->SetVirusTotalStatus(m_Vt);
		ShowView(TreeItemType::Image, it->second->GetHTreeItem());
	}
	UpdateUI();

	// the thread has its own copies of everything: the window and the file may be gone when it finishes
	std::thread([hWnd = m_hWnd, generation = m_VtGeneration, cancel = m_VtCancel, path = std::wstring(m_PE.GetPath()), key] {
		vt::Scan(path, key, *cancel, [&](vt::Status const& status) {
			auto copy = new vt::Status(status);
			if (!::PostMessage(hWnd, WM_VT_STATUS, generation, (LPARAM)copy))
				delete copy;
		});
	}).detach();
	return 0;
}

LRESULT CMainFrame::OnVirusTotalStatus(UINT, WPARAM generation, LPARAM lParam, BOOL&) {
	std::unique_ptr<vt::Status> status((vt::Status*)lParam);
	if ((uint32_t)generation != m_VtGeneration || !m_PE)
		return 0;	// the scan of a file that is not open any more

	m_Vt = *status;
	if (auto it = m_Views.find(TreeItemType::Image); it != m_Views.end())
		it->second->SetVirusTotalStatus(m_Vt);
	if (!m_Vt.Running())
		m_VtCancel.reset();
	UpdateUI();
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

// All the symbols, and those of a kind under them (in a list of their own, for searching and filtering): a kind
// the symbols have none of has no node
void CMainFrame::InsertSymbolsNode(HTREEITEM hRoot) {
	auto hSymbols = InsertTreeItem(m_Tree, L"Symbols", GetTreeIcon(IDI_SYMBOLS), TreeItemType::Symbols, hRoot);
	struct {
		PCWSTR Name;
		UINT Icon;
		TreeItemType Type;
		SymViewType Kind;
	} const kinds[] = {
		{ L"Class", IDI_CLASS, TreeItemType::SymbolsClasses, SymViewType::Class },
		{ L"Struct", IDI_TYPE, TreeItemType::SymbolsStructs, SymViewType::Struct },
		{ L"Function", IDI_FUNCTION, TreeItemType::SymbolsFunctions, SymViewType::Function },
		{ L"Enum", IDI_ENUM, TreeItemType::SymbolsEnums, SymViewType::Enum },
	};
	CWaitCursor wait;
	for (auto& k : kinds)
		if (CSymbolsView::HasSymbols(m_Symbols, k.Kind))
			InsertTreeItem(m_Tree, k.Name, GetTreeIcon(k.Icon), k.Type, hSymbols);
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
	CloseDecompiler(false);	// made again when it is used: with the names and types the symbols have (the work in progress ends as it began)
	SetStatusText(0, L"Symbols loaded");
	if (auto root = FindTreeItem(TreeItemType::Image); root && !FindTreeItem(TreeItemType::Symbols))
		InsertSymbolsNode(root);
	RefreshViews();
	SearchTargetChanged();
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
	UIEnable(ID_NAV_GOTO, m_PE || m_Elf);
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
	if (!m_PE && !m_Elf)
		return 0;
	CGoToDlg dlg(m_GoTo);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	if (m_Elf) {
		// an ELF file has addresses (an RVA is an address too), and offsets in the file
		bool shown;
		if (m_GoTo.Kind == GoToKind::FileOffset) {
			shown = false;
			if (m_GoTo.Disassemble) {
				// the code at the offset: in an executable section
				auto const& sections = m_Elf.Sections();
				for (int i = 1; i < (int)sections.size() && !shown; i++) {
					auto const& s = sections[i];
					if ((s.Flags & 4) && s.Type != 8 && m_GoTo.Value >= s.Offset && m_GoTo.Value < s.Offset + s.Size)
						shown = ShowElfSection(i, (int64_t)(m_GoTo.Value - s.Offset), true);
				}
			}
			if (!shown)
				shown = ShowElfFileOffset((int64_t)m_GoTo.Value);
		}
		else
			shown = ShowElfAddress(m_GoTo.Value, m_GoTo.Disassemble);
		if (!shown)
			AtlMessageBox(m_hWnd, std::format(L"0x{:X} is not in the file", m_GoTo.Value).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}

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
	// (empty until they are found: views that show them are told when they are, by XrefsChanged)
	if (m_PE && !m_XrefsReady && !m_XrefThread.joinable())
		StartXrefs();
	return m_Xrefs;
}

void CMainFrame::StartXrefs() {
	CancelXrefs();
	if (!m_PE)
		return;
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	m_XrefCancel = cancel;
	SetStatusText(XrefsPane, L"Finding cross-references...");
	m_XrefThread = std::thread([pe = &m_PE, cancel, hWnd = m_hWnd, generation = ++m_XrefGeneration] {
		::SetThreadDescription(::GetCurrentThread(), L"Cross-references");
		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		auto map = std::make_unique<XrefMap>();
		bool built = map->Build(*pe, cancel.get());
		if (*cancel)
			return;
		if (::PostMessage(hWnd, WM_XREFS_BUILT, generation, (LPARAM)(built ? map.get() : nullptr)) && built)
			map.release();
		});
}

void CMainFrame::CancelXrefs() {
	if (m_XrefThread.joinable()) {
		*m_XrefCancel = true;
		m_XrefThread.join();	// (it looks at the flag often: a moment)
	}
	++m_XrefGeneration;		// a result that was posted is not this file's
	m_XrefCancel.reset();
	m_Xrefs.Clear();
	m_XrefsReady = false;
	m_AfterXrefs.clear();
	if (m_StatusBar)
		SetStatusText(XrefsPane, L"");
}

bool CMainFrame::WithXrefs(std::function<void()> action) {
	GetXrefs();
	if (m_XrefsReady) {
		action();
		return true;
	}
	m_AfterXrefs.push_back(std::move(action));
	return false;
}

LRESULT CMainFrame::OnXrefsBuilt(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	std::unique_ptr<XrefMap> map(reinterpret_cast<XrefMap*>(lParam));
	if ((uint32_t)wParam != m_XrefGeneration)
		return 0;	// of a file that was closed
	if (m_XrefThread.joinable())
		m_XrefThread.join();
	if (map)
		m_Xrefs = std::move(*map);
	m_XrefsReady = true;
	SetStatusText(XrefsPane, m_Xrefs.Built() ? std::format(L"{} cross-references", m_Xrefs.Count()).c_str() : L"");
	for (auto const& [type, view] : m_Views)
		view->XrefsChanged();
	auto actions = std::move(m_AfterXrefs);
	for (auto& action : actions)
		action();
	return 0;
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
