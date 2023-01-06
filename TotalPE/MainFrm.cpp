// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "AboutDlg.h"
#include "PEImageView.h"
#include "MainFrm.h"
#include <IconHelper.h>
#include "SecurityHelper.h"
#include <ToolbarHelper.h>
#include "PEStrings.h"
#include "SectionsView.h"
#include "DataDirectoriesView.h"
#include "ExportsView.h"
#include "ImportsView.h"
#include "AppSettings.h"
#include "ResourcesView.h"
#include "StructView.h"
#include "IconsView.h"
#include "TextView.h"
#include "StringMessageTableView.h"
#include "VersionView.h"
#include "AcceleratorTableView.h"
#include "ExceptionsView.h"
#include "DebugView.h"

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

bool CMainFrame::OnTreeDoubleClick(HWND, HTREEITEM hItem) {
	return ShowView(hItem);
}

void CMainFrame::UpdateUI() {
	auto const fi = m_PE->GetFileInfo();
	UIEnable(ID_VIEW_EXPORTS, fi && fi->HasExport);
	UIEnable(ID_VIEW_IMPORTS, fi && fi->HasImport);
	UIEnable(ID_VIEW_RESOURCES, fi && fi->HasResource);
	UIEnable(ID_VIEW_DEBUG, fi && fi->HasDebug);
	UIEnable(ID_VIEW_DIRECTORIES, fi && fi->HasDataDirs);
	UIEnable(ID_VIEW_SECTIONS, fi && fi->HasSections);
	UIEnable(ID_PE_SECURITY, fi && fi->HasSecurity);
	UIEnable(ID_FILE_CLOSE, fi != nullptr);
	UIEnable(ID_FILE_SAVE, fi != nullptr);
	UIEnable(ID_VIEW_MANIFEST, fi && m_HasManifest);
	UIEnable(ID_VIEW_VERSION, fi && m_HasVersion);
	UIEnable(ID_FILE_OPENINANEWWINDOW, fi != nullptr);
	UIEnable(ID_EDIT_COPY, FALSE);
	UIEnable(ID_EDIT_FIND, fi && m_Tabs.GetActivePage() >= 0);
}

int CMainFrame::GetResourceIconIndex(WORD resType) const {
	return ResourceTypeToIcon(resType);
}

void CMainFrame::InitMenu(HMENU hMenu) {
	struct {
		int id;
		UINT icon;
		HICON hIcon{ nullptr };
	} const commands[] = {
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
		{ ID_VIEW_DEBUG, IDI_DEBUG },
		{ ID_EDIT_FIND, IDI_FIND },
		{ ID_EDIT_FIND_PREVIOUS, IDI_FIND_PREV },
		{ ID_EDIT_FIND_NEXT, IDI_FIND_NEXT },
		{ ID_FILE_RUNASADMINISTRATOR, 0, IconHelper::GetShieldIcon() },
		{ ID_FILE_CLOSE, IDI_CLOSE },
		{ ID_WINDOW_CLOSE, IDI_WIN_CLOSE },
		{ ID_WINDOW_CLOSE_ALL, IDI_WIN_CLOSEALL },

	};
	for (auto& cmd : commands) {
		if (cmd.icon)
			AddCommand(cmd.id, cmd.icon);
		else
			AddCommand(cmd.id, cmd.hIcon);
	}
}

TreeItemType CMainFrame::GetIndex(TreeItemType value) {
	return TreeItemType();
}

DiaSymbol CMainFrame::GetSymbolForName(PCWSTR mod, PCWSTR name) const {
	static std::unordered_map<std::wstring, DiaSession> symbols;
	auto it = symbols.find(mod);
	if (it == symbols.end()) {
		DiaSession session;
		WCHAR path[MAX_PATH];
		::GetSystemDirectory(path, _countof(path));
		wcscat_s(path, L"\\");
		wcscat_s(path, mod);
		if (!session.OpenImage(path))
			return DiaSymbol::Empty;
		it = symbols.insert({ mod, std::move(session) }).first;
	}

	auto& session = it->second;
	auto sym = session.FindChildren(name);
	return sym.empty() ? DiaSymbol::Empty : sym[0];
}

bool CMainFrame::AddToolBar(HWND tb) {
	return UIAddToolBar(tb);
}

bool CMainFrame::RemoveToolBar(HWND hWndToolBar) {
	ATLASSERT(::IsWindow(hWndToolBar));
	TBBUTTONINFO tbbi = { sizeof(TBBUTTONINFO), TBIF_COMMAND | TBIF_STYLE | TBIF_BYINDEX };

	// remove toolbar buttons
	for (int uItem = 0; ::SendMessage(hWndToolBar, TB_GETBUTTONINFO, uItem, (LPARAM)&tbbi) != -1; uItem++) {
		if (tbbi.fsStyle ^ BTNS_SEP)
			UIRemoveElement<CDynamicUpdateUI<CMainFrame>::UPDUI_TOOLBAR>(tbbi.idCommand);
	}

	return true;	// (CUpdateUIBase::UIAddToolBar(hWndToolBar) != FALSE);
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	DragAcceptFiles();
	auto& settings = AppSettings::Get();
	s_Frames++;
	if (s_Frames == 1) {
		//InitDarkTheme();
		if (settings.LoadFromKey(L"SOFTWARE\\ScorpioSoftware\\TotalPE")) {
			m_RecentFiles.Set(settings.RecentFiles());
			UpdateRecentFilesMenu();
		}
	}

	CreateSimpleStatusBar();
	m_StatusBar.SubclassWindow(m_hWndStatusBar);
	int parts[] = { 200, 400, 600, 800, 1000 };
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
		{ 0 },
		{ ID_EDIT_FIND, IDI_FIND},
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	auto tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons));

	AddSimpleReBarBand(tb);
	UIAddToolBar(tb);

	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);
	m_Tabs.m_bTabCloseButton = false;
	m_Tabs.Create(m_Splitter, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT);

	m_Splitter.SetSplitterPanes(m_Tree, m_Tabs);
	m_Splitter.SetSplitterPosPct(15);

	// register object for message filtering and idle updates
	auto pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	CMenuHandle menuMain = GetMenu();

	const int WindowMenuPosition = 5;
	m_Tabs.SetWindowMenu(menuMain.GetSubMenu(WindowMenuPosition));

	CMenuHandle hMenu = GetMenu();
	if (SecurityHelper::IsRunningElevated()) {
		hMenu.GetSubMenu(0).DeleteMenu(ID_FILE_RUNASADMINISTRATOR, MF_BYCOMMAND);
		hMenu.GetSubMenu(0).DeleteMenu(0, MF_BYPOSITION);
		CString text;
		GetWindowText(text);
		SetWindowText(text + L" (Administrator)");
	}

	InitMenu(hMenu);
	AddMenu(hMenu);
	UIAddMenu(hMenu);
	AddMenu(IDR_CONTEXT);
	UIAddMenu(IDR_CONTEXT);

	UISetCheck(ID_VIEW_STATUS_BAR, 1);
	UpdateUI();

	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	if (--s_Frames == 0) {
		WINDOWPLACEMENT wp{ sizeof(wp) };
		GetWindowPlacement(&wp);
		auto& settings = AppSettings::Get();
		settings.MainWindowPlacement(wp);
		settings.Save();

		// unregister message filtering and idle updates
		CMessageLoop* pLoop = _Module.GetMessageLoop();
		ATLASSERT(pLoop != nullptr);
		pLoop->RemoveMessageFilter(this);
		pLoop->RemoveIdleHandler(this);
		bHandled = FALSE;
	}

	// unregister message filtering and idle updates
	auto pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 1;
}

std::pair<IView*, CMessageMap*> CMainFrame::CreateView(TreeItemType type) {
	switch (type & TreeItemType::ItemMask) {
		case TreeItemType::Image:
		{
			auto view = new CPEImageView(this, m_PE);
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
			view->SetData(m_PE, 0, m_PE->GetFileInfo()->IsPE64 ?
				m_PE->GetNTHeader()->NTHdr64.OptionalHeader.SizeOfHeaders : m_PE->GetNTHeader()->NTHdr32.OptionalHeader.SizeOfHeaders);
			return { view, view };
		}

		case TreeItemType::DOSHeader:
		{
			auto sym = GetSymbolForName(L"ntdll.dll", L"_IMAGE_DOS_HEADER");
			if (!sym)		// temporary
				return {};

			auto view = new CStructView(this, sym, L"DOS Header");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetValue(m_PE->GetMSDOSHeader());
			return { view, view };
		}

		case TreeItemType::NTHeader:
		{
			auto sym = GetSymbolForName(L"ntdll.dll", m_PE->GetFileInfo()->IsPE32 ? L"_IMAGE_NT_HEADERS32" : L"_IMAGE_NT_HEADERS64");
			if (!sym)		// temporary
				return {};

			auto view = new CStructView(this, sym, L"NT Header");
			if (nullptr == view->DoCreate(m_Tabs)) {
				ATLASSERT(false);
				return {};
			}
			view->SetPEOffset(m_PE, m_PE->GetNTHeader()->dwOffset);
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
			auto const& sec = m_PE->GetSecHeaders()->at(((size_t)type >> ItemShift) - 1);
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

bool CMainFrame::ShowView(HTREEITEM hItem) {
	auto data = GetItemData<TreeItemType>(m_Tree, hItem);
	if (auto it = m_Views.find(data); it != m_Views.end()) {
		int count = m_Tabs.GetPageCount();
		for (int i = 0; i < count; i++) {
			if (m_Tabs.GetPageHWND(i) == it->second) {
				m_Tabs.SetActivePage(i);
				return true;
			}
		}
	}
	else {
		auto [view, map] = CreateView(data);
		if (view) {
			int image, dummy;
			m_Tree.GetItemImage(hItem, image, dummy);
			m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, map);
			m_Views.insert({ data, view->GetHwnd() });
			m_Views2.insert({ view->GetHwnd(), data });
			return true;
		}
	}
	return false;
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

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (int page = m_Tabs.GetActivePage(); page >= 0) {
		auto hWnd = m_Tabs.GetPageHWND(page);
		auto type = m_Views2.at(hWnd);
		m_Views.erase(type);
		m_Views2.erase(hWnd);
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
	if (!m_PE.Open(path)) {
		AtlMessageBox(m_hWnd, L"Error parsing file", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}

	m_Symbols.OpenImage(path);
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

	m_Views.clear();
	m_Views2.clear();
	m_Tabs.RemoveAllPages();
	ShowView(m_hRoot);
	UpdateUI();

	return true;
}

TreeItemType CMainFrame::TreeItemWithIndex(TreeItemType type, int index) {
	return static_cast<TreeItemType>((uint32_t)type + index);
}

void CMainFrame::BuildTree(int iconSize) {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Tree.SetItemHeight(iconSize + 2);
	if (!m_TreeImages) {
		BuildTreeImageList(iconSize);
		m_Tree.SetImageList(m_TreeImages);
		m_Tabs.SetImageList(m_TreeImages);
	}

	auto& path = m_PE.GetPath();

	auto root = InsertTreeItem(m_Tree, path.substr(path.rfind(L'\\') + 1).c_str(), 0, TreeItemType::Image);
	auto headers = InsertTreeItem(m_Tree, L"Headers", GetTreeIcon(IDI_HEADERS), TreeItemType::Headers, root);
	InsertTreeItem(m_Tree, L"DOS Header", GetTreeIcon(IDI_MSDOS), TreeItemType::DOSHeader, headers);
	InsertTreeItem(m_Tree, L"NT Header", GetTreeIcon(IDI_FILE_HEADER), TreeItemType::NTHeader, headers);
	if (m_PE->GetFileInfo()->HasRichHdr)
		InsertTreeItem(m_Tree, L"Rich Header", GetTreeIcon(IDI_RICH_HEADER), TreeItemType::RichHeader, headers);
	m_Tree.Expand(headers, TVE_EXPAND);

	auto sections = InsertTreeItem(m_Tree, L"Sections", GetTreeIcon(IDI_SECTIONS), TreeItemType::Sections, root);
	int i = 0;
	for (auto const& sec : *m_PE->GetSecHeaders()) {
		CString name = sec.SectionName.c_str();
		if (name.IsEmpty())
			name = CString((PCSTR)sec.SecHdr.Name, 8);
		InsertTreeItem(m_Tree, name, GetTreeIcon(IDI_SECTION), TreeItemWithIndex(TreeItemType::Section, (i + 1) << ItemShift), sections);
		i++;
	}
	if (m_PE->GetSecHeaders()->size() < 15)
		m_Tree.Expand(sections, TVE_EXPAND);

	auto directories = InsertTreeItem(m_Tree, L"Data Directories", GetTreeIcon(IDI_DIRS), TreeItemType::Directories, root);
	i = 0;
	for (auto const& dir : *m_PE->GetDataDirs()) {
		if (dir.DataDir.Size) {
			InsertTreeItem(m_Tree, PEStrings::GetDataDirectoryName(i), DirectoryIndexToIcon(i),
				TreeItemWithIndex(TreeItemType::Directory, i), directories);
		}
		i++;
	}
	m_Tree.Expand(directories, TVE_EXPAND);

	auto resources = InsertTreeItem(m_Tree, L"Resources", GetTreeIcon(IDI_RESOURCE), TreeItemType::Resources, root);
	std::unordered_map<std::wstring, HTREEITEM> typeItems;
	auto fresources = libpe::Ilibpe::FlatResources(*m_PE->GetResources());
	i = 0;
	m_HasManifest = m_HasVersion = false;
	m_FlatResources.clear();
	m_FlatResources.reserve(fresources.size());
	for (auto const& res : fresources) {
		std::wstring type = res.TypeStr.empty() ? PEStrings::ResourceTypeToString(res.TypeID) : std::wstring(res.TypeStr);
		if (type.empty())
			type = std::format(L"#{}", res.TypeID);
		if (res.TypeID == (WORD)(LONG_PTR)RT_MANIFEST)
			m_HasManifest = true;
		else if (res.TypeID == (WORD)(LONG_PTR)RT_VERSION)
			m_HasVersion = true;
		auto it = typeItems.find(type);
		if (it == typeItems.end()) {
			auto hItem = InsertTreeItem(m_Tree, type.c_str(), ResourceTypeToIcon(res.TypeID),
				TreeItemWithIndex(TreeItemType::ResourceTypeName, (i + 1) << ItemShift), resources, TVI_SORT);
			it = typeItems.insert({ type, hItem }).first;
		}
		auto name = res.NameID == 0 ? res.NameStr.data() : (L"#" + std::to_wstring(res.NameID));
		auto lang = res.LangStr.empty() ? PEStrings::LanguageToString(res.LangID) : std::wstring(res.LangStr);
		FlatResource fres(res);
		InsertTreeItem(m_Tree, std::format(L"{} ({})", name, lang).c_str(), ResourceTypeToIcon(res.TypeID),
			TreeItemWithIndex(TreeItemType::Resource, (i + 1) << ItemShift), it->second, TVI_SORT);
		fres.Name = std::move(name);
		fres.Type = std::move(type);
		fres.Language = std::move(lang);
		m_FlatResources.emplace_back(std::move(fres));
		i++;
	}
	m_Tree.Expand(resources, TVE_EXPAND);
	m_hRoot = root;

	m_Tree.Expand(root, TVE_EXPAND);
	m_Tree.SelectItem(root);
	m_Tree.SetRedraw();
	m_Tree.SetFocus();
}

HWND CMainFrame::GetHwnd() const {
	return m_hWnd;
}

BOOL CMainFrame::TrackPopupMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd) {
	return ShowContextMenu(hMenu, flags, x, y, hWnd);
}

CUpdateUIBase& CMainFrame::GetUI() {
	return *this;
}

LRESULT CMainFrame::OnFileClose(WORD, WORD, HWND, BOOL&) {
	m_Tabs.RemoveAllPages();
	m_Views.clear();
	m_PE.Close();
	m_Tree.DeleteAllItems();
	UpdateUI();

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
	else if (resId == RT_ACCELERATOR) {
		auto view = new CAcceleratorTableView(this, (res.Name + L" (Accel)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		view->AddAccelTable(res.Data);
		return { view, view };
	}
	else if (resId == RT_MANIFEST) {
		auto view = new CTextView(this, (res.Name + L" (Manifest)").c_str());
		if (!view->DoCreate(m_Tabs))
			return {};

		view->SetXmlText(CString((PCSTR)res.Data.data(), (int)res.Data.size()));
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

	bool icon = resId == RT_ICON || resId == RT_GROUP_ICON;
	bool group = resId == RT_GROUP_ICON || resId == RT_GROUP_CURSOR;
	if (icon || group) {
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

CString CMainFrame::DoFileOpen() const {
	CSimpleFileDialog dlg(TRUE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING,
		L"All PE Files\0*.exe;*.dll;*.efi;*.ocx;*.cpl;*.sys;*.mui;*.mun;*.scr\0All Files\0*.*\0");
	ThemeHelper::Suspend();
	auto path = IDOK == dlg.DoModal() ? dlg.m_szFileName : L"";
	ThemeHelper::Resume();
	return path;
}

int CMainFrame::GetIconIndex(UINT icon) const {
	return s_ImageIndices.at(icon);
}

void CMainFrame::BuildTreeImageList(int iconSize) {
	if (m_TreeImages)
		return;

	m_TreeImages.Create(iconSize, iconSize, ILC_COLOR32, 32, 16);

	WORD icon = 0;
	CString path = m_PE.GetPath().c_str();
	auto hIcon = ::ExtractAssociatedIcon(_Module.GetModuleInstance(), path.GetBuffer(), &icon);
	if (!hIcon)
		hIcon = AtlLoadSysIcon(IDI_APPLICATION);
	m_TreeImages.AddIcon(hIcon);

	UINT const icons[] = {
		IDI_SECTIONS, IDI_DIR_OPEN, IDI_RESOURCE, IDI_HEADERS, IDI_DIRS, IDI_SECURITY,
		IDI_SECTION, IDI_EXPORTS, IDI_IMPORTS, IDI_DEBUG, IDI_FONT, IDI_ICON, IDI_CURSOR,
		IDI_MANIFEST, IDI_VERSION, IDI_TYPE, IDI_BITMAP, IDI_MESSAGE, IDI_TEXT,
		IDI_KEYBOARD, IDI_FORM, IDI_EXCEPTION, IDI_DELAY_IMPORT, IDI_RELOC,
		IDI_THREAD, IDI_RICH_HEADER, IDI_MSDOS, IDI_FILE_HEADER, IDI_COMPONENT,
		IDI_FUNCTION, IDI_FUNC_FORWARD, IDI_INTERFACE, IDI_DLL_IMPORT, IDI_FUNCTION2,
	};

	bool insert = s_ImageIndices.empty();
	for (auto icon : icons) {
		int image = m_TreeImages.AddIcon(AtlLoadIconImage(icon, 0, iconSize, iconSize));
		if (insert)
			s_ImageIndices.insert({ icon, image });
	}
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
	AddSubMenu(menu);
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

LRESULT CMainFrame::OnShowWindow(UINT, WPARAM show, LPARAM, BOOL&) {
	bool static shown = false;
	if (show && shown) {
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

LRESULT CMainFrame::OnMenuSelect(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) const {
	return 0;
}
