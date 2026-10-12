#pragma once

#include <WTLDockUI.h>
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

	// the page is activated (and the window is destroyed when it is removed)
	bool AddPage(HWND hWnd, PCWSTR title, int image = -1, void* data = nullptr, Placement where = Placement::Document);
	void RemovePage(int page);
	void RemoveAllPages();
	int GetPageCount() const {
		return (int)m_Pages.size();
	}
	int GetActivePage() const {
		return m_Active;
	}
	void SetActivePage(int page);
	HWND GetPageHWND(int page) const;
	PCWSTR GetPageTitle(int page) const;
	void* GetPageData(int page) const;

	// the images of the pages (the icons of their tabs)
	void SetImageList(HIMAGELIST images);
	// the menu that lists the pages (ID_WINDOW_TABFIRST + page)
	void SetWindowMenu(HMENU menu);

	BOOL PreTranslateMessage(MSG* msg);

private:
	struct Page {
		WTLDock::DockPane* Pane;
		void* Data;
		int Image;
	};

	int FindPage(WTLDock::DockPane const* pane) const;
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
};
