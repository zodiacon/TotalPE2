#include "pch.h"
#include "MainFrm.h"
#include "AppSettings.h"
#include "AboutDlg.h"
#include <Images.h>
#include <ToolbarHelper.h>
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
}

CMainFrame::CMainFrame(std::wstring startName) : m_StartName(std::move(startName)) {
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (m_Browser && m_Browser->PreTranslateMessage(pMsg, m_hWnd))
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

	m_Images = Images::Create({ IDI_FUNCTION, IDI_FUNCTION2, IDI_TYPE, IDI_UNION, IDI_ENUM, IDI_DATA, IDI_INTERFACE, IDI_FIELD,
		IDI_NAMESPACE, IDI_FOLDER_CLOSED, IDI_FOLDER });
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
	CApiBrowser::State state;
	state.ShowDocumentation = settings.ShowDocumentation();
	state.DocumentationPercent = settings.DocumentationPercent();
	state.NavigatorWidth = settings.NavigatorWidth();
	state.NavigatorPage = settings.NavigatorPage();
	state.ShowMode = settings.ShowMode();
	state.OpenTabs = settings.OpenTabs();
	state.ActiveTab = settings.ActiveTab();
	state.Favorites = settings.Favorites();
	state.History = settings.History();
	CDeclarationView::SetCodeFont(settings.CodeFont());

	m_Browser = std::make_unique<CApiBrowser>(*m_Api, m_Images, std::move(state));
	m_Browser->OnStatus = [this](std::wstring const& text) {
		m_StatusText = text;
		UpdateStatus();
	};
	m_Browser->OnTitleChanged = [this] { UpdateTitle(); };
	m_Loading.DestroyWindow();
	m_hWndClient = m_Browser->Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	UpdateLayout();

	// (the default font: as the controls have it)
	if (settings.UIFont().lfFaceName[0])
		ApplyUIFont();
	if (!m_StartName.empty())
		OpenByName(m_StartName);
	UpdateStatus();
	UpdateTitle();
	UpdateUIState();
}

bool CMainFrame::OpenByName(std::wstring const& name) {
	return m_Browser->OpenByName(Narrow(name));
}

// Another instance's name (it is not started again): opened here, now or when the metadata is loaded; this window to the front
LRESULT CMainFrame::OnCopyData(UINT, WPARAM, LPARAM lp, BOOL&) {
	auto data = (COPYDATASTRUCT const*)lp;
	if (!data || data->dwData != CopyDataName)
		return FALSE;
	std::wstring name((PCWSTR)data->lpData, data->cbData / sizeof(WCHAR));
	if (!name.empty()) {
		if (m_Browser)
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

// While the metadata is loaded: Exit and About only. Then the browser's commands (of the menus, the toolbar) are its own.
LRESULT CMainFrame::OnCommand(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
	auto id = LOWORD(wp);
	if (m_Browser) {
		if (!CApiBrowser::IsCommand(id)) {
			handled = FALSE;
			return 0;
		}
		return m_Browser->SendMessage(msg, wp, lp);
	}
	handled = id == ID_APP_EXIT || id == ID_APP_ABOUT ? FALSE : TRUE;
	// (the machine is the one being loaded)
	if (id == IDC_MACHINE)
		m_MachineBar.Combo().SetCurSel((int)m_Machine);
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

LRESULT CMainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	// (closed while the metadata is loaded: when it is)
	if (m_Loader.joinable())
		m_Loader.join();
	if (m_Browser)
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
	auto state = m_Browser->GetState();
	settings.NavigatorWidth(state.NavigatorWidth);
	settings.DocumentationPercent(state.DocumentationPercent);
	settings.ShowDocumentation(state.ShowDocumentation);
	settings.NavigatorPage(state.NavigatorPage);
	settings.ShowMode(state.ShowMode);
	settings.OpenTabs(state.OpenTabs);
	settings.ActiveTab(state.ActiveTab);
	settings.Favorites(state.Favorites);
	settings.History(state.History);
}

void CMainFrame::UpdateStatus() {
	if (!m_hWndStatusBar || !m_Browser)
		return;
	CStatusBarCtrl status(m_hWndStatusBar);
	status.SetText(0, m_StatusText.c_str());
	status.SetText(1, std::format(L"Machine: {}", s_Machines[(int)m_Machine]).c_str());
}

void CMainFrame::UpdateTitle() {
	CString title;
	title.LoadString(IDR_MAINFRAME);
	auto name = m_Browser ? m_Browser->Title() : std::string();
	if (!name.empty())
		title += L" - " + CString(name.c_str());
	SetWindowText(title);
}

void CMainFrame::UpdateUIState() {
	if (!m_Browser)
		return;
	m_Browser->UpdateUI(*this);
	UISetCheck(ID_VIEW_DARKMODE, AppSettings::Get().DarkMode());
	static_assert(ID_MACHINE_X64 == ID_MACHINE_X86 + 1 && ID_MACHINE_ARM64 == ID_MACHINE_X86 + 2);
	UISetRadioMenuItem(ID_MACHINE_X86 + (int)m_Machine, ID_MACHINE_X86, ID_MACHINE_ARM64);
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
	m_Machine = machine;
	m_Api = api;
	m_Browser->SetApi(*api);
	m_MachineBar.Combo().SetCurSel((int)machine);
	AppSettings::Get().Machine((int)machine);
	UpdateStatus();
}

void CMainFrame::ApplyColors() {
	auto dark = AppSettings::Get().DarkMode();
	// (the mode may be the one asked for already: the controls are themed all the same)
	if (!WTLHelper::SwitchToMode(dark ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd))
		WTLHelper::ApplyMode(m_hWnd);
	SendMessageToDescendants(::RegisterWindowMessage(L"WTLHelperUpdateTheme"));
	m_Browser->UpdateColors(dark);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

//
// Commands
//

LRESULT CMainFrame::OnAppCommand(UINT, WPARAM, LPARAM lp, BOOL& handled) {
	if (m_Browser) {
		switch (GET_APPCOMMAND_LPARAM(lp)) {
			case APPCOMMAND_BROWSER_BACKWARD:
				m_Browser->GoBack();
				return TRUE;
			case APPCOMMAND_BROWSER_FORWARD:
				m_Browser->GoForward();
				return TRUE;
		}
	}
	handled = FALSE;
	return 0;
}

LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&) {
	CAboutDlg dlg(m_Api);
	dlg.DoModal(m_hWnd);
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
	CDeclarationView::SetCodeFont(dlg.m_lf);
	m_Browser->UpdateCodeFont();
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
	m_Browser->SetUIFont(font, custom ? &logFont : nullptr);
	m_MachineBar.SetUIFont(font);
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
	CDeclarationView::SetCodeFont(LOGFONT{});
	m_Browser->UpdateCodeFont();
	ApplyUIFont();
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
