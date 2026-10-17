#include "pch.h"
#include "MainFrm.h"
#include "AppSettings.h"
#include "Images.h"
#include <ToolbarHelper.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>
#include <MetadataPackages.h>

namespace {
	// (the names of the metadata are ASCII)
	std::wstring Wide(std::string const& text) {
		return std::wstring(text.begin(), text.end());
	}
	std::string Narrow(std::wstring const& text) {
		std::string result;
		for (auto c : text)
			result += (char)c;
		return result;
	}

	PCWSTR const s_Machines[] = { L"x86", L"x64", L"ARM64" };
	constexpr int DefaultNavigatorWidth = 440;
}

CMainFrame::CMainFrame(std::wstring startName) : m_StartName(std::move(startName)) {
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (m_QuickOpen.PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Decode.PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Search && m_Search->PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Namespaces && m_Namespaces->PreTranslateMessage(pMsg))
		return TRUE;
	if (m_UsedBy && m_UsedBy->PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Favorites && m_Favorites->PreTranslateMessage(pMsg))
		return TRUE;
	if (m_History && m_History->PreTranslateMessage(pMsg))
		return TRUE;
	if (auto view = ActiveView(); view && view->PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Side && m_Side->PreTranslateMessage(pMsg))
		return TRUE;
	return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UpdateUIState();
	return FALSE;
}

LRESULT CMainFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	auto& settings = AppSettings::Get();
	// the machine used last, else this computer's
	auto machine = settings.Machine();
	if (machine < 0 || machine > (int)Win32Api::Machine::Arm64) {
#ifdef _M_ARM64
		machine = (int)Win32Api::Machine::Arm64;
#else
		machine = (int)Win32Api::Machine::X64;
#endif
	}
	m_Machine = (Win32Api::Machine)machine;

	m_Images = Images::Create();
	CreateToolBar();
	CreateSimpleStatusBar();
	{
		CStatusBarCtrl status(m_hWndStatusBar);
		int parts[] = { 320, -1 };
		status.SetParts(_countof(parts), parts);
		status.SetText(0, L"Loading...");
	}
	m_hWndClient = m_Loading.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);

	auto loop = _Module.GetMessageLoop();
	loop->AddMessageFilter(this);
	loop->AddIdleHandler(this);

	// the metadata, and what is made of it at first (the entries, the namespaces), while the window is there
	m_Loader = std::thread([hWnd = m_hWnd, machine = m_Machine] {
		auto api = Win32Api::Get(machine);
		if (api) {
			api->Entries();
			api->Namespaces();
		}
		::PostMessage(hWnd, WM_LOADED, 0, (LPARAM)api);
	});
	return 0;
}

LRESULT CMainFrame::OnLoaded(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Loader.joinable())
		m_Loader.join();
	m_Api = (Win32Api*)lp;
	if (!m_Api) {
		auto text = std::format(L"The Win32 metadata was not found:\n{}\n\nRun GetWin32Metadata.ps1 (in the TotalPE folder of the "
			L"sources) to download it, and build again.", Win32Api::MetadataPath());
		AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
		PostMessage(WM_CLOSE);
		return 0;
	}
	SetRedraw(FALSE);
	FinishCreate();
	SetRedraw(TRUE);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
	return 0;
}

void CMainFrame::FinishCreate() {
	auto& settings = AppSettings::Get();
	m_QuickOpen.Create(m_hWnd, rcDefault, nullptr, WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW);
	m_QuickOpen.SetApi(*m_Api);
	m_QuickOpen.SetImageList(m_Images);
	m_Decode.Create(m_hWnd, rcDefault, nullptr, WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW);
	m_QuickOpen.OnOpen = [this](size_t entry) {
		OpenEntry(entry, true);
		if (auto view = ActiveView())
			view->SetFocus();
	};

	m_Loading.DestroyWindow();
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	CreateNavigator();
	CreateEditors();
	m_Splitter.SetSplitterExtendedStyle(0, SPLIT_PROPORTIONAL);	// (the left side keeps its width as the window is sized)
	m_Splitter.SetSplitterPanes(m_Navigator, m_Right);
	UpdateLayout();
	auto width = settings.NavigatorWidth();
	m_Splitter.SetSplitterPos(width > 100 ? width : DefaultNavigatorWidth);
	m_Right.SetSplitterPosPct(100 - std::clamp(settings.DocumentationPercent(), 10, 90));

	// (the default font: as the controls have it)
	if (settings.UIFont().lfFaceName[0])
		ApplyUIFont();
	for (auto const& name : settings.Favorites())
		m_FavoriteNames.push_back(Narrow(name));
	for (auto const& name : settings.History())
		m_HistoryNames.push_back(Narrow(name));
	UpdateLists();
	// (the Used By page is of a type asked for)
	auto page = settings.NavigatorPage();
	ShowNavigatorPage(page == 2 ? 0 : page);
	RestoreTabs();
	if (!m_StartName.empty())
		OpenByName(m_StartName);
	UpdateStatus();
	UpdateTitle();
	UpdateUIState();
}

bool CMainFrame::OpenByName(std::wstring const& name) {
	auto entry = m_Api->Find(Narrow(name));
	if (!entry)
		return false;
	auto index = (size_t)(entry - m_Api->Entries().data());
	OpenEntry(index, true);
	m_Search->SelectEntry(index);
	if (auto view = ActiveView())
		view->SetFocus();
	return true;
}

// Another instance's name (it is not started again): opened here, now or when the metadata is loaded; this window to the front
LRESULT CMainFrame::OnCopyData(UINT, WPARAM, LPARAM lp, BOOL&) {
	auto data = (COPYDATASTRUCT const*)lp;
	if (!data || data->dwData != CopyDataName)
		return FALSE;
	std::wstring name((PCWSTR)data->lpData, data->cbData / sizeof(WCHAR));
	if (!name.empty()) {
		if (m_Api)
			OpenByName(name);
		else
			m_StartName = name;
	}
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	::SetForegroundWindow(m_hWnd);
	return TRUE;
}

//
// The metadata
//

void CMainFrame::AskRestart() {
	if (AtlMessageBox(m_hWnd, L"The metadata is read when Win32 API Explorer starts. Start it again now?", IDR_MAINFRAME,
		MB_ICONQUESTION | MB_YESNO) != IDYES)
		return;
	// (the settings first: the new one reads them)
	SaveSettings();
	AppSettings::Get().Save();
	WCHAR path[MAX_PATH];
	::GetModuleFileName(nullptr, path, _countof(path));
	if ((INT_PTR)::ShellExecute(nullptr, L"open", path, L"/new", nullptr, SW_SHOWNORMAL) > 32)
		PostMessage(WM_CLOSE);
}

LRESULT CMainFrame::OnMetadataFolder(WORD, WORD, HWND, BOOL&) {
	CShellFileOpenDialog dlg(nullptr, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
	dlg.GetPtr()->SetTitle(L"The folder of Windows.Win32.winmd (and Windows.Wdk.winmd)");
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	CString folder;
	dlg.GetFilePath(folder);
	if (::GetFileAttributesW(folder + L"\\Windows.Win32.winmd") == INVALID_FILE_ATTRIBUTES) {
		AtlMessageBox(m_hWnd, L"That folder has no Windows.Win32.winmd.", IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	auto& settings = AppSettings::Get();
	settings.MetadataFolder((PCWSTR)folder);
	settings.MetadataVersion(L"");
	settings.WdkMetadataVersion(L"");
	AskRestart();
	return 0;
}

LRESULT CMainFrame::OnDefaultMetadata(WORD, WORD, HWND, BOOL&) {
	auto& settings = AppSettings::Get();
	if (settings.MetadataFolder().empty()) {
		AtlMessageBox(m_hWnd, L"The metadata next to Win32ApiExp.exe is the one used.", IDR_MAINFRAME, MB_ICONINFORMATION);
		return 0;
	}
	settings.MetadataFolder(L"");
	settings.MetadataVersion(L"");
	settings.WdkMetadataVersion(L"");
	AskRestart();
	return 0;
}

LRESULT CMainFrame::OnCheckMetadata(WORD, WORD, HWND, BOOL&) {
	if (m_MetadataBusy)
		return 0;
	m_MetadataBusy = true;
	CStatusBarCtrl(m_hWndStatusBar).SetText(0, L"Looking for the newest metadata on nuget.org...");
	std::thread([hWnd = m_hWnd] {
		auto win32 = MetadataPackages::Versions(MetadataPackages::Win32Package);
		auto wdk = MetadataPackages::Versions(MetadataPackages::WdkPackage);
		auto found = win32.empty() ? nullptr : new MetadataVersions{ win32.back(), wdk.empty() ? std::string() : wdk.back() };
		if (!::PostMessage(hWnd, WM_METADATA_CHECKED, 0, (LPARAM)found))
			delete found;
	}).detach();
	return 0;
}

LRESULT CMainFrame::OnMetadataChecked(UINT, WPARAM, LPARAM lp, BOOL&) {
	std::unique_ptr<MetadataVersions> found((MetadataVersions*)lp);
	m_MetadataBusy = false;
	UpdateStatus();
	if (!found) {
		AtlMessageBox(m_hWnd, L"The versions of the metadata could not be read from nuget.org.", IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	auto& settings = AppSettings::Get();
	auto version = Narrow(settings.MetadataVersion()), wdkVersion = Narrow(settings.WdkMetadataVersion());
	auto newest = Wide(found->Win32), newestWdk = found->Wdk.empty() ? std::wstring(L"(none)") : Wide(found->Wdk);
	if (version == found->Win32 && wdkVersion == found->Wdk) {
		AtlMessageBox(m_hWnd, std::format(L"The metadata used is the newest: {} (the WDK's: {}).", newest, newestWdk).c_str(),
			IDR_MAINFRAME, MB_ICONINFORMATION);
		return 0;
	}
	auto used = !version.empty() ? std::format(L"{} (the WDK's: {})", Wide(version), wdkVersion.empty() ? L"(none)" : Wide(wdkVersion)) :
		settings.MetadataFolder().empty() ? std::wstring(L"the one next to Win32ApiExp.exe (its version is not known)") :
		L"the one in " + settings.MetadataFolder() + L" (its version is not known)";
	auto text = std::format(L"The newest metadata on nuget.org: {} (the WDK's: {}).\nThe metadata used: {}.\n\n"
		L"Download it (about 25 MB, under the license of the Windows SDK, which is saved with it) and use it?", newest, newestWdk, used);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONQUESTION | MB_YESNO) != IDYES)
		return 0;

	// to the user's folder of the application, of its version
	WCHAR local[MAX_PATH];
	::ExpandEnvironmentStrings(L"%LOCALAPPDATA%\\Win32ApiExplorer\\Metadata\\", local, _countof(local));
	found->Folder = local + newest;
	m_MetadataBusy = true;
	std::thread([hWnd = m_hWnd, versions = *found] {
		auto ok = MetadataPackages::Install(versions.Win32, versions.Wdk, versions.Folder, [hWnd](std::wstring const& status) {
			auto text = new std::wstring(status);
			if (!::PostMessage(hWnd, WM_METADATA_STATUS, 0, (LPARAM)text))
				delete text;
		});
		auto result = new MetadataVersions(versions);
		if (!::PostMessage(hWnd, WM_METADATA_INSTALLED, ok, (LPARAM)result))
			delete result;
	}).detach();
	return 0;
}

LRESULT CMainFrame::OnMetadataStatus(UINT, WPARAM, LPARAM lp, BOOL&) {
	std::unique_ptr<std::wstring> text((std::wstring*)lp);
	CStatusBarCtrl(m_hWndStatusBar).SetText(0, text->c_str());
	return 0;
}

LRESULT CMainFrame::OnMetadataInstalled(UINT, WPARAM ok, LPARAM lp, BOOL&) {
	std::unique_ptr<MetadataVersions> versions((MetadataVersions*)lp);
	m_MetadataBusy = false;
	UpdateStatus();
	if (!ok) {
		AtlMessageBox(m_hWnd, L"The metadata could not be downloaded.", IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}
	auto& settings = AppSettings::Get();
	settings.MetadataFolder(versions->Folder);
	settings.MetadataVersion(Wide(versions->Win32));
	settings.WdkMetadataVersion(Wide(versions->Wdk));
	AskRestart();
	return 0;
}

LRESULT CMainFrame::OnCommandWhileLoading(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	handled = FALSE;
	if (m_Api)
		return 0;
	switch (LOWORD(wp)) {
		case ID_APP_EXIT: case ID_APP_ABOUT:
			break;
		case IDC_MACHINE:
			// (the machine is the one being loaded)
			m_MachineBar.Combo().SetCurSel((int)m_Machine);
			handled = TRUE;
			break;
		default:
			handled = TRUE;
			break;
	}
	return 0;
}

void CMainFrame::CreateToolBar() {
	ToolBarButtonInfo const buttons[] = {
		{ ID_GO_BACK, IDI_BACK },
		{ ID_GO_FORWARD, IDI_FORWARD },
		{ 0 },
		{ ID_GO_REVEAL, IDI_NAMESPACE, BTNS_BUTTON | BTNS_AUTOSIZE, L"Reveal" },
		{ ID_GO_USEDBY, IDI_FIND, BTNS_BUTTON | BTNS_AUTOSIZE, L"Find Usages" },
		{ 0 },
		{ ID_SHOW_DECLARATION, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Declaration" },
		{ ID_SHOW_LAYOUT, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Layout" },
		{ ID_SHOW_COMPARE, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Compare" },
		{ 0 },
		{ ID_VIEW_DOCUMENTATION, IDI_GLOBE, BTNS_CHECK | BTNS_AUTOSIZE, L"Documentation" },
		{ ID_DOC_BROWSER, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Open in Browser" },
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_ToolBar = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 16);
	AddSimpleReBarBand(m_ToolBar);
	UIAddToolBar(m_ToolBar);
	// the toolbar's band as wide as its buttons (the machine follows them)
	CRect last;
	m_ToolBar.GetItemRect(m_ToolBar.GetButtonCount() - 1, &last);
	CReBarCtrl rebar(m_hWndToolBar);
	REBARBANDINFO band{ sizeof(band) };
	band.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
	rebar.GetBandInfo(0, &band);
	band.cxMinChild = band.cxIdeal = last.right;
	band.cx = last.right + 16;
	rebar.SetBandInfo(0, &band);

	m_MachineBar.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
	for (auto name : s_Machines)
		m_MachineBar.Combo().AddString(name);
	m_MachineBar.Combo().SetCurSel((int)m_Machine);
	AddSimpleReBarBand(m_MachineBar, nullptr, FALSE, 200);
	// (the band as high as the list)
	band.fMask = RBBIM_CHILDSIZE;
	rebar.GetBandInfo(1, &band);
	band.cyMinChild = m_MachineBar.GetIdealHeight();
	rebar.SetBandInfo(1, &band);
}

void CMainFrame::CreateNavigator() {
	m_Navigator.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto& strip = m_Navigator.Strip();
	strip.SetClosable(false);
	strip.Add(L"Search");
	strip.Add(L"Namespaces");
	strip.Add(L"Used By");
	strip.Add(L"Favorites");
	strip.Add(L"History");
	strip.OnSelected = [this](int page) { ShowNavigatorPage(page); };

	// (from any: a preview, or a tab that stays, which gets the focus)
	auto open = [this](size_t entry, bool pin, std::string const& name) {
		OpenEntry(entry, pin, name);
		if (auto view = ActiveView(); view && pin)
			view->SetFocus();
	};
	m_Search = std::make_unique<CSearchPane>(*m_Api);
	m_Search->Create(m_Navigator, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Search->SetImageList(m_Images);
	m_Search->OnOpen = open;
	m_Search->OnCountChanged = [this] { UpdateStatus(); };

	m_Namespaces = std::make_unique<CNamespacePane>(*m_Api);
	m_Namespaces->Create(m_Navigator, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Namespaces->SetImageList(m_Images);
	m_Namespaces->OnOpen = [open](size_t entry, bool pin) { open(entry, pin, {}); };

	m_UsedBy = std::make_unique<CSearchPane>(*m_Api);
	m_UsedBy->Create(m_Navigator, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_UsedBy->SetImageList(m_Images);
	m_UsedBy->SetScope(&m_NoEntries, L"what uses a type (Shift+F12 finds it)");
	m_UsedBy->OnOpen = open;
	m_UsedBy->OnCountChanged = [this] { UpdateStatus(); };

	for (auto pane : { &m_Favorites, &m_History }) {
		*pane = std::make_unique<CSearchPane>(*m_Api);
		(*pane)->Create(m_Navigator, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
		(*pane)->SetImageList(m_Images);
		(*pane)->OnOpen = open;
		(*pane)->OnCountChanged = [this] { UpdateStatus(); };
	}
	// (Delete: out of the list)
	auto remover = [this](std::vector<std::string>& names) {
		return [this, &names](size_t entry) {
			std::erase(names, m_Api->Entries()[entry].Name);
			UpdateLists();
		};
	};
	m_Favorites->OnDelete = remover(m_FavoriteNames);
	m_History->OnDelete = remover(m_HistoryNames);
}

void CMainFrame::CreateEditors() {
	auto& settings = AppSettings::Get();
	m_Right.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_EditorSplit.Create(m_Right, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Editors.Create(m_EditorSplit, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_SideHost.Create(m_EditorSplit, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_EditorSplit.SetSplitterPanes(m_Editors, m_SideHost);
	m_EditorSplit.SetSinglePaneMode(SPLIT_PANE_LEFT);
	m_SideHost.Strip().SetImageList(m_Images);
	m_SideHost.Strip().OnCloseRequested = [this](int) { CloseSide(); };
	auto& strip = m_Editors.Strip();
	strip.SetImageList(m_Images);
	strip.SetListButton(true);
	strip.OnMoved = [this](int from, int to) {
		auto tab = std::move(m_Tabs[from]);
		m_Tabs.erase(m_Tabs.begin() + from);
		m_Tabs.insert(m_Tabs.begin() + to, std::move(tab));
	};
	strip.OnSelected = [this](int index) { ActivateTab(index); };
	strip.OnCloseRequested = [this](int index) { CloseTab(index); };
	strip.OnDoubleClicked = [this](int index) { Pin(index); };
	strip.OnContextMenu = [this](int index, CPoint pt) { ShowTabMenu(index, pt); };

	m_Doc.SetDarkMode(settings.DarkMode());
	m_Doc.Create(m_Right, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Right.SetSplitterPanes(m_EditorSplit, m_Doc);
	if (!settings.ShowDocumentation())
		m_Right.SetSinglePaneMode(SPLIT_PANE_TOP);
}

LRESULT CMainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	// (closed while the metadata is loaded: when it is)
	if (m_Loader.joinable())
		m_Loader.join();
	if (m_Api)
		SaveSettings();
	auto loop = _Module.GetMessageLoop();
	loop->RemoveMessageFilter(this);
	loop->RemoveIdleHandler(this);
	handled = FALSE;
	return 0;
}

void CMainFrame::SaveSettings() {
	auto& settings = AppSettings::Get();
	WINDOWPLACEMENT wp{ sizeof(wp) };
	GetWindowPlacement(&wp);
	settings.MainWindowPlacement(wp);
	settings.NavigatorWidth(m_Splitter.GetSplitterPos());
	if (settings.ShowDocumentation())
		settings.DocumentationPercent(100 - m_Right.GetSplitterPosPct());
	// the tabs that stay (not the preview)
	std::vector<std::wstring> names;
	int active = -1;
	for (int i = 0; i < (int)m_Tabs.size(); i++) {
		auto const& tab = m_Tabs[i];
		auto name = tab.View->Name();
		if (tab.Preview || name.empty())
			continue;
		if (i == ActiveTab())
			active = (int)names.size();
		names.push_back(Wide(name));
	}
	settings.OpenTabs(names);
	std::vector<std::wstring> favorites, history;
	for (auto const& name : m_FavoriteNames)
		favorites.push_back(Wide(name));
	for (auto const& name : m_HistoryNames)
		history.push_back(Wide(name));
	settings.Favorites(favorites);
	settings.History(history);
	settings.ActiveTab(active);
}

void CMainFrame::RestoreTabs() {
	auto& settings = AppSettings::Get();
	for (auto const& name : settings.OpenTabs())
		if (auto entry = m_Api->Find(Narrow(name)))
			AddTab((int)(entry - m_Api->Entries().data()), false, (int)m_Tabs.size());
	if (m_Tabs.empty())
		return;
	auto active = settings.ActiveTab();
	ActivateTab(active >= 0 && active < (int)m_Tabs.size() ? active : (int)m_Tabs.size() - 1);
}

//
// The tabs
//

int CMainFrame::ActiveTab() const {
	return m_Editors.m_hWnd ? m_Editors.Strip().GetSelected() : -1;
}

int CMainFrame::TargetTab() const {
	return m_MenuTab >= 0 && m_MenuTab < (int)m_Tabs.size() ? m_MenuTab : ActiveTab();
}

CDeclarationView* CMainFrame::FocusedView() const {
	auto focus = ::GetFocus();
	if (m_Side && focus && (focus == m_Side->m_hWnd || m_Side->IsChild(focus)))
		return m_Side.get();
	return ActiveView();
}

int CMainFrame::SideTarget() const {
	auto const& entries = m_Api->Entries();
	if (m_MenuTab >= 0 && m_MenuTab < (int)m_Tabs.size())
		return m_Tabs[m_MenuTab].View->Entry();
	auto view = FocusedView();
	if (!view)
		return -1;
	auto referred = view->ReferredAtCaret();
	if (auto found = referred.empty() ? nullptr : m_Api->Find(referred))
		return (int)(found - entries.data());
	return view->Entry();
}

void CMainFrame::OpenToSide(size_t entry) {
	if (!m_Side) {
		m_Side = std::make_unique<CDeclarationView>(*m_Api);
		auto side = m_Side.get();
		side->OnFollow = [this, side](std::string const& name, bool newTab) {
			if (!newTab)
				side->Navigate(name);
			else if (auto found = m_Api->Find(name))
				OpenEntry(found - m_Api->Entries().data(), true);
		};
		side->OnShownChanged = [this] { SideChanged(); };
		side->Create(m_SideHost, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
		side->SetMode((CDeclarationView::Mode)std::clamp(AppSettings::Get().ShowMode(), 0, 2));
		m_SideHost.Strip().Add(L"", -1);
		m_SideHost.Strip().Select(0);
		m_SideHost.SetPage(*side);
		m_EditorSplit.SetSinglePaneMode(SPLIT_PANE_NONE);
		m_EditorSplit.SetSplitterPosPct(50);
	}
	m_Side->Show((int)entry);
	m_Side->SetFocus();
}

void CMainFrame::SideChanged() {
	if (!m_Side)
		return;
	auto entry = m_Side->Entry();
	if (entry < 0)
		return;
	m_SideHost.Strip().SetText(0, Wide(m_Side->Name()));
	m_SideHost.Strip().SetImage(0, Images::Of(m_Api->Entries()[entry].Kind));
}

void CMainFrame::CloseSide() {
	if (!m_Side)
		return;
	m_EditorSplit.SetSinglePaneMode(SPLIT_PANE_LEFT);
	m_SideHost.SetPage(nullptr);
	m_SideHost.Strip().RemoveAll();
	m_Side->DestroyWindow();
	m_Side.reset();
	if (auto view = ActiveView())
		view->SetFocus();
}

LRESULT CMainFrame::OnOpenToSide(WORD, WORD, HWND, BOOL&) {
	auto entry = SideTarget();
	m_MenuTab = -1;
	if (entry < 0)
		::MessageBeep(MB_ICONWARNING);
	else
		OpenToSide(entry);
	return 0;
}

CDeclarationView* CMainFrame::ActiveView() const {
	auto index = ActiveTab();
	return index >= 0 && index < (int)m_Tabs.size() ? m_Tabs[index].View.get() : nullptr;
}

int CMainFrame::TabOf(CDeclarationView const* view) const {
	for (int i = 0; i < (int)m_Tabs.size(); i++)
		if (m_Tabs[i].View.get() == view)
			return i;
	return -1;
}

int CMainFrame::TabShowing(int entry) const {
	for (int i = 0; i < (int)m_Tabs.size(); i++)
		if (m_Tabs[i].View->Entry() == entry)
			return i;
	return -1;
}

void CMainFrame::OpenEntry(size_t entry, bool pin, std::string const& name) {
	auto show = [&](int index) {
		ActivateTab(index);
		if (!name.empty())
			m_Tabs[index].View->HighlightName(name);
	};
	if (pin)
		AddToHistory((int)entry);
	if (auto index = TabShowing((int)entry); index >= 0) {
		if (pin)
			Pin(index);
		show(index);
		return;
	}
	if (!pin) {
		for (int i = 0; i < (int)m_Tabs.size(); i++) {
			if (!m_Tabs[i].Preview)
				continue;
			auto view = m_Tabs[i].View.get();
			view->ClearHistory();
			view->Show((int)entry);
			show(i);
			return;
		}
	}
	show(AddTab((int)entry, !pin));
}

int CMainFrame::AddTab(int entry, bool preview, int position) {
	auto view = std::make_unique<CDeclarationView>(*m_Api);
	auto p = view.get();
	p->OnFollow = [this, p](std::string const& name, bool newTab) { Follow(p, name, newTab); };
	p->OnShownChanged = [this, p] { ViewChanged(p); };
	p->Create(m_Editors, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	p->SetMode((CDeclarationView::Mode)std::clamp(AppSettings::Get().ShowMode(), 0, 2));
	p->Show(entry);
	// (after the active one, as an editor opens one)
	if (position < 0 || position > (int)m_Tabs.size())
		position = std::min(ActiveTab() + 1, (int)m_Tabs.size());
	m_Tabs.insert(m_Tabs.begin() + position, Tab{ std::move(view), preview });
	auto image = entry >= 0 ? Images::Of(m_Api->Entries()[entry].Kind) : -1;
	m_Editors.Strip().Insert(position, Wide(p->Name()), image, preview);
	return position;
}

void CMainFrame::ActivateTab(int index) {
	auto& strip = m_Editors.Strip();
	if (index < 0 || index >= (int)m_Tabs.size()) {
		strip.Select(-1);
		m_Editors.SetPage(nullptr);
	}
	else {
		strip.Select(index);
		m_Editors.SetPage(*m_Tabs[index].View);
	}
	UpdateDocumentation();
	UpdateTitle();
}

void CMainFrame::CloseTab(int index) {
	if (index < 0 || index >= (int)m_Tabs.size())
		return;
	auto active = ActiveTab();
	auto view = m_Tabs[index].View.get();
	if (m_Editors.GetPage() == view->m_hWnd)
		m_Editors.SetPage(nullptr);
	view->DestroyWindow();
	m_Tabs.erase(m_Tabs.begin() + index);
	auto& strip = m_Editors.Strip();
	strip.Remove(index);
	if (m_Tabs.empty())
		ActivateTab(-1);
	else if (index == active)
		ActivateTab(std::min(index, (int)m_Tabs.size() - 1));	// (the one after it, else the one before)
	else
		ActivateTab(strip.GetSelected());
}

void CMainFrame::Pin(int index) {
	if (index < 0 || index >= (int)m_Tabs.size() || !m_Tabs[index].Preview)
		return;
	m_Tabs[index].Preview = false;
	m_Editors.Strip().SetItalic(index, false);
	AddToHistory(m_Tabs[index].View->Entry());
}

void CMainFrame::AddToHistory(int entry) {
	constexpr size_t MaxHistory = 100;
	if (entry < 0 || !m_History || m_SwitchingMachine)
		return;
	auto const& name = m_Api->Entries()[entry].Name;
	if (!m_HistoryNames.empty() && m_HistoryNames.front() == name)
		return;
	std::erase(m_HistoryNames, name);
	m_HistoryNames.insert(m_HistoryNames.begin(), name);
	if (m_HistoryNames.size() > MaxHistory)
		m_HistoryNames.resize(MaxHistory);
	UpdateLists();
}

void CMainFrame::UpdateLists() {
	auto entries = [&](std::vector<std::string> const& names) {
		std::vector<size_t> result;
		for (auto const& name : names)
			if (auto entry = m_Api->Find(name))
				result.push_back(entry - m_Api->Entries().data());
		return result;
	};
	m_FavoriteEntries = entries(m_FavoriteNames);
	m_HistoryEntries = entries(m_HistoryNames);
	m_Favorites->SetScope(&m_FavoriteEntries, L"the favorites (Ctrl+Shift+A adds and removes)");
	m_History->SetScope(&m_HistoryEntries, L"what was opened");
}

void CMainFrame::Follow(CDeclarationView* view, std::string const& name, bool newTab) {
	if (newTab) {
		if (auto entry = m_Api->Find(name))
			OpenEntry(entry - m_Api->Entries().data(), true);
		return;
	}
	// (a preview that was used is kept)
	view->Navigate(name);
	Pin(TabOf(view));
}

void CMainFrame::ViewChanged(CDeclarationView* view) {
	auto index = TabOf(view);
	if (index < 0)
		return;		// (not in a tab yet)
	auto entry = view->Entry();
	auto& strip = m_Editors.Strip();
	// (an entry that the machine does not have: the tab keeps its title)
	if (entry >= 0) {
		strip.SetText(index, Wide(view->Name()));
		strip.SetImage(index, Images::Of(m_Api->Entries()[entry].Kind));
		// (a tab that stays went elsewhere: that was opened)
		if (!m_Tabs[index].Preview)
			AddToHistory(entry);
	}
	if (index == ActiveTab()) {
		UpdateDocumentation();
		UpdateTitle();
	}
}

void CMainFrame::ShowTabMenu(int index, CPoint pt) {
	if (index < 0)
		return;
	m_MenuTab = index;
	UpdateUIState();
	CMenu menu;
	menu.LoadMenu(IDR_TABMENU);
	auto command = menu.GetSubMenu(0).TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (command)
		SendMessage(WM_COMMAND, command);
	m_MenuTab = -1;
}

//
// What is shown around the tabs
//

void CMainFrame::ShowNavigatorPage(int page) {
	page = std::clamp(page, 0, 4);
	HWND const pages[] = { *m_Search, *m_Namespaces, *m_UsedBy, *m_Favorites, *m_History };
	m_Navigator.Strip().Select(page);
	m_Navigator.SetPage(pages[page]);
	AppSettings::Get().NavigatorPage(page);
	UpdateStatus();
}

int CMainFrame::UsagesTarget() const {
	auto const& entries = m_Api->Entries();
	int entry = -1;
	if (m_MenuTab >= 0 && m_MenuTab < (int)m_Tabs.size())
		entry = m_Tabs[m_MenuTab].View->Entry();
	else if (auto view = FocusedView()) {
		auto referred = view->ReferredAtCaret();
		auto found = referred.empty() ? nullptr : m_Api->Find(referred);
		entry = found ? (int)(found - entries.data()) : view->Entry();
	}
	if (entry < 0)
		return -1;
	auto kind = entries[entry].Kind;
	return kind == Win32Api::Entry::Kinds::Function || kind == Win32Api::Entry::Kinds::Constant ? -1 : entry;
}

int CMainFrame::DecodeTarget() const {
	auto const& entries = m_Api->Entries();
	// (an enum, or a typedef that is one: HRESULT)
	auto decodable = [&](int entry) { return entry >= 0 && !m_Api->DecodeValue(entries[entry], 0).empty(); };
	if (m_MenuTab >= 0 && m_MenuTab < (int)m_Tabs.size()) {
		auto entry = m_Tabs[m_MenuTab].View->Entry();
		return decodable(entry) ? entry : -1;
	}
	auto view = FocusedView();
	if (!view)
		return -1;
	auto referred = view->ReferredAtCaret();
	if (auto found = referred.empty() ? nullptr : m_Api->Find(referred); found && decodable((int)(found - entries.data())))
		return (int)(found - entries.data());
	return decodable(view->Entry()) ? view->Entry() : -1;
}

LRESULT CMainFrame::OnDecode(WORD, WORD, HWND, BOOL&) {
	auto entry = DecodeTarget();
	m_MenuTab = -1;
	if (entry < 0)
		::MessageBeep(MB_ICONWARNING);
	else
		m_Decode.Show(*m_Api, entry);
	return 0;
}

void CMainFrame::FindUsages(size_t entry) {
	m_UsedBySubject = m_Api->Entries()[entry].Name;
	UpdateUsedBy();
	ShowNavigatorPage(2);
	m_UsedBy->SetFocus();
}

void CMainFrame::UpdateUsedBy() {
	auto entry = m_UsedBySubject.empty() ? nullptr : m_Api->Find(m_UsedBySubject);
	auto& strip = m_Navigator.Strip();
	if (!entry) {
		m_UsedBy->SetScope(&m_NoEntries, L"what uses a type (Shift+F12 finds it)");
		strip.SetText(2, L"Used By");
		return;
	}
	CWaitCursor wait;
	auto const& users = m_Api->UsedBy(entry - m_Api->Entries().data());
	auto name = Wide(m_UsedBySubject);
	m_UsedBy->SetScope(&users, std::format(L"the {} that use {}", users.size(), name));
	strip.SetText(2, L"Used By: " + name);
}

void CMainFrame::UpdateDocumentation() {
	if (!AppSettings::Get().ShowDocumentation())
		return;
	auto view = ActiveView();
	auto entry = view ? view->Entry() : -1;
	if (entry < 0) {
		// (the browser is started for a page: not for none)
		if (!m_Doc.Url().empty())
			m_Doc.Navigate({}, {});
		return;
	}
	auto const& e = m_Api->Entries()[entry];
	m_Doc.Navigate(m_Api->DocumentationUrl(e), Wide(e.Name));
}

void CMainFrame::UpdateStatus() {
	if (!m_hWndStatusBar || !m_Search)
		return;
	CStatusBarCtrl status(m_hWndStatusBar);
	// (of the list shown: the search, or what uses a type)
	CSearchPane* const lists[] = { m_Search.get(), m_Search.get(), m_UsedBy.get(), m_Favorites.get(), m_History.get() };
	auto list = lists[std::clamp(m_Navigator.Strip().GetSelected(), 0, 4)];
	status.SetText(0, std::format(L"{} of {} shown", list->GetShownCount(), list->GetTotalCount()).c_str());
	status.SetText(1, std::format(L"Machine: {}", s_Machines[(int)m_Machine]).c_str());
}

void CMainFrame::UpdateTitle() {
	CString title;
	title.LoadString(IDR_MAINFRAME);
	auto view = ActiveView();
	auto name = view ? view->Name() : std::string();
	if (!name.empty())
		title += L" - " + CString(name.c_str());
	SetWindowText(title);
}

void CMainFrame::UpdateUIState() {
	if (!m_Api)
		return;
	auto& settings = AppSettings::Get();
	auto view = FocusedView();
	UIEnable(ID_GO_BACK, view && view->CanGoBack());
	UIEnable(ID_GO_FORWARD, view && view->CanGoForward());
	bool referred = view && !view->ReferredAtCaret().empty();
	UIEnable(ID_GO_DEFINITION, referred);
	UIEnable(ID_GO_DEFINITION_NEWTAB, referred);
	auto target = TargetTab();
	bool shows = target >= 0 && m_Tabs[target].View->Entry() >= 0;
	UIEnable(ID_GO_REVEAL, shows);
	UIEnable(ID_GO_USEDBY, UsagesTarget() >= 0);
	UIEnable(ID_GO_SIDE, SideTarget() >= 0);
	UIEnable(ID_EDIT_DECODE, DecodeTarget() >= 0);
	UIEnable(ID_DOC_BROWSER, shows);
	UIEnable(ID_EDIT_COPYNAME, shows);
	UISetCheck(ID_VIEW_DOCUMENTATION, settings.ShowDocumentation());
	UISetCheck(ID_VIEW_DARKMODE, settings.DarkMode());
	auto page = m_Navigator.m_hWnd ? m_Navigator.Strip().GetSelected() : 0;
	// (one of each: a radio item, of the range of its group's IDs)
	static_assert(ID_VIEW_NAMESPACES == ID_VIEW_SEARCH + 1 && ID_VIEW_USEDBY == ID_VIEW_SEARCH + 2 &&
		ID_VIEW_FAVORITES == ID_VIEW_SEARCH + 3 && ID_VIEW_HISTORY == ID_VIEW_SEARCH + 4);
	static_assert(ID_MACHINE_X64 == ID_MACHINE_X86 + 1 && ID_MACHINE_ARM64 == ID_MACHINE_X86 + 2);
	UISetRadioMenuItem(ID_VIEW_SEARCH + std::clamp(page, 0, 4), ID_VIEW_SEARCH, ID_VIEW_HISTORY);
	UIEnable(ID_VIEW_CLEARHISTORY, !m_HistoryNames.empty());
	UIEnable(ID_EDIT_FAVORITE, shows);
	UISetCheck(ID_EDIT_FAVORITE, shows && std::ranges::find(m_FavoriteNames, m_Tabs[target].View->Name()) != m_FavoriteNames.end());
	UISetRadioMenuItem(ID_MACHINE_X86 + (int)m_Machine, ID_MACHINE_X86, ID_MACHINE_ARM64);
	static_assert(ID_SHOW_LAYOUT == ID_SHOW_DECLARATION + 1 && ID_SHOW_COMPARE == ID_SHOW_DECLARATION + 2);
	UISetRadioMenuItem(ID_SHOW_DECLARATION + std::clamp(settings.ShowMode(), 0, 2), ID_SHOW_DECLARATION, ID_SHOW_COMPARE);
	auto count = (int)m_Tabs.size();
	UIEnable(ID_TAB_CLOSE, target >= 0);
	UIEnable(ID_TAB_CLOSEOTHERS, count > 1 && target >= 0);
	UIEnable(ID_TAB_CLOSEALL, count > 0);
	UIEnable(ID_TAB_NEXT, count > 1);
	UIEnable(ID_TAB_PREV, count > 1);
	UIEnable(ID_TAB_KEEPOPEN, target >= 0 && m_Tabs[target].Preview);
	UIUpdateToolBar();
}

void CMainFrame::SetMachine(Win32Api::Machine machine) {
	if (machine == m_Machine) {
		m_MachineBar.Combo().SetCurSel((int)machine);
		return;
	}
	CWaitCursor wait;
	auto api = Win32Api::Get(machine);
	if (!api) {
		m_MachineBar.Combo().SetCurSel((int)m_Machine);
		return;
	}
	api->Entries();
	m_Machine = machine;
	m_Api = api;
	m_Search->SetApi(*api);
	m_Namespaces->SetApi(*api);
	m_UsedBy->SetApi(*api);
	UpdateUsedBy();
	m_Favorites->SetApi(*api);
	m_History->SetApi(*api);
	UpdateLists();
	m_QuickOpen.SetApi(*api);
	if (m_Side)
		m_Side->SetApi(*api);
	m_SwitchingMachine = true;
	for (auto& tab : m_Tabs)
		tab.View->SetApi(*api);
	m_SwitchingMachine = false;
	m_MachineBar.Combo().SetCurSel((int)machine);
	AppSettings::Get().Machine((int)machine);
	UpdateDocumentation();
	UpdateStatus();
	UpdateTitle();
}

void CMainFrame::ApplyColors() {
	auto dark = AppSettings::Get().DarkMode();
	// (the mode may be the one asked for already: the controls are themed all the same)
	if (!WTLHelper::SwitchToMode(dark ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd))
		WTLHelper::ApplyMode(m_hWnd);
	SendMessageToDescendants(::RegisterWindowMessage(L"WTLHelperUpdateTheme"));
	m_Navigator.Strip().UpdateColors();
	m_Editors.Strip().UpdateColors();
	m_SideHost.Strip().UpdateColors();
	for (auto& tab : m_Tabs)
		tab.View->UpdateColors();
	if (m_Side)
		m_Side->UpdateColors();
	m_Doc.SetDarkMode(dark);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

//
// Commands
//

LRESULT CMainFrame::OnAppCommand(UINT, WPARAM, LPARAM lp, BOOL& handled) {
	BOOL dummy;
	switch (GET_APPCOMMAND_LPARAM(lp)) {
		case APPCOMMAND_BROWSER_BACKWARD:
			OnBack(0, ID_GO_BACK, nullptr, dummy);
			return TRUE;
		case APPCOMMAND_BROWSER_FORWARD:
			OnForward(0, ID_GO_FORWARD, nullptr, dummy);
			return TRUE;
	}
	handled = FALSE;
	return 0;
}

LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&) {
	AtlMessageBox(m_hWnd, L"Win32 API Explorer\n\nThe functions, types and constants of Windows, from the Win32 metadata "
		L"(Microsoft.Windows.SDK.Win32Metadata).\n\nCopyright (C) 2026 Pavel Yosifovich", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

namespace {
	bool IsEdit(HWND hWnd) {
		WCHAR name[32]{};
		::GetClassName(hWnd, name, _countof(name));
		return _wcsicmp(name, WC_EDIT) == 0;
	}
}

// the accelerators take Ctrl+C and Ctrl+A from the edits: they are given back
LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&) {
	auto focus = ::GetFocus();
	if (auto view = FocusedView(); view && (focus == view->m_hWnd || view->IsChild(focus)))
		view->Copy();
	else if (IsEdit(focus))
		::SendMessage(focus, WM_COPY, 0, 0);
	else {
		BOOL handled;
		OnCopyName(0, ID_EDIT_COPYNAME, nullptr, handled);
	}
	return 0;
}

LRESULT CMainFrame::OnSelectAll(WORD, WORD, HWND, BOOL&) {
	auto focus = ::GetFocus();
	if (auto view = FocusedView(); view && (focus == view->m_hWnd || view->IsChild(focus)))
		view->SelectAll();
	else if (IsEdit(focus))
		::SendMessage(focus, EM_SETSEL, 0, -1);
	return 0;
}

LRESULT CMainFrame::OnCopyName(WORD, WORD, HWND, BOOL&) {
	auto target = TargetTab();
	if (target < 0)
		return 0;
	auto name = m_Tabs[target].View->Name();
	if (!name.empty())
		ClipboardHelper::CopyText(m_hWnd, Wide(name).c_str());
	return 0;
}

LRESULT CMainFrame::OnFind(WORD, WORD, HWND, BOOL&) {
	ShowNavigatorPage(0);
	m_Search->FocusFilter();
	return 0;
}

LRESULT CMainFrame::OnViewNavigator(WORD, WORD id, HWND, BOOL&) {
	auto page = id - ID_VIEW_SEARCH;
	ShowNavigatorPage(page);
	::SetFocus(m_Navigator.GetPage());
	return 0;
}

LRESULT CMainFrame::OnQuickOpen(WORD, WORD, HWND, BOOL&) {
	m_QuickOpen.Show();
	return 0;
}

LRESULT CMainFrame::OnClearHistory(WORD, WORD, HWND, BOOL&) {
	m_HistoryNames.clear();
	UpdateLists();
	return 0;
}

// the tab's (whose menu is shown, else the active one): a favorite, or not any more
LRESULT CMainFrame::OnFavorite(WORD, WORD, HWND, BOOL&) {
	auto target = TargetTab();
	m_MenuTab = -1;
	auto name = target >= 0 ? m_Tabs[target].View->Name() : std::string();
	if (name.empty())
		return 0;
	if (auto it = std::ranges::find(m_FavoriteNames, name); it != m_FavoriteNames.end())
		m_FavoriteNames.erase(it);
	else
		m_FavoriteNames.push_back(name);
	UpdateLists();
	return 0;
}

LRESULT CMainFrame::OnFindUsages(WORD, WORD, HWND, BOOL&) {
	auto entry = UsagesTarget();
	m_MenuTab = -1;
	if (entry < 0)
		::MessageBeep(MB_ICONWARNING);
	else
		FindUsages(entry);
	return 0;
}

LRESULT CMainFrame::OnViewDocumentation(WORD, WORD, HWND, BOOL&) {
	auto& settings = AppSettings::Get();
	auto show = !settings.ShowDocumentation();
	settings.ShowDocumentation(show);
	m_Right.SetSinglePaneMode(show ? SPLIT_PANE_NONE : SPLIT_PANE_TOP);
	UpdateDocumentation();
	return 0;
}

LRESULT CMainFrame::OnViewDarkMode(WORD, WORD, HWND, BOOL&) {
	auto& settings = AppSettings::Get();
	settings.DarkMode(!settings.DarkMode());
	ApplyColors();
	return 0;
}

// the font of the declarations (and of their tips)
LRESULT CMainFrame::OnViewFont(WORD, WORD, HWND, BOOL&) {
	auto font = CDeclarationView::CodeFont();
	CFontDialog dlg(&font, CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS, nullptr, m_hWnd);
	if (!WTLHelper::InvokeFontDialog(dlg, m_hWnd))
		return 0;
	AppSettings::Get().CodeFont(dlg.m_lf);
	for (auto& tab : m_Tabs)
		tab.View->UpdateColors();
	if (m_Side)
		m_Side->UpdateColors();
	return 0;
}

LOGFONT CMainFrame::UIFont() {
	auto font = AppSettings::Get().UIFont();
	if (font.lfFaceName[0])
		return font;
	NONCLIENTMETRICS ncm{ sizeof(ncm) };
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	return ncm.lfMessageFont;
}

void CMainFrame::ApplyUIFont() {
	auto logFont = UIFont();
	bool custom = AppSettings::Get().UIFont().lfFaceName[0] != 0;
	CFont font;
	font.CreateFontIndirect(&logFont);
	m_Search->SetUIFont(font);
	m_UsedBy->SetUIFont(font);
	m_Favorites->SetUIFont(font);
	m_History->SetUIFont(font);
	m_Namespaces->SetUIFont(font);
	m_Navigator.Strip().SetFont(custom ? &logFont : nullptr);
	m_Editors.Strip().SetFont(custom ? &logFont : nullptr);
	m_SideHost.Strip().SetFont(custom ? &logFont : nullptr);
	m_Navigator.Relayout();
	m_Editors.Relayout();
	m_SideHost.Relayout();
	m_MachineBar.SetUIFont(font);
	m_QuickOpen.SetUIFont(font);
	m_Decode.SetUIFont(font);
	// (the machine's band as high as its list)
	CReBarCtrl rebar(m_hWndToolBar);
	REBARBANDINFO band{ sizeof(band) };
	band.fMask = RBBIM_CHILDSIZE;
	rebar.GetBandInfo(1, &band);
	band.cyMinChild = m_MachineBar.GetIdealHeight();
	rebar.SetBandInfo(1, &band);
	::SendMessage(m_hWndStatusBar, WM_SETFONT, (WPARAM)(HFONT)font, TRUE);
	// (the one the controls had goes when they have this one)
	if (!m_UIFont.IsNull())
		m_UIFont.DeleteObject();
	m_UIFont.Attach(font.Detach());
	UpdateLayout();
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

LRESULT CMainFrame::OnViewUIFont(WORD, WORD, HWND, BOOL&) {
	auto font = UIFont();
	CFontDialog dlg(&font, CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS, nullptr, m_hWnd);
	if (!WTLHelper::InvokeFontDialog(dlg, m_hWnd))
		return 0;
	AppSettings::Get().UIFont(dlg.m_lf);
	ApplyUIFont();
	return 0;
}

LRESULT CMainFrame::OnResetFonts(WORD, WORD, HWND, BOOL&) {
	auto& settings = AppSettings::Get();
	settings.CodeFont(LOGFONT{});
	settings.UIFont(LOGFONT{});
	for (auto& tab : m_Tabs)
		tab.View->UpdateColors();
	if (m_Side)
		m_Side->UpdateColors();
	ApplyUIFont();
	return 0;
}

// what the tabs show of structs and unions (all of them: as a way of looking at them)
LRESULT CMainFrame::OnShowMode(WORD, WORD id, HWND, BOOL&) {
	auto mode = id - ID_SHOW_DECLARATION;
	AppSettings::Get().ShowMode(mode);
	for (auto& tab : m_Tabs)
		tab.View->SetMode((CDeclarationView::Mode)mode);
	if (m_Side)
		m_Side->SetMode((CDeclarationView::Mode)mode);
	return 0;
}

LRESULT CMainFrame::OnBack(WORD, WORD, HWND, BOOL&) {
	if (auto view = FocusedView())
		view->GoBack();
	return 0;
}

LRESULT CMainFrame::OnForward(WORD, WORD, HWND, BOOL&) {
	if (auto view = FocusedView())
		view->GoForward();
	return 0;
}

LRESULT CMainFrame::OnGoToDefinition(WORD, WORD id, HWND, BOOL&) {
	auto view = FocusedView();
	if (!view || !view->FollowCaret(id == ID_GO_DEFINITION_NEWTAB))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

// in the tree (a constant, which the tree does not have: in the search)
LRESULT CMainFrame::OnReveal(WORD, WORD, HWND, BOOL&) {
	auto target = TargetTab();
	auto entry = target >= 0 ? m_Tabs[target].View->Entry() : -1;
	if (entry < 0)
		return 0;
	if (m_Api->Entries()[entry].Kind == Win32Api::Entry::Kinds::Constant) {
		ShowNavigatorPage(0);
		if (m_Search->SelectEntry(entry))
			m_Search->SetFocus();
		return 0;
	}
	ShowNavigatorPage(1);
	if (m_Namespaces->SelectEntry(entry))
		m_Namespaces->SetFocus();
	return 0;
}

// its page, or the search of the documentation for it if it has none
LRESULT CMainFrame::OnOpenInBrowser(WORD, WORD, HWND, BOOL&) {
	auto target = TargetTab();
	auto entry = target >= 0 ? m_Tabs[target].View->Entry() : -1;
	if (entry < 0)
		return 0;
	auto const& e = m_Api->Entries()[entry];
	// (the metadata's page, else the one the documentation found, else the search for it)
	auto url = m_Api->DocumentationUrl(e);
	if (url.empty())
		url = CDocPane::FoundPage(Wide(e.Name));
	if (url.empty())
		url = CDocPane::SearchUrl(Wide(e.Name));
	::ShellExecute(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CMainFrame::OnMachine(WORD, WORD id, HWND, BOOL&) {
	SetMachine((Win32Api::Machine)(id - ID_MACHINE_X86));
	return 0;
}

LRESULT CMainFrame::OnMachineCombo(WORD, WORD, HWND, BOOL&) {
	auto index = m_MachineBar.Combo().GetCurSel();
	if (index >= 0)
		SetMachine((Win32Api::Machine)index);
	return 0;
}

LRESULT CMainFrame::OnCloseTab(WORD, WORD, HWND, BOOL&) {
	CloseTab(TargetTab());
	return 0;
}

LRESULT CMainFrame::OnCloseOtherTabs(WORD, WORD, HWND, BOOL&) {
	auto keep = TargetTab();
	if (keep < 0)
		return 0;
	m_MenuTab = -1;
	for (int i = (int)m_Tabs.size() - 1; i > keep; i--)
		CloseTab(i);
	for (int i = keep - 1; i >= 0; i--)
		CloseTab(i);
	ActivateTab(0);
	return 0;
}

LRESULT CMainFrame::OnCloseAllTabs(WORD, WORD, HWND, BOOL&) {
	m_MenuTab = -1;
	while (!m_Tabs.empty())
		CloseTab((int)m_Tabs.size() - 1);
	return 0;
}

LRESULT CMainFrame::OnNextTab(WORD, WORD id, HWND, BOOL&) {
	auto count = (int)m_Tabs.size();
	if (count < 2)
		return 0;
	auto step = id == ID_TAB_NEXT ? 1 : count - 1;
	ActivateTab((std::max(ActiveTab(), 0) + step) % count);
	return 0;
}

LRESULT CMainFrame::OnKeepOpen(WORD, WORD, HWND, BOOL&) {
	Pin(TargetTab());
	return 0;
}
