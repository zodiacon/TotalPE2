#include "pch.h"
#include "DockViews.h"
#include <WTLDockDarkMode.h>

using namespace WTLDock;

HWND CDockViews::Create(HWND hParent) {
	m_Dock.Create(hParent, ATL::CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	UseDarkModeTheme(m_Dock);	// follows WTLHelper's dark mode, as the rest of the application

	m_Dock.OnActivePaneChanged = [this] { OnActivePaneChanged(); };
	m_Dock.OnPaneClosed = [this](DockPane* pane) { OnPaneClosed(pane); };
	return m_Dock;
}

void CDockViews::SetExplorer(HWND hTree, PCWSTR title) {
	PaneDesc desc;
	desc.Id = L"explorer";
	desc.Title = title;
	desc.hWnd = hTree;
	desc.Kind = PaneKind::Tool;
	desc.Caps = PaneCaps::CanFloat | PaneCaps::CanAutoHide;
	desc.DefaultSide = DockSide::Left;
	desc.PreferredSize = { 280, 400 };
	m_Explorer = m_Dock.Layout().AddPane(desc);
	m_Dock.Layout().Show(m_Explorer);
}

bool CDockViews::AddPage(HWND hWnd, PCWSTR title, int image, void* data, Placement where) {
	PaneDesc desc;
	desc.Id = std::format(L"view{}", ++m_NextId);
	desc.Title = title;
	desc.hWnd = hWnd;
	desc.Icon = IconOf(image);
	if (where == Placement::Tool) {
		desc.Kind = PaneKind::Tool;
		desc.DefaultSide = DockSide::Bottom;
		desc.PreferredSize = { 600, 260 };
	}
	else {
		desc.Kind = PaneKind::Document;
	}
	auto& layout = m_Dock.Layout();
	auto pane = layout.AddPane(desc);
	if (!pane)
		return false;

	// a tool page joins the one that is docked already, as a tab
	bool placed = false;
	if (where == Placement::Tool) {
		for (auto const& p : m_Pages) {
			if (p.Pane->Kind() == PaneKind::Tool && p.Pane->State() == PaneState::Docked && p.Pane->Group()) {
				placed = layout.DockTo(pane, p.Pane->Group(), DockPosition::Tab);
				break;
			}
		}
	}
	if (!placed)
		layout.Show(pane);

	m_Pages.push_back({ pane, data, image });
	SetActivePage((int)m_Pages.size() - 1);
	UpdateWindowMenu();
	return true;
}

void CDockViews::RemovePage(int page) {
	if (page < 0 || page >= (int)m_Pages.size())
		return;

	auto pane = m_Pages[page].Pane;
	auto hWnd = pane->hWnd;
	m_Pages.erase(m_Pages.begin() + page);
	if (m_Active == page)
		m_Active = -1;
	else if (m_Active > page)
		m_Active--;

	m_Removing = true;
	m_Dock.Layout().Hide(pane);
	m_Dock.Layout().RemovePane(pane);
	m_Removing = false;
	::DestroyWindow(hWnd);

	ActivateAfterRemoval();
	UpdateWindowMenu();
}

void CDockViews::RemoveAllPages() {
	auto pages = std::move(m_Pages);
	m_Pages.clear();
	m_Active = -1;

	m_Removing = true;
	for (auto const& page : pages) {
		auto hWnd = page.Pane->hWnd;
		m_Dock.Layout().Hide(page.Pane);
		m_Dock.Layout().RemovePane(page.Pane);
		::DestroyWindow(hWnd);
	}
	m_Removing = false;
	UpdateWindowMenu();
}

void CDockViews::SetActivePage(int page) {
	if (page < 0 || page >= (int)m_Pages.size())
		return;

	auto pane = m_Pages[page].Pane;
	bool changed = m_Active != page;
	m_Active = page;
	m_Dock.ShowPane(pane);
	m_Dock.ActivatePane(pane);
	if (changed) {
		Notify(TBVN_PAGEACTIVATED, page);
		UpdateWindowMenu();
	}
}

HWND CDockViews::GetPageHWND(int page) const {
	return page >= 0 && page < (int)m_Pages.size() ? m_Pages[page].Pane->hWnd : nullptr;
}

PCWSTR CDockViews::GetPageTitle(int page) const {
	return page >= 0 && page < (int)m_Pages.size() ? m_Pages[page].Pane->Title.c_str() : L"";
}

void* CDockViews::GetPageData(int page) const {
	return page >= 0 && page < (int)m_Pages.size() ? m_Pages[page].Data : nullptr;
}

// The icons are made from the image list; a new list (another size) makes them again
void CDockViews::SetImageList(HIMAGELIST images) {
	auto old = std::move(m_Icons);
	m_Icons.clear();
	m_Images = images;
	for (auto& page : m_Pages) {
		page.Pane->Icon = IconOf(page.Image);
		m_Dock.RefreshPane(page.Pane);
	}
	for (auto& [image, icon] : old)
		::DestroyIcon(icon);
}

HICON CDockViews::IconOf(int image) {
	if (image < 0 || !m_Images)
		return nullptr;
	if (auto it = m_Icons.find(image); it != m_Icons.end())
		return it->second;
	auto icon = ImageList_GetIcon(m_Images, image, ILD_NORMAL);
	m_Icons[image] = icon;
	return icon;
}

void CDockViews::SetWindowMenu(HMENU menu) {
	m_WindowMenu = menu;
	UpdateWindowMenu();
}

BOOL CDockViews::PreTranslateMessage(MSG* msg) {
	return m_Dock.m_hWnd && m_Dock.PreTranslateMessage(msg);
}

int CDockViews::FindPage(DockPane const* pane) const {
	for (int i = 0; i < (int)m_Pages.size(); i++)
		if (m_Pages[i].Pane == pane)
			return i;
	return -1;
}

void CDockViews::Notify(UINT code, int page) {
	NMHDR hdr{ m_Dock.m_hWnd, (UINT_PTR)page, code };
	m_Dock.GetParent().SendMessage(WM_NOTIFY, page, (LPARAM)&hdr);
}

// A view has the focus: it is the active page (the tree having the focus leaves the active page as it was)
void CDockViews::OnActivePaneChanged() {
	if (m_Removing)
		return;
	int page = FindPage(m_Dock.ActivePane());
	if (page < 0 || page == m_Active)
		return;
	m_Active = page;
	Notify(TBVN_PAGEACTIVATED, page);
	UpdateWindowMenu();
}

// Closed from its tab or caption: the frame lets go of the view (as for the close button of a tab), then it is destroyed
void CDockViews::OnPaneClosed(DockPane* pane) {
	int page = FindPage(pane);
	if (page < 0)
		return;
	NMHDR hdr{ m_Dock.m_hWnd, (UINT_PTR)page, TBVN_TABCLOSEBTN };
	if (m_Dock.GetParent().SendMessage(WM_NOTIFY, page, (LPARAM)&hdr) == 0)
		RemovePage(page);
	else
		m_Dock.Layout().Show(pane);		// the frame keeps it
}

void CDockViews::ActivateAfterRemoval() {
	if (m_Active >= 0)
		return;
	int page = FindPage(m_Dock.ActivePane());
	if (page < 0)
		page = (int)m_Pages.size() - 1;
	m_Active = page;
	Notify(TBVN_PAGEACTIVATED, page);
}

// The items of the pages at the end of the Window menu (after a separator), the active one checked
void CDockViews::UpdateWindowMenu() {
	if (!m_WindowMenu)
		return;
	CMenuHandle menu(m_WindowMenu);

	int first = 0;
	for (; first < menu.GetMenuItemCount(); first++) {
		auto id = menu.GetMenuItemID(first);
		if (id >= ID_WINDOW_TABFIRST && id <= ID_WINDOW_TABLAST)
			break;
	}
	while (menu.DeleteMenu(first, MF_BYPOSITION))
		;

	int count = std::min((int)m_Pages.size(), (int)(ID_WINDOW_TABLAST - ID_WINDOW_TABFIRST + 1));
	count = std::min(count, 20);
	bool separator = false;
	if (first > 0) {
		CMenuItemInfo mii;
		mii.fMask = MIIM_FTYPE;
		menu.GetMenuItemInfo(first - 1, TRUE, &mii);
		separator = (mii.fType & MFT_SEPARATOR) != 0;
	}
	if (count == 0) {
		if (separator)
			menu.DeleteMenu(first - 1, MF_BYPOSITION);
		return;
	}
	if (!separator && first > 0)
		menu.AppendMenu(MF_SEPARATOR);
	for (int i = 0; i < count; i++) {
		auto text = i < 9 ? std::format(L"&{} {}", i + 1, m_Pages[i].Pane->Title) : std::format(L"{} {}", i + 1, m_Pages[i].Pane->Title);
		menu.AppendMenu(MF_STRING | (i == m_Active ? MF_CHECKED : 0), ID_WINDOW_TABFIRST + i, text.c_str());
	}
}
