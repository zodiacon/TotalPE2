// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <CustomTabView.h>
#include <CustomSplitterWindow.h>
#include <TreeViewHelper.h>
#include <PEFile.h>
#include <OwnerDrawnMenu.h>
#include "Interfaces.h"

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public CTreeViewHelper<CMainFrame>,
	public COwnerDrawnMenu<CMainFrame>,
	public IMainFrame,
	public CMessageFilter, 
	public CIdleHandler {
public:
	DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME)

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;

	bool OnTreeDoubleClick(HWND tree, HTREEITEM hItem);

	BEGIN_MSG_MAP(CMainFrame)
		COMMAND_TABVIEW_HANDLER(m_Tabs, 1)
		MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles)
		COMMAND_ID_HANDLER(ID_FILE_RUNASADMINISTRATOR, OnRunAsAdmin)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE, OnWindowClose)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE_ALL, OnWindowCloseAll)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnFileOpen)
		COMMAND_RANGE_HANDLER(ID_WINDOW_TABFIRST, ID_WINDOW_TABLAST, OnWindowActivate)
		COMMAND_ID_HANDLER(ID_FILE_CLOSE, OnFileClose)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CTreeViewHelper<CMainFrame>)
		CHAIN_MSG_MAP(COwnerDrawnMenu<CMainFrame>)
		CHAIN_MSG_MAP(CAutoUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

private:
	// IMainFrame
	HWND GetHwnd() const override;
	BOOL TrackPopupMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd) override;
	CUpdateUIBase& GetUI() override;

	std::pair<IView*, CMessageMap*> CreateView(TreeItemType type);
	bool ShowView(HTREEITEM hItem);

	void UpdateUI();
	void InitMenu(HMENU hMenu);
	void BuildTree(int iconSize = 16);
	bool OpenPE(PCWSTR path);
	CString DoFileOpen() const;
	void BuildTreeImageList(int iconSize = 16);
	static int GetTreeIcon(UINT id);
	static TreeItemType TreeItemWithIndex(TreeItemType type, int index);
	static int ResourceTypeToIcon(WORD resType);
	static int DirectoryIndexToIcon(int index);

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFileNew(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowActivate(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAsAdmin(WORD, WORD, HWND, BOOL&);
	LRESULT OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFileOpen(WORD, WORD, HWND, BOOL&);
	LRESULT OnFileClose(WORD, WORD, HWND, BOOL&);

	CCustomTabView m_Tabs;
	CCustomSplitterWindow m_Splitter;
	CTreeViewCtrl m_Tree;
	CMultiPaneStatusBarCtrl m_StatusBar;
	PEFile m_PE;
	std::vector<libpe::PEResFlat> m_FlatResources;
	CImageList m_TreeImages;
	std::unordered_map<TreeItemType, HWND> m_Views;
	std::unordered_map<HWND, TreeItemType> m_Views2;
	HTREEITEM m_hRoot;
	bool m_HasManifest : 1, m_HasVersion : 1;
	inline static std::unordered_map<UINT, int> s_TreeImageIndices;
	inline static int s_Frames{ 1 };
};
