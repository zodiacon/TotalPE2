#include "pch.h"
#include "ApiBrowser.h"
#include "Images.h"
#include <ClipboardHelper.h>
#include <WTLHelper.h>

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

	constexpr int DefaultNavigatorWidth = 440;

	bool IsEdit(HWND hWnd) {
		WCHAR name[32]{};
		::GetClassName(hWnd, name, _countof(name));
		return _wcsicmp(name, WC_EDIT) == 0;
	}
}

CApiBrowser::CApiBrowser(Win32Api& api, HIMAGELIST images, State state) : m_Api(&api), m_Images(images), m_State(std::move(state)) {
	for (auto const& name : m_State.Favorites)
		m_FavoriteNames.push_back(Narrow(name));
	for (auto const& name : m_State.History)
		m_HistoryNames.push_back(Narrow(name));
}

bool CApiBrowser::IsCommand(UINT id) {
	return (id >= ID_APIBROWSER_FIRST && id <= ID_APIBROWSER_LAST) || id == ID_EDIT_COPY || id == ID_EDIT_SELECT_ALL || id == ID_EDIT_FIND;
}

HACCEL CApiBrowser::Accelerators() {
	static HACCEL accelerators = [] {
		constexpr BYTE Ctrl = FVIRTKEY | FCONTROL, CtrlShift = FVIRTKEY | FCONTROL | FSHIFT, Shift = FVIRTKEY | FSHIFT;
		ACCEL keys[] = {
			{ Ctrl, 'C', ID_EDIT_COPY },
			{ Ctrl, 'A', ID_EDIT_SELECT_ALL },
			{ CtrlShift, 'C', ID_EDIT_COPYNAME },
			{ CtrlShift, 'V', ID_EDIT_DECODE },
			{ Ctrl, 'F', ID_EDIT_FIND },
			{ Ctrl, 'Q', ID_EDIT_FIND },
			{ Ctrl, 'P', ID_GO_QUICKOPEN },
			{ Ctrl, VK_OEM_5, ID_GO_SIDE },
			{ Ctrl, '1', ID_VIEW_SEARCH },
			{ Ctrl, '2', ID_VIEW_NAMESPACES },
			{ Ctrl, '3', ID_VIEW_USEDBY },
			{ Ctrl, '4', ID_VIEW_FAVORITES },
			{ Ctrl, '5', ID_VIEW_HISTORY },
			{ Ctrl, '6', ID_VIEW_HOSTLIST },
			{ CtrlShift, 'A', ID_EDIT_FAVORITE },
			{ Ctrl, 'D', ID_VIEW_DOCUMENTATION },
			{ CtrlShift, 'D', ID_SHOW_DECLARATION },
			{ CtrlShift, 'L', ID_SHOW_LAYOUT },
			{ CtrlShift, 'M', ID_SHOW_COMPARE },
			{ FVIRTKEY | FALT, VK_LEFT, ID_GO_BACK },
			{ FVIRTKEY | FALT, VK_RIGHT, ID_GO_FORWARD },
			{ FVIRTKEY, VK_F12, ID_GO_DEFINITION },
			{ Ctrl, VK_F12, ID_GO_DEFINITION_NEWTAB },
			{ Shift, VK_F12, ID_GO_USEDBY },
			{ CtrlShift, 'E', ID_GO_REVEAL },
			{ CtrlShift, 'B', ID_DOC_BROWSER },
			{ Ctrl, 'K', ID_TAB_KEEPOPEN },
			{ Ctrl, 'W', ID_TAB_CLOSE },
			{ Ctrl, VK_F4, ID_TAB_CLOSE },
			{ Ctrl, VK_TAB, ID_TAB_NEXT },
			{ CtrlShift, VK_TAB, ID_TAB_PREV },
		};
		return ::CreateAcceleratorTable(keys, _countof(keys));
	}();
	return accelerators;
}

BOOL CApiBrowser::PreTranslateMessage(MSG* pMsg, HWND hScope) {
	if (!m_hWnd)
		return FALSE;
	if (m_QuickOpen.PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Decode.PreTranslateMessage(pMsg))
		return TRUE;
	for (auto pane : { m_Search.get(), m_UsedBy.get(), m_Favorites.get(), m_History.get(), m_HostList.get() })
		if (pane && pane->PreTranslateMessage(pMsg))
			return TRUE;
	if (m_Namespaces && m_Namespaces->PreTranslateMessage(pMsg))
		return TRUE;
	if (auto view = ActiveView(); view && view->PreTranslateMessage(pMsg))
		return TRUE;
	if (m_Side && m_Side->PreTranslateMessage(pMsg))
		return TRUE;
	// (the keys of the frame's windows: not of other windows of the thread)
	if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST && hScope &&
		(pMsg->hwnd == hScope || ::IsChild(hScope, pMsg->hwnd)))
		return ::TranslateAccelerator(m_hWnd, Accelerators(), pMsg);
	return FALSE;
}

LRESULT CApiBrowser::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	// (popups: of the frame, at the top of it)
	m_QuickOpen.Create(m_hWnd, rcDefault, nullptr, WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW);
	m_QuickOpen.SetApi(*m_Api);
	m_QuickOpen.SetImageList(m_Images);
	m_QuickOpen.OnOpen = [this](size_t entry) {
		OpenEntry(entry, true);
		if (auto view = ActiveView())
			view->SetFocus();
	};
	m_Decode.Create(m_hWnd, rcDefault, nullptr, WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW);

	m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	CreateNavigator();
	CreateEditors();
	m_Splitter.SetSplitterExtendedStyle(0, SPLIT_PROPORTIONAL);	// (the left side keeps its width as the window is sized)
	m_Splitter.SetSplitterPanes(m_Navigator, m_Right);

	UpdateLists();
	// (the Used By page is of a type asked for)
	ShowNavigatorPage(m_State.NavigatorPage == UsedByPage ? SearchPage : m_State.NavigatorPage);
	RestoreTabs();
	UpdateStatus();
	return 0;
}

LRESULT CApiBrowser::OnSize(UINT, WPARAM, LPARAM lp, BOOL&) {
	int cx = GET_X_LPARAM(lp), cy = GET_Y_LPARAM(lp);
	if (!m_Splitter)
		return 0;
	m_Splitter.SetWindowPos(nullptr, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
	// (where the splitters were: once there is room for them)
	static constexpr int Laid = -1;
	if (m_State.NavigatorWidth != Laid && cx > 0 && cy > 0) {
		auto width = m_State.NavigatorWidth;
		m_Splitter.SetSplitterPos(width > 100 ? width : DefaultNavigatorWidth);
		m_Right.SetSplitterPosPct(100 - std::clamp(m_State.DocumentationPercent, 10, 90));
		m_State.NavigatorWidth = Laid;
	}
	return 0;
}

LRESULT CApiBrowser::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (auto view = ActiveView())
		view->SetFocus();
	else if (auto page = m_Navigator.GetPage())
		::SetFocus(page);
	return 0;
}

LRESULT CApiBrowser::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	// (the state is asked for before: the windows are there)
	if (m_QuickOpen)
		m_QuickOpen.DestroyWindow();
	if (m_Decode)
		m_Decode.DestroyWindow();
	handled = FALSE;
	return 0;
}

CApiBrowser::State CApiBrowser::GetState() const {
	auto state = m_State;
	if (m_Splitter) {
		state.NavigatorWidth = m_Splitter.GetSplitterPos();
		if (state.ShowDocumentation)
			state.DocumentationPercent = 100 - m_Right.GetSplitterPosPct();
	}
	if (state.NavigatorWidth < 0)
		state.NavigatorWidth = 0;
	// the tabs that stay (not the preview)
	state.OpenTabs.clear();
	state.ActiveTab = -1;
	for (int i = 0; i < (int)m_Tabs.size(); i++) {
		auto const& tab = m_Tabs[i];
		auto name = tab.View->Name();
		if (tab.Preview || name.empty())
			continue;
		if (i == ActiveTab())
			state.ActiveTab = (int)state.OpenTabs.size();
		state.OpenTabs.push_back(Wide(name));
	}
	state.Favorites.clear();
	for (auto const& name : m_FavoriteNames)
		state.Favorites.push_back(Wide(name));
	state.History.clear();
	for (auto const& name : m_HistoryNames)
		state.History.push_back(Wide(name));
	return state;
}

void CApiBrowser::RestoreTabs() {
	for (auto const& name : m_State.OpenTabs)
		if (auto entry = m_Api->Find(Narrow(name)))
			AddTab((int)(entry - m_Api->Entries().data()), false, (int)m_Tabs.size());
	if (m_Tabs.empty())
		return;
	auto active = m_State.ActiveTab;
	ActivateTab(active >= 0 && active < (int)m_Tabs.size() ? active : (int)m_Tabs.size() - 1);
}

void CApiBrowser::CreateNavigator() {
	m_Navigator.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	auto& strip = m_Navigator.Strip();
	strip.SetClosable(false);
	for (auto name : { L"Search", L"Namespaces", L"Used By", L"Favorites", L"History" })
		strip.Add(name);
	if (!m_HostTitle.empty())
		strip.Add(m_HostTitle);
	strip.OnSelected = [this](int page) { ShowNavigatorPage(page); };

	// (from any: a preview, or a tab that stays, which gets the focus)
	auto open = [this](size_t entry, bool pin, std::string const& name) {
		OpenEntry(entry, pin, name);
		if (auto view = ActiveView(); view && pin)
			view->SetFocus();
	};
	auto list = [&] {
		auto pane = std::make_unique<CSearchPane>(*m_Api);
		pane->Create(m_Navigator, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
		pane->SetImageList(m_Images);
		pane->OnOpen = open;
		pane->OnCountChanged = [this] { UpdateStatus(); };
		return pane;
	};
	m_Search = list();

	m_Namespaces = std::make_unique<CNamespacePane>(*m_Api);
	m_Namespaces->Create(m_Navigator, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Namespaces->SetImageList(m_Images);
	m_Namespaces->OnOpen = [open](size_t entry, bool pin) { open(entry, pin, {}); };

	m_UsedBy = list();
	m_UsedBy->SetScope(&m_NoEntries, L"what uses a type (Shift+F12 finds it)");
	m_Favorites = list();
	m_History = list();
	m_HostList = list();
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

void CApiBrowser::CreateEditors() {
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

	m_Doc.SetDarkMode(WTLHelper::IsDarkMode());
	m_Doc.Create(m_Right, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Right.SetSplitterPanes(m_EditorSplit, m_Doc);
	if (!m_State.ShowDocumentation)
		m_Right.SetSinglePaneMode(SPLIT_PANE_TOP);
}

std::unique_ptr<CDeclarationView> CApiBrowser::CreateView(HWND hParent) {
	auto view = std::make_unique<CDeclarationView>(*m_Api);
	view->OnExtendMenu = [this](CMenu& menu) {
		// (as the commands are now; then the host's)
		for (auto const& command : CommandStates()) {
			menu.EnableMenuItem(command.Id, MF_BYCOMMAND | (command.Enabled ? MF_ENABLED : MF_GRAYED));
			if (command.Check >= 0)
				menu.CheckMenuItem(command.Id, MF_BYCOMMAND | (command.Check ? MF_CHECKED : MF_UNCHECKED));
		}
		if (OnExtendMenu)
			OnExtendMenu(menu);
	};
	view->Create(hParent, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	view->SetMode((CDeclarationView::Mode)std::clamp(m_State.ShowMode, 0, 2));
	view->SetSingleLine(m_State.SingleLine);
	return view;
}

void CApiBrowser::SendCommand(UINT id) {
	if (IsCommand(id))
		SendMessage(WM_COMMAND, id);
	else
		GetTopLevelParent().SendMessage(WM_COMMAND, id);
}

//
// What the host asks for
//

Win32Api& CApiBrowser::Api() const {
	return *m_Api;
}

bool CApiBrowser::OpenByName(std::string const& name) {
	auto entry = m_Api->Find(name);
	if (!entry)
		return false;
	auto index = (size_t)(entry - m_Api->Entries().data());
	OpenEntry(index, true);
	m_Search->SelectEntry(index);
	if (auto view = ActiveView())
		view->SetFocus();
	return true;
}

int CApiBrowser::TargetEntry() const {
	if (m_MenuTab >= 0 && m_MenuTab < (int)m_Tabs.size())
		return m_Tabs[m_MenuTab].View->Entry();
	auto view = FocusedView();
	return view ? view->Entry() : -1;
}

std::string CApiBrowser::Title() const {
	auto view = ActiveView();
	return view ? view->Name() : std::string();
}

void CApiBrowser::ShowDocumentation(bool show) {
	m_State.ShowDocumentation = show;
	if (!m_hWnd)
		return;
	m_Right.SetSinglePaneMode(show ? SPLIT_PANE_NONE : SPLIT_PANE_TOP);
	UpdateDocumentation();
}

void CApiBrowser::SetHostList(std::wstring const& title, std::vector<std::string> const& names, std::wstring const& description) {
	m_HostTitle = title;
	m_HostNames = title.empty() ? std::vector<std::string>() : names;
	m_HostDescription = description;
	if (!m_hWnd)
		return;
	auto& strip = m_Navigator.Strip();
	if (title.empty() && strip.GetCount() > HostPage) {
		if (strip.GetSelected() == HostPage)
			ShowNavigatorPage(SearchPage);
		strip.Remove(HostPage);
	}
	else if (!title.empty()) {
		if (strip.GetCount() > HostPage)
			strip.SetText(HostPage, title);
		else
			strip.Add(title);
	}
	m_Navigator.Relayout();
	UpdateLists();
}

void CApiBrowser::UpdateColors(bool dark) {
	m_Navigator.Strip().UpdateColors();
	m_Editors.Strip().UpdateColors();
	m_SideHost.Strip().UpdateColors();
	for (auto& tab : m_Tabs)
		tab.View->UpdateColors();
	if (m_Side)
		m_Side->UpdateColors();
	m_Doc.SetDarkMode(dark);
}

void CApiBrowser::UpdateCodeFont() {
	for (auto& tab : m_Tabs)
		tab.View->UpdateColors();
	if (m_Side)
		m_Side->UpdateColors();
}

void CApiBrowser::SetUIFont(HFONT font, LOGFONT const* custom) {
	for (auto pane : { m_Search.get(), m_UsedBy.get(), m_Favorites.get(), m_History.get(), m_HostList.get() })
		pane->SetUIFont(font);
	m_Namespaces->SetUIFont(font);
	for (auto host : { &m_Navigator, &m_Editors, &m_SideHost }) {
		host->Strip().SetFont(custom);
		host->Relayout();
	}
	m_QuickOpen.SetUIFont(font);
	m_Decode.SetUIFont(font);
}

void CApiBrowser::SetApi(Win32Api& api) {
	if (&api == m_Api)
		return;
	CWaitCursor wait;
	api.Entries();
	m_Api = &api;
	for (auto pane : { m_Search.get(), m_UsedBy.get(), m_Favorites.get(), m_History.get(), m_HostList.get() })
		pane->SetApi(api);
	m_Namespaces->SetApi(api);
	UpdateUsedBy();
	UpdateLists();
	m_QuickOpen.SetApi(api);
	if (m_Side)
		m_Side->SetApi(api);
	m_SwitchingMachine = true;
	for (auto& tab : m_Tabs)
		tab.View->SetApi(api);
	m_SwitchingMachine = false;
	UpdateDocumentation();
	UpdateStatus();
	TitleChanged();
}

//
// The tabs
//

int CApiBrowser::ActiveTab() const {
	return m_Editors.m_hWnd ? m_Editors.Strip().GetSelected() : -1;
}

int CApiBrowser::TargetTab() const {
	return m_MenuTab >= 0 && m_MenuTab < (int)m_Tabs.size() ? m_MenuTab : ActiveTab();
}

CDeclarationView* CApiBrowser::FocusedView() const {
	auto focus = ::GetFocus();
	if (m_Side && focus && (focus == m_Side->m_hWnd || m_Side->IsChild(focus)))
		return m_Side.get();
	return ActiveView();
}

int CApiBrowser::SideTarget() const {
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

void CApiBrowser::OpenToSide(size_t entry) {
	if (!m_Side) {
		m_Side = CreateView(m_SideHost);
		auto side = m_Side.get();
		side->OnFollow = [this, side](std::string const& name, bool newTab) {
			if (!newTab)
				side->Navigate(name);
			else if (auto found = m_Api->Find(name))
				OpenEntry(found - m_Api->Entries().data(), true);
		};
		side->OnShownChanged = [this] { SideChanged(); };
		m_SideHost.Strip().Add(L"", -1);
		m_SideHost.Strip().Select(0);
		m_SideHost.SetPage(*side);
		m_EditorSplit.SetSinglePaneMode(SPLIT_PANE_NONE);
		m_EditorSplit.SetSplitterPosPct(50);
	}
	m_Side->Show((int)entry);
	m_Side->SetFocus();
}

void CApiBrowser::SideChanged() {
	if (!m_Side)
		return;
	auto entry = m_Side->Entry();
	if (entry < 0)
		return;
	m_SideHost.Strip().SetText(0, Wide(m_Side->Name()));
	m_SideHost.Strip().SetImage(0, Images::Of(m_Api->Entries()[entry].Kind));
}

void CApiBrowser::CloseSide() {
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

CDeclarationView* CApiBrowser::ActiveView() const {
	auto index = ActiveTab();
	return index >= 0 && index < (int)m_Tabs.size() ? m_Tabs[index].View.get() : nullptr;
}

int CApiBrowser::TabOf(CDeclarationView const* view) const {
	for (int i = 0; i < (int)m_Tabs.size(); i++)
		if (m_Tabs[i].View.get() == view)
			return i;
	return -1;
}

int CApiBrowser::TabShowing(int entry) const {
	for (int i = 0; i < (int)m_Tabs.size(); i++)
		if (m_Tabs[i].View->Entry() == entry)
			return i;
	return -1;
}

void CApiBrowser::OpenEntry(size_t entry, bool pin, std::string const& name) {
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

int CApiBrowser::AddTab(int entry, bool preview, int position) {
	auto view = CreateView(m_Editors);
	auto p = view.get();
	p->OnFollow = [this, p](std::string const& name, bool newTab) { Follow(p, name, newTab); };
	p->OnShownChanged = [this, p] { ViewChanged(p); };
	p->Show(entry);
	// (after the active one, as an editor opens one)
	if (position < 0 || position > (int)m_Tabs.size())
		position = std::min(ActiveTab() + 1, (int)m_Tabs.size());
	m_Tabs.insert(m_Tabs.begin() + position, Tab{ std::move(view), preview });
	auto image = entry >= 0 ? Images::Of(m_Api->Entries()[entry].Kind) : -1;
	m_Editors.Strip().Insert(position, Wide(p->Name()), image, preview);
	return position;
}

void CApiBrowser::ActivateTab(int index) {
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
	TitleChanged();
}

void CApiBrowser::CloseTab(int index) {
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

void CApiBrowser::Pin(int index) {
	if (index < 0 || index >= (int)m_Tabs.size() || !m_Tabs[index].Preview)
		return;
	m_Tabs[index].Preview = false;
	m_Editors.Strip().SetItalic(index, false);
	AddToHistory(m_Tabs[index].View->Entry());
}

void CApiBrowser::AddToHistory(int entry) {
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

void CApiBrowser::UpdateLists() {
	if (!m_Favorites)
		return;
	auto entries = [&](std::vector<std::string> const& names) {
		std::vector<size_t> result;
		for (auto const& name : names)
			if (auto entry = m_Api->Find(name))
				result.push_back(entry - m_Api->Entries().data());
		return result;
	};
	m_FavoriteEntries = entries(m_FavoriteNames);
	m_HistoryEntries = entries(m_HistoryNames);
	m_HostEntries = entries(m_HostNames);
	m_Favorites->SetScope(&m_FavoriteEntries, L"the favorites (Ctrl+Shift+A adds and removes)");
	m_History->SetScope(&m_HistoryEntries, L"what was opened");
	m_HostList->SetScope(&m_HostEntries, m_HostDescription);
}

void CApiBrowser::Follow(CDeclarationView* view, std::string const& name, bool newTab) {
	if (newTab) {
		if (auto entry = m_Api->Find(name))
			OpenEntry(entry - m_Api->Entries().data(), true);
		return;
	}
	// (a preview that was used is kept)
	view->Navigate(name);
	Pin(TabOf(view));
}

void CApiBrowser::ViewChanged(CDeclarationView* view) {
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
		TitleChanged();
	}
}

void CApiBrowser::ShowTabMenu(int index, CPoint pt) {
	if (index < 0)
		return;
	m_MenuTab = index;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_TAB_KEEPOPEN, L"&Keep Open");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_TAB_CLOSE, L"&Close");
	menu.AppendMenu(MF_STRING, ID_TAB_CLOSEOTHERS, L"Close &Others");
	menu.AppendMenu(MF_STRING, ID_TAB_CLOSEALL, L"Close &All");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPYNAME, L"Copy &Name");
	CMenu copyAs;
	copyAs.CreatePopupMenu();
	copyAs.AppendMenu(MF_STRING, ID_EDIT_COPYCSHARP, L"&C#");
	copyAs.AppendMenu(MF_STRING, ID_EDIT_COPYRUST, L"&Rust");
	copyAs.AppendMenu(MF_STRING, ID_EDIT_COPYPYTHON, L"&Python (ctypes)");
	menu.AppendMenu(MF_POPUP, copyAs.Detach(), L"Copy &As");
	menu.AppendMenu(MF_STRING, ID_GO_REVEAL, L"&Reveal in Namespaces");
	menu.AppendMenu(MF_STRING, ID_GO_USEDBY, L"Find &Usages");
	menu.AppendMenu(MF_STRING, ID_EDIT_FAVORITE, L"Add to Fa&vorites");
	menu.AppendMenu(MF_STRING, ID_GO_SIDE, L"Open to the &Side");
	menu.AppendMenu(MF_STRING, ID_DOC_BROWSER, L"Open Documentation in &Browser");
	for (auto const& command : CommandStates()) {
		menu.EnableMenuItem(command.Id, MF_BYCOMMAND | (command.Enabled ? MF_ENABLED : MF_GRAYED));
		if (command.Check >= 0)
			menu.CheckMenuItem(command.Id, MF_BYCOMMAND | (command.Check ? MF_CHECKED : MF_UNCHECKED));
	}
	if (OnExtendMenu)
		OnExtendMenu(menu);
	auto command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (command)
		SendCommand(command);
	m_MenuTab = -1;
}

//
// What is shown around the tabs
//

int CApiBrowser::PageCount() const {
	return m_HostTitle.empty() ? HostPage : HostPage + 1;
}

void CApiBrowser::ShowNavigatorPage(int page) {
	if (page < 0 || page >= PageCount())
		page = SearchPage;
	HWND const pages[] = { *m_Search, *m_Namespaces, *m_UsedBy, *m_Favorites, *m_History, *m_HostList };
	m_Navigator.Strip().Select(page);
	m_Navigator.SetPage(pages[page]);
	m_State.NavigatorPage = page;
	UpdateStatus();
}

int CApiBrowser::UsagesTarget() const {
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

int CApiBrowser::DecodeTarget() const {
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

void CApiBrowser::FindUsages(size_t entry) {
	m_UsedBySubject = m_Api->Entries()[entry].Name;
	UpdateUsedBy();
	ShowNavigatorPage(UsedByPage);
	m_UsedBy->SetFocus();
}

void CApiBrowser::UpdateUsedBy() {
	auto entry = m_UsedBySubject.empty() ? nullptr : m_Api->Find(m_UsedBySubject);
	auto& strip = m_Navigator.Strip();
	if (!entry) {
		m_UsedBy->SetScope(&m_NoEntries, L"what uses a type (Shift+F12 finds it)");
		strip.SetText(UsedByPage, L"Used By");
		return;
	}
	CWaitCursor wait;
	auto const& users = m_Api->UsedBy(entry - m_Api->Entries().data());
	auto name = Wide(m_UsedBySubject);
	m_UsedBy->SetScope(&users, std::format(L"the {} that use {}", users.size(), name));
	strip.SetText(UsedByPage, L"Used By: " + name);
}

void CApiBrowser::UpdateDocumentation() {
	if (!m_State.ShowDocumentation)
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

void CApiBrowser::UpdateStatus() {
	if (!m_Search)
		return;
	// (of the list shown: the search, or what uses a type)
	CSearchPane* const lists[] = { m_Search.get(), m_Search.get(), m_UsedBy.get(), m_Favorites.get(), m_History.get(), m_HostList.get() };
	auto list = lists[std::clamp(m_Navigator.Strip().GetSelected(), 0, PageCount() - 1)];
	Status(std::format(L"{} of {} shown", list->GetShownCount(), list->GetTotalCount()));
}

void CApiBrowser::Status(std::wstring const& text) {
	if (OnStatus)
		OnStatus(text);
}

void CApiBrowser::TitleChanged() {
	if (OnTitleChanged)
		OnTitleChanged();
}

std::vector<CApiBrowser::CommandState> CApiBrowser::CommandStates() const {
	std::vector<CommandState> states;
	if (!m_hWnd || !m_Search)
		return states;
	auto view = FocusedView();
	bool referred = view && !view->ReferredAtCaret().empty();
	auto target = TargetTab();
	bool shows = target >= 0 && m_Tabs[target].View->Entry() >= 0;
	auto count = (int)m_Tabs.size();
	bool favorite = shows && std::ranges::find(m_FavoriteNames, m_Tabs[target].View->Name()) != m_FavoriteNames.end();
	bool copyable = TargetEntry() >= 0;
	states = {
		{ ID_GO_BACK, view && view->CanGoBack() },
		{ ID_GO_FORWARD, view && view->CanGoForward() },
		{ ID_GO_DEFINITION, referred },
		{ ID_GO_DEFINITION_NEWTAB, referred },
		{ ID_GO_REVEAL, shows },
		{ ID_GO_USEDBY, UsagesTarget() >= 0 },
		{ ID_GO_SIDE, SideTarget() >= 0 },
		{ ID_GO_QUICKOPEN, true },
		{ ID_EDIT_DECODE, DecodeTarget() >= 0 },
		{ ID_DOC_BROWSER, shows },
		{ ID_EDIT_COPYNAME, shows },
		{ ID_EDIT_COPYCSHARP, copyable },
		{ ID_EDIT_COPYRUST, copyable },
		{ ID_EDIT_COPYPYTHON, copyable },
		{ ID_EDIT_FAVORITE, shows, favorite ? 1 : 0 },
		{ ID_VIEW_DOCUMENTATION, true, m_State.ShowDocumentation ? 1 : 0 },
		{ ID_SHOW_SINGLELINE, true, m_State.SingleLine ? 1 : 0 },
		{ ID_VIEW_CLEARHISTORY, !m_HistoryNames.empty() },
		{ ID_VIEW_HOSTLIST, !m_HostTitle.empty() },
		{ ID_TAB_CLOSE, target >= 0 },
		{ ID_TAB_CLOSEOTHERS, count > 1 && target >= 0 },
		{ ID_TAB_CLOSEALL, count > 0 },
		{ ID_TAB_NEXT, count > 1 },
		{ ID_TAB_PREV, count > 1 },
		{ ID_TAB_KEEPOPEN, target >= 0 && m_Tabs[target].Preview },
	};
	return states;
}

std::vector<CApiBrowser::RadioState> CApiBrowser::RadioStates() const {
	if (!m_hWnd || !m_Search)
		return {};
	// (one of each: a radio item, of the range of its group's IDs)
	static_assert(ID_VIEW_NAMESPACES == ID_VIEW_SEARCH + NamespacesPage && ID_VIEW_USEDBY == ID_VIEW_SEARCH + UsedByPage &&
		ID_VIEW_FAVORITES == ID_VIEW_SEARCH + FavoritesPage && ID_VIEW_HISTORY == ID_VIEW_SEARCH + HistoryPage &&
		ID_VIEW_HOSTLIST == ID_VIEW_SEARCH + HostPage);
	static_assert(ID_SHOW_LAYOUT == ID_SHOW_DECLARATION + 1 && ID_SHOW_COMPARE == ID_SHOW_DECLARATION + 2);
	auto pages = PageCount();
	auto page = std::clamp(m_Navigator.Strip().GetSelected(), 0, pages - 1);
	return {
		{ (UINT)(ID_VIEW_SEARCH + page), ID_VIEW_SEARCH, (UINT)(ID_VIEW_SEARCH + pages - 1) },
		{ (UINT)(ID_SHOW_DECLARATION + std::clamp(m_State.ShowMode, 0, 2)), ID_SHOW_DECLARATION, ID_SHOW_COMPARE },
	};
}

//
// Commands
//

void CApiBrowser::GoBack() {
	if (auto view = FocusedView())
		view->GoBack();
}

void CApiBrowser::GoForward() {
	if (auto view = FocusedView())
		view->GoForward();
}

// the accelerators take Ctrl+C and Ctrl+A from the edits: they are given back
LRESULT CApiBrowser::OnCopy(WORD, WORD, HWND, BOOL&) {
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

LRESULT CApiBrowser::OnSelectAll(WORD, WORD, HWND, BOOL&) {
	auto focus = ::GetFocus();
	if (auto view = FocusedView(); view && (focus == view->m_hWnd || view->IsChild(focus)))
		view->SelectAll();
	else if (IsEdit(focus))
		::SendMessage(focus, EM_SETSEL, 0, -1);
	return 0;
}

LRESULT CApiBrowser::OnCopyName(WORD, WORD, HWND, BOOL&) {
	auto target = TargetTab();
	if (target < 0)
		return 0;
	auto name = m_Tabs[target].View->Name();
	if (!name.empty())
		ClipboardHelper::CopyText(m_hWnd, Wide(name).c_str());
	return 0;
}

LRESULT CApiBrowser::OnDecode(WORD, WORD, HWND, BOOL&) {
	auto entry = DecodeTarget();
	m_MenuTab = -1;
	if (entry < 0)
		::MessageBeep(MB_ICONWARNING);
	else
		m_Decode.Show(*m_Api, entry);
	return 0;
}

LRESULT CApiBrowser::OnFind(WORD, WORD, HWND, BOOL&) {
	ShowNavigatorPage(SearchPage);
	m_Search->FocusFilter();
	return 0;
}

LRESULT CApiBrowser::OnViewNavigator(WORD, WORD id, HWND, BOOL&) {
	auto page = id - ID_VIEW_SEARCH;
	if (page >= PageCount())
		return 0;
	ShowNavigatorPage(page);
	::SetFocus(m_Navigator.GetPage());
	return 0;
}

LRESULT CApiBrowser::OnQuickOpen(WORD, WORD, HWND, BOOL&) {
	m_QuickOpen.Show();
	return 0;
}

// its declaration in C#, Rust or Python, to the clipboard
LRESULT CApiBrowser::OnCopyAs(WORD, WORD id, HWND, BOOL&) {
	auto entry = TargetEntry();
	m_MenuTab = -1;
	if (entry < 0)
		return 0;
	static_assert(ID_EDIT_COPYRUST == ID_EDIT_COPYCSHARP + 1 && ID_EDIT_COPYPYTHON == ID_EDIT_COPYCSHARP + 2);
	auto language = (Win32Api::Language)(id - ID_EDIT_COPYCSHARP);
	auto const& e = m_Api->Entries()[entry];
	auto text = m_Api->DeclarationIn(e, language);
	if (text.empty()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	ClipboardHelper::CopyText(m_hWnd, Wide(text).c_str());
	PCWSTR const names[] = { L"C#", L"Rust", L"Python" };
	Status(std::format(L"{} copied as {}", Wide(e.Name), names[(int)language]));
	return 0;
}

LRESULT CApiBrowser::OnClearHistory(WORD, WORD, HWND, BOOL&) {
	m_HistoryNames.clear();
	UpdateLists();
	return 0;
}

// the tab's (whose menu is shown, else the active one): a favorite, or not any more
LRESULT CApiBrowser::OnFavorite(WORD, WORD, HWND, BOOL&) {
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

LRESULT CApiBrowser::OnFindUsages(WORD, WORD, HWND, BOOL&) {
	auto entry = UsagesTarget();
	m_MenuTab = -1;
	if (entry < 0)
		::MessageBeep(MB_ICONWARNING);
	else
		FindUsages(entry);
	return 0;
}

LRESULT CApiBrowser::OnViewDocumentation(WORD, WORD, HWND, BOOL&) {
	ShowDocumentation(!m_State.ShowDocumentation);
	return 0;
}

// what the tabs show of structs and unions (all of them: as a way of looking at them)
LRESULT CApiBrowser::OnShowMode(WORD, WORD id, HWND, BOOL&) {
	auto mode = id - ID_SHOW_DECLARATION;
	m_State.ShowMode = mode;
	for (auto& tab : m_Tabs)
		tab.View->SetMode((CDeclarationView::Mode)mode);
	if (m_Side)
		m_Side->SetMode((CDeclarationView::Mode)mode);
	return 0;
}

// how the tabs declare functions (all of them)
LRESULT CApiBrowser::OnSingleLine(WORD, WORD, HWND, BOOL&) {
	m_State.SingleLine = !m_State.SingleLine;
	for (auto& tab : m_Tabs)
		tab.View->SetSingleLine(m_State.SingleLine);
	if (m_Side)
		m_Side->SetSingleLine(m_State.SingleLine);
	return 0;
}


LRESULT CApiBrowser::OnBack(WORD, WORD, HWND, BOOL&) {
	GoBack();
	return 0;
}

LRESULT CApiBrowser::OnForward(WORD, WORD, HWND, BOOL&) {
	GoForward();
	return 0;
}

LRESULT CApiBrowser::OnGoToDefinition(WORD, WORD id, HWND, BOOL&) {
	auto view = FocusedView();
	if (!view || !view->FollowCaret(id == ID_GO_DEFINITION_NEWTAB))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

LRESULT CApiBrowser::OnOpenToSide(WORD, WORD, HWND, BOOL&) {
	auto entry = SideTarget();
	m_MenuTab = -1;
	if (entry < 0)
		::MessageBeep(MB_ICONWARNING);
	else
		OpenToSide(entry);
	return 0;
}

// in the tree
LRESULT CApiBrowser::OnReveal(WORD, WORD, HWND, BOOL&) {
	auto target = TargetTab();
	auto entry = target >= 0 ? m_Tabs[target].View->Entry() : -1;
	if (entry < 0)
		return 0;
	ShowNavigatorPage(NamespacesPage);
	if (m_Namespaces->SelectEntry(entry))
		m_Namespaces->SetFocus();
	return 0;
}

// its page, or the search of the documentation for it if it has none
LRESULT CApiBrowser::OnOpenInBrowser(WORD, WORD, HWND, BOOL&) {
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

LRESULT CApiBrowser::OnCloseTab(WORD, WORD, HWND, BOOL&) {
	CloseTab(TargetTab());
	return 0;
}

LRESULT CApiBrowser::OnCloseOtherTabs(WORD, WORD, HWND, BOOL&) {
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

LRESULT CApiBrowser::OnCloseAllTabs(WORD, WORD, HWND, BOOL&) {
	m_MenuTab = -1;
	while (!m_Tabs.empty())
		CloseTab((int)m_Tabs.size() - 1);
	return 0;
}

LRESULT CApiBrowser::OnNextTab(WORD, WORD id, HWND, BOOL&) {
	auto count = (int)m_Tabs.size();
	if (count < 2)
		return 0;
	auto step = id == ID_TAB_NEXT ? 1 : count - 1;
	ActivateTab((std::max(ActiveTab(), 0) + step) % count);
	return 0;
}

LRESULT CApiBrowser::OnKeepOpen(WORD, WORD, HWND, BOOL&) {
	Pin(TargetTab());
	return 0;
}
