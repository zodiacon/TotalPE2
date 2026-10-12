#pragma once

#include <WTLDockUI.h>
#include <functional>
#include <map>

//
// The views of the main window in a docking area (WTLDock): the views are documents (in tab groups that can be split and
// floated), lists to come back to are tool panes around them, and the tree of the file is a tool pane of its own.
//
// It has what the frame used of a tab view: pages by index (in the order they were added), the active page and its data
// (the message map that commands go to), a Window menu with an item per page, and the notifications TBVN_PAGEACTIVATED and
// TBVN_TABCLOSEBTN to the frame (idFrom is the page). The active page is the view that had the focus last: it stays
// active while the focus is in the tree, as the selected tab did.
//
// A tool page can have an id of its own (beginning with "tool:", one per kind of list): closed, its pane stays (without
// a window) and the list comes back where it was. The layout file keeps the tree and these places, not the views (they are
// of a file).
//
class CDockViews {
public:
	enum class Placement {
		Document,	// in the document area
		Tool,		// a tool pane at the bottom (with the other tool pages, as tabs)
	};

	HWND Create(HWND hParent);
	// the docking area: the parent of the views
	operator HWND() const {
		return m_Dock.m_hWnd;
	}
	WTLDock::CDockHost& Dock() {
		return m_Dock;
	}

	// the tree of the file, at the left (it cannot be closed: the views are opened from it)
	void SetExplorer(HWND hTree, PCWSTR title);

	// the page is activated (and the window is destroyed when it is removed). 'toolId': a tool page that keeps its place.
	bool AddPage(HWND hWnd, PCWSTR title, int image = -1, void* data = nullptr, Placement where = Placement::Document,
		std::wstring const& toolId = {});
	void RemovePage(int page);
	void RemoveAllPages();
	int GetPageCount() const {
		return (int)m_Pages.size();
	}
	int GetActivePage() const {
		return m_Active;
	}
	void SetActivePage(int page);
	// a page into the place of another: in its group, at its tab (the other is to be removed then)
	void MoveToPlaceOf(int page, int other);
	HWND GetPageHWND(int page) const;
	PCWSTR GetPageTitle(int page) const;
	void* GetPageData(int page) const;

	// the images of the pages (the icons of their tabs)
	void SetImageList(HIMAGELIST images);
	// the menu that lists the pages (ID_WINDOW_TABFIRST + page)
	void SetWindowMenu(HMENU menu);

	BOOL PreTranslateMessage(MSG* msg);

	// The layout: the default (to reset to) is the arrangement when it is loaded. 'toolTitle' names the tool panes the file
	// knows (by their ids). False if there is no file, or it cannot be used (the default stays).
	bool LoadLayout(std::wstring const& path, std::function<std::wstring(std::wstring const& id)> toolTitle);
	bool SaveLayout(std::wstring const& path) const;
	// the default layout, with the pages that are open: the documents in the document area, the tool pages at the bottom
	void ResetLayout();
	void ShowExplorer();

	// the docking commands (new tab group...) for the active page
	bool CanExecute(WTLDock::DockCommand command) const;
	bool Execute(WTLDock::DockCommand command);
	void ShowWindowsDialog(HWND hOwner);

private:
	struct Page {
		WTLDock::DockPane* Pane;
		void* Data;
		int Image;
	};

	int FindPage(WTLDock::DockPane const* pane) const;
	WTLDock::DockPane* ActivePagePane() const;
	// a page that is new where it goes: a document in the document area, a tool page with the docked tool pages
	void Place(WTLDock::DockPane* pane);
	WTLDock::DockPane* MakeToolPane(WTLDock::DockLayout& layout, std::wstring const& id);
	// tool panes without a window (their lists are closed) that the layout shows are hidden: they keep their place
	void HideEmptyToolPanes();
	static bool IsToolId(std::wstring const& id) {
		return id.starts_with(L"tool:");
	}
	HICON IconOf(int image);
	void Notify(UINT code, int page);
	void OnActivePaneChanged();
	void OnPaneClosed(WTLDock::DockPane* pane);
	// after a page is gone: the page that has the focus, or the last one, is the active page
	void ActivateAfterRemoval();
	void UpdateWindowMenu();

	WTLDock::CDockHost m_Dock;
	WTLDock::DockPane* m_Explorer{};
	std::vector<Page> m_Pages;
	int m_Active{ -1 };
	int m_NextId{ 0 };
	bool m_Removing{ false };
	HIMAGELIST m_Images{};
	std::map<int, HICON> m_Icons;
	HMENU m_WindowMenu{};
	std::function<std::wstring(std::wstring const& id)> m_ToolTitle;
};
