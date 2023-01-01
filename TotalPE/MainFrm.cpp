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

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	return m_Tabs.PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UIUpdateToolBar();
	return FALSE;
}

bool CMainFrame::OnTreeDoubleClick(HWND tree, HTREEITEM hItem) {
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
		auto view = CreateView(data);
		if (view) {
			int image, dummy;
			m_Tree.GetItemImage(hItem, image, dummy);
			m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), image, view);
			m_Views.insert({ data, view->GetHwnd() });
			return true;
		}
	}
	return false;
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
	//UIEnable(ID_VIEW_MANIFEST, m_pe != nullptr && m_pe->get_resources().entry_by_id(dummy, 24));
	//UIEnable(ID_VIEW_VERSION, m_pe != nullptr && m_pe->get_resources().entry_by_id(dummy, 16));
	UIEnable(ID_FILE_OPENINANEWWINDOW, fi != nullptr);
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
		{ ID_VIEW_SECTIONS, IDI_SECTIONS },
		{ ID_VIEW_DIRECTORIES, IDI_DIR_OPEN },
		{ ID_VIEW_RESOURCES, IDI_RESOURCE },
		{ ID_VIEW_EXPORTS, IDI_EXPORTS },
		{ ID_VIEW_IMPORTS, IDI_IMPORTS },
		{ ID_VIEW_MANIFEST, IDI_MANIFEST },
		{ ID_VIEW_VERSION, IDI_VERSION },
		{ ID_VIEW_DEBUG, IDI_DEBUG},
		{ ID_FILE_RUNASADMINISTRATOR, 0, IconHelper::GetShieldIcon() },
	};
	for (auto& cmd : commands) {
		if (cmd.icon)
			AddCommand(cmd.id, cmd.icon);
		else
			AddCommand(cmd.id, cmd.hIcon);
	}
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	DragAcceptFiles();
	CreateSimpleStatusBar();
	m_StatusBar.SubclassWindow(m_hWndStatusBar);

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
	m_Splitter.SetSplitterPosPct(25);

	// register object for message filtering and idle updates
	auto pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	CMenuHandle menuMain = GetMenu();

	const int WindowMenuPosition = 4;
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

	UISetCheck(ID_VIEW_STATUS_BAR, 1);
	UpdateUI();

	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// unregister message filtering and idle updates
	auto pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 1;
}

IView* CMainFrame::CreateView(TreeItemType type) {
	switch (type) {
		case TreeItemType::Image:
		{
			auto view = new CPEImageView(this, m_PE);
			if (nullptr == view->Create(m_Tabs, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN)) {
				ATLASSERT(false);
				return nullptr;
			}
			return view;
		}
	}
	return nullptr;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
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
	int nActivePage = m_Tabs.GetActivePage();
	if (nActivePage != -1)
		m_Tabs.RemovePage(nActivePage);
	else
		::MessageBeep((UINT)-1);

	return 0;
}

int CMainFrame::GetTreeIcon(UINT id) {
	ATLASSERT(s_TreeImageIndices.contains(id));
	return s_TreeImageIndices[id];
}

LRESULT CMainFrame::OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Tabs.RemoveAllPages();

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

int CMainFrame::DirectoryIndexToIcon(int index) {
	static const int icons[] = {
		GetTreeIcon(IDI_EXPORTS),
		GetTreeIcon(IDI_IMPORTS),
		GetTreeIcon(IDI_RESOURCE),
		-1,
		GetTreeIcon(IDI_SECURITY),
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
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

	BuildTree(20);
	UpdateUI();

	CString ftitle;
	ftitle.LoadString(IDR_MAINFRAME);
	if (SecurityHelper::IsRunningElevated())
		ftitle += L" (Administrator)";
	CString spath(path);
	CString title = spath.Mid(spath.ReverseFind(L'\\') + 1) + L" - " + ftitle;
	SetWindowText(title);
	//s_recentFiles.AddFile(path);
	//s_settings.RecentFiles(s_recentFiles.Files());
	//UpdateRecentFilesMenu();

	return true;
}

TreeItemType CMainFrame::TreeItemWithIndex(TreeItemType type, int index) {
	return static_cast<TreeItemType>((uint32_t)type + index);
}

void CMainFrame::BuildTree(int iconSize) {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	if (!m_TreeImages) {
		BuildTreeImageList(iconSize);
		m_Tree.SetImageList(m_TreeImages);
		m_Tabs.SetImageList(m_TreeImages);
	}

	auto& path = m_PE.GetPath();

	auto root = InsertTreeItem(m_Tree, path.substr(path.rfind(L'\\') + 1).c_str(), 0, TreeItemType::Image);
	auto headers = InsertTreeItem(m_Tree, L"Headers", GetTreeIcon(IDI_HEADERS), TreeItemType::Headers, root);
	InsertTreeItem(m_Tree, L"DOS Header", GetTreeIcon(IDI_HEADERS), TreeItemType::DOSHeader, headers);
	InsertTreeItem(m_Tree, L"Optional Header", GetTreeIcon(IDI_HEADERS), TreeItemType::OptionalHeader, headers);
	InsertTreeItem(m_Tree, L"Rich Header", GetTreeIcon(IDI_HEADERS), TreeItemType::RichHeader, headers);

	auto sections = InsertTreeItem(m_Tree, L"Sections", GetTreeIcon(IDI_SECTIONS), TreeItemType::Sections, root);
	int i = 0;
	for (auto const& sec : *m_PE->GetSecHeaders()) {
		CString name = sec.SectionName.c_str();
		if (name.IsEmpty())
			name = CString((PCSTR)sec.SecHdr.Name, 8);
		InsertTreeItem(m_Tree, name, GetTreeIcon(IDI_SECTION), TreeItemWithIndex(TreeItemType::Section, i), sections);
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
	m_FlatResources = libpe::Ilibpe::FlatResources(*m_PE->GetResources());
	i = 0;
	for (auto const& res : m_FlatResources) {
		std::wstring type = res.TypeStr.empty() ? PEStrings::ResourceTypeToString(res.TypeID) : std::wstring(res.TypeStr);
		if (type.empty())
			type = std::format(L"#{}", res.TypeID);

		auto it = typeItems.find(type);
		if (it == typeItems.end()) {
			auto hItem = InsertTreeItem(m_Tree, type.c_str(), ResourceTypeToIcon(res.TypeID), TreeItemWithIndex(TreeItemType::ResourceTypeName, i), resources, TVI_SORT);
			it = typeItems.insert({ type, hItem }).first;
		}
		auto name = res.NameID == 0 ? res.NameStr.data() : (L"#" + std::to_wstring(res.NameID));
		auto lang = res.LangStr.empty() ? PEStrings::LanguageToString(res.LangID) : std::wstring(res.LangStr);
		InsertTreeItem(m_Tree, std::format(L"{} ({})", name, lang).c_str(), ResourceTypeToIcon(res.TypeID), 
			TreeItemWithIndex(TreeItemType::Resource, i), it->second, TVI_SORT);
		i++;
	}
	m_Tree.Expand(resources, TVE_EXPAND);

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

LRESULT CMainFrame::OnFileOpen(WORD, WORD, HWND, BOOL&) {
	auto path = DoFileOpen();
	if (path.IsEmpty())
		return 0;

	OpenPE(path);
	return 0;
}

CString CMainFrame::DoFileOpen() const {
	CSimpleFileDialog dlg(TRUE, nullptr, nullptr, OFN_EXPLORER | OFN_ENABLESIZING,
		L"All PE Files\0*.exe;*.dll;*.efi;*.ocx;*.cpl;*.sys;*.mui;*.mun;*.scr\0All Files\0*.*\0");
	ThemeHelper::Suspend();
	auto path = IDOK == dlg.DoModal() ? dlg.m_szFileName : L"";
	ThemeHelper::Resume();
	return path;
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
		IDI_KEYBOARD, IDI_FORM,
	};

	bool insert = s_TreeImageIndices.empty();
	for (auto icon : icons) {
		int image = m_TreeImages.AddIcon(AtlLoadIconImage(icon, 0, iconSize, iconSize));
		if (insert)
			s_TreeImageIndices.insert({ icon, image });
	}
}
