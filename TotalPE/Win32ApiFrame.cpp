#include "pch.h"
#include "Win32ApiFrame.h"
#include "AppSettings.h"
#include "Interfaces.h"
#include "Fonts.h"
#include <Images.h>
#include <ToolbarHelper.h>
#include <WTLHelper.h>

CWin32ApiFrame::CWin32ApiFrame(IMainFrame* main, std::function<void()> closed) : m_Main(main), m_Closed(std::move(closed)) {
}

LRESULT CWin32ApiFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	// (in WM_CREATE: dark mode's hook themes the frame's controls once it has handled it, as the main window's)
	CreateToolBar();
	CreateSimpleStatusBar(L"");
	auto loop = _Module.GetMessageLoop();
	loop->AddMessageFilter(this);
	loop->AddIdleHandler(this);
	handled = FALSE;
	return 0;
}

void CWin32ApiFrame::CreateToolBar() {
	ToolBarButtonInfo const buttons[] = {
		{ ID_GO_BACK, IDI_BACK },
		{ ID_GO_FORWARD, IDI_FORWARD },
		{ 0 },
		{ ID_GO_REVEAL, IDI_SYSTEM, BTNS_BUTTON | BTNS_AUTOSIZE, L"Reveal" },
		{ ID_GO_USEDBY, IDI_FIND, BTNS_BUTTON | BTNS_AUTOSIZE, L"Find Usages" },
		{ ID_WIN32_REFERENCES, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"References" },
		{ 0 },
		{ ID_SHOW_DECLARATION, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Declaration" },
		{ ID_SHOW_LAYOUT, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Layout" },
		{ ID_SHOW_COMPARE, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Compare" },
		{ 0 },
		{ ID_VIEW_DOCUMENTATION, IDI_GLOBE, BTNS_CHECK | BTNS_AUTOSIZE, L"Documentation" },
		{ ID_DOC_BROWSER, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Open in Browser" },
		{ ID_WIN32_EXPLORER, 0, BTNS_BUTTON | BTNS_AUTOSIZE, L"Open in Explorer" },
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_ToolBar = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 16);
	AddSimpleReBarBand(m_ToolBar);
	UIAddToolBar(m_ToolBar);
}

bool CWin32ApiFrame::Open(Win32Api& api, std::unordered_set<std::string> imports, CString const& title) {
	if (!Create(nullptr, rcDefault, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN))
		return false;
	m_Title = title;
	auto& settings = AppSettings::Get();
	CApiBrowser::State state;
	state.ShowDocumentation = settings.Win32ApiDocumentation();
	state.DocumentationPercent = settings.Win32ApiDocumentationPercent();
	state.NavigatorWidth = settings.Win32ApiNavigatorWidth();
	state.NavigatorPage = settings.Win32ApiNavigatorPage();
	state.ShowMode = settings.Win32ApiShowMode();
	state.OpenTabs = settings.Win32ApiTabs();
	state.ActiveTab = settings.Win32ApiActiveTab();
	state.Favorites = settings.Win32ApiFavorites();
	state.History = settings.Win32ApiHistory();
	CDeclarationView::SetCodeFont(Fonts::Code());

	m_Images = Images::Create({ IDI_FUNCTION, IDI_FUNCTION2, IDI_TYPE, IDI_UNION, IDI_ENUM, IDI_DATA, IDI_INTERFACE, IDI_FIELD,
		IDI_SYSTEM, IDI_FOLDER_CLOSED, IDI_DIR_OPEN });
	m_Browser = std::make_unique<CApiBrowser>(api, m_Images, std::move(state));
	m_Browser->OnStatus = [this](std::wstring const& text) { ::SetWindowText(m_hWndStatusBar, text.c_str()); };
	m_Browser->OnTitleChanged = [this] { UpdateTitle(); };
	m_Browser->OnExtendMenu = [this](CMenu& menu) {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING | (CanShowReferences() ? 0 : MF_GRAYED), ID_WIN32_REFERENCES, L"&References in the File");
		menu.AppendMenu(MF_STRING, ID_WIN32_EXPLORER, L"Open in &Explorer");
	};
	SetImports(std::move(imports));
	m_hWndClient = m_Browser->Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	if (!m_hWndClient)
		return false;
	ApplyFonts();

	// where it was the last time (else, of a size that shows the list and the declarations)
	auto wp = settings.Win32ApiWindowPlacement();
	if (wp.length == sizeof(wp) && wp.showCmd != SW_HIDE) {
		if (wp.showCmd == SW_SHOWMINIMIZED)
			wp.showCmd = SW_SHOWNORMAL;
		SetWindowPlacement(&wp);
	}
	else {
		SetWindowPos(nullptr, 0, 0, 1300, 800, SWP_NOMOVE | SWP_NOZORDER);
		CenterWindow();
		ShowWindow(SW_SHOW);
	}
	UpdateLayout();
	UpdateTitle();
	m_Browser->SetFocus();
	return true;
}

void CWin32ApiFrame::SetImports(std::unordered_set<std::string> imports) {
	m_Imports = std::move(imports);
	std::vector<std::string> names(m_Imports.begin(), m_Imports.end());
	std::ranges::sort(names);
	// (without a file, or of one that imports nothing: no page)
	m_Browser->SetHostList(names.empty() ? L"" : L"Imports", names, L"the functions the file imports");
}

void CWin32ApiFrame::SetFile(Win32Api& api, std::unordered_set<std::string> imports, CString const& title) {
	m_Title = title;
	m_Browser->SetApi(api);
	SetImports(std::move(imports));
	UpdateTitle();
}

bool CWin32ApiFrame::Select(std::string const& name) {
	return m_Browser->OpenByName(name);
}

void CWin32ApiFrame::ShowDocumentation() {
	m_Browser->ShowDocumentation(true);
}

void CWin32ApiFrame::Activate() {
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	::SetForegroundWindow(m_hWnd);
}

void CWin32ApiFrame::UpdateTitle() {
	auto name = m_Browser ? m_Browser->Title() : std::string();
	SetWindowText(name.empty() ? m_Title : m_Title + L" - " + CString(name.c_str()));
}

BOOL CWin32ApiFrame::PreTranslateMessage(MSG* pMsg) {
	return m_Browser && m_Browser->PreTranslateMessage(pMsg, m_hWnd);
}

BOOL CWin32ApiFrame::OnIdle() {
	if (!m_Browser)
		return FALSE;
	m_Browser->UpdateUI(*this);
	UIEnable(ID_WIN32_REFERENCES, CanShowReferences());
	UIUpdateToolBar();
	return FALSE;
}

LRESULT CWin32ApiFrame::OnCommand(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
	if (!m_Browser || !CApiBrowser::IsCommand(LOWORD(wp))) {
		handled = FALSE;
		return 0;
	}
	return m_Browser->SendMessage(msg, wp, lp);
}

void CWin32ApiFrame::SaveState() {
	auto& settings = AppSettings::Get();
	WINDOWPLACEMENT wp{ sizeof(wp) };
	GetWindowPlacement(&wp);
	settings.Win32ApiWindowPlacement(wp);
	if (!m_Browser || !m_Browser->IsWindow())
		return;
	auto state = m_Browser->GetState();
	settings.Win32ApiDocumentation(state.ShowDocumentation);
	settings.Win32ApiDocumentationPercent(state.DocumentationPercent);
	settings.Win32ApiNavigatorWidth(state.NavigatorWidth);
	settings.Win32ApiNavigatorPage(state.NavigatorPage);
	settings.Win32ApiShowMode(state.ShowMode);
	settings.Win32ApiTabs(state.OpenTabs);
	settings.Win32ApiActiveTab(state.ActiveTab);
	settings.Win32ApiFavorites(state.Favorites);
	settings.Win32ApiHistory(state.History);
}

LRESULT CWin32ApiFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
	SaveState();
	auto loop = _Module.GetMessageLoop();
	loop->RemoveMessageFilter(this);
	loop->RemoveIdleHandler(this);
	// (not to the frame's: for a window with no parent, it ends the application)
	return 0;
}

LRESULT CWin32ApiFrame::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Browser && m_Browser->IsWindow())
		m_Browser->SetFocus();
	return 0;
}

// the mouse's Back and Forward buttons
LRESULT CWin32ApiFrame::OnAppCommand(UINT, WPARAM, LPARAM lParam, BOOL& handled) {
	if (m_Browser) {
		switch (GET_APPCOMMAND_LPARAM(lParam)) {
			case APPCOMMAND_BROWSER_BACKWARD: m_Browser->GoBack(); return TRUE;
			case APPCOMMAND_BROWSER_FORWARD: m_Browser->GoForward(); return TRUE;
		}
	}
	handled = FALSE;
	return 0;
}

// (also when the fonts or the colors of code change: the main window asks all the windows to update)
LRESULT CWin32ApiFrame::OnUpdateDarkMode(UINT, WPARAM, LPARAM, BOOL&) {
	auto dark = AppSettings::Get().DarkMode();
	// (another top-level window may have switched the mode already: this one's controls are themed all the same)
	if (!WTLHelper::SwitchToMode(dark ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd))
		WTLHelper::ApplyMode(m_hWnd);
	SendMessageToDescendants(::RegisterWindowMessage(L"WTLHelperUpdateTheme"));
	if (m_Browser) {
		m_Browser->UpdateColors(dark);
		ApplyFonts();
	}
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
	return 0;
}

void CWin32ApiFrame::ApplyFonts() {
	CDeclarationView::SetCodeFont(Fonts::Code());
	m_Browser->UpdateCodeFont();
	LOGFONT logFont;
	if (!Fonts::Application(logFont))
		return;
	CFont font;
	font.CreateFontIndirect(&logFont);
	m_Browser->SetUIFont(font, &logFont);
	// (the one the controls had goes when they have this one)
	if (!m_UIFont.IsNull())
		m_UIFont.DeleteObject();
	m_UIFont.Attach(font.Detach());
}

bool CWin32ApiFrame::CanShowReferences() const {
	auto entry = m_Browser ? m_Browser->TargetEntry() : -1;
	if (entry < 0)
		return false;
	auto const& e = m_Browser->Api().Entries()[entry];
	return e.Kind == Win32Api::Entry::Kinds::Function && m_Imports.contains(e.Name);
}

LRESULT CWin32ApiFrame::OnReferences(WORD, WORD, HWND, BOOL&) {
	if (!CanShowReferences() || !m_Main->ShowImportReferences(m_Browser->Api().Entries()[m_Browser->TargetEntry()].Name))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

bool CWin32ApiFrame::OpenInExplorer(std::string const& name) {
	WCHAR path[MAX_PATH];
	::GetModuleFileName(nullptr, path, _countof(path));
	std::wstring exe = path;
	exe = exe.substr(0, exe.find_last_of(L'\\') + 1) + L"Win32ApiExp.exe";
	if (::GetFileAttributes(exe.c_str()) == INVALID_FILE_ATTRIBUTES)
		return false;
	std::wstring arguments(name.begin(), name.end());
	return (INT_PTR)::ShellExecute(nullptr, L"open", exe.c_str(), arguments.empty() ? nullptr : arguments.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}

LRESULT CWin32ApiFrame::OnOpenInExplorer(WORD, WORD, HWND, BOOL&) {
	auto entry = m_Browser ? m_Browser->TargetEntry() : -1;
	if (!OpenInExplorer(entry >= 0 ? m_Browser->Api().Entries()[entry].Name : std::string()))
		AtlMessageBox(m_hWnd, L"Win32 API Explorer (Win32ApiExp.exe) is not next to TotalPE.exe.", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

void CWin32ApiFrame::OnFinalMessage(HWND) {
	if (m_Closed)
		m_Closed();
	delete this;
}
