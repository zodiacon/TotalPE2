#pragma once

#include "ViewBase.h"
#include "MenuTemplate.h"

// Shows a menu resource as a tree, plus a clickable menu bar that opens the real popup menus.
class CMenuView : public CViewBase<CMenuView> {
public:
	CMenuView(IMainFrame* frame, PCWSTR title);
	~CMenuView();

	CString GetTitle() const override;
	bool SetData(std::span<const std::byte> data);

	static constexpr int FirstBarId = 0x6000;

	BEGIN_MSG_MAP(CMenuView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		COMMAND_RANGE_HANDLER(FirstBarId, FirstBarId + 0xFFF, OnBarClick)
		CHAIN_MSG_MAP(CViewBase<CMenuView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CMenuView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnBarClick(WORD, WORD id, HWND, BOOL&);

private:
	void BuildTree(std::vector<MenuNode> const& nodes, HTREEITEM parent);
	void BuildBar();

	CToolBarCtrl m_Bar;
	CTreeViewCtrl m_Tree;
	MenuTemplate m_Menu;
	std::vector<HMENU> m_Popups;	// one per top-level item (null if it has no submenu)
	CString m_Title;
};
