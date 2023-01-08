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
#include "RecentFilesManager.h"
#include <Theme.h>

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public CTreeViewHelper<CMainFrame>,
	public COwnerDrawnMenu<CMainFrame>,
	public IMainFrame,
	public CMessageFilter, 
	public CIdleHandler {
public:
	DECLARE_FRAME_WND_CLASS(L"TotalPEMainWndClass", IDR_MAINFRAME)

	static const UINT WM_UPDATE_DARKMODE = WM_APP + 56;

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;
	void OnFinalMessage(HWND) override;

	bool OnTreeDoubleClick(HWND tree, HTREEITEM hItem);

	BEGIN_MSG_MAP(CMainFrame)
		COMMAND_TABVIEW_HANDLER(m_Tabs, 1)
		MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles)
		COMMAND_ID_HANDLER(ID_FILE_RUNASADMINISTRATOR, OnRunAsAdmin)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnEditFind)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE, OnWindowClose)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE_ALL, OnWindowCloseAll)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnFileOpen)
		COMMAND_RANGE_HANDLER(ID_WINDOW_TABFIRST, ID_WINDOW_TABLAST, OnWindowActivate)
		COMMAND_ID_HANDLER(ID_FILE_CLOSE, OnFileClose)
		COMMAND_ID_HANDLER(ID_VIEW_EXPORTS, OnViewExports)
		COMMAND_ID_HANDLER(ID_VIEW_IMPORTS, OnViewImports)
		COMMAND_ID_HANDLER(ID_VIEW_RESOURCES, OnViewResources)
		COMMAND_ID_HANDLER(ID_VIEW_DEBUG, OnViewDebug)
		COMMAND_ID_HANDLER(ID_VIEW_MANIFEST, OnViewManifest)
		COMMAND_ID_HANDLER(ID_VIEW_VERSION, OnViewVersion)
		COMMAND_ID_HANDLER(ID_PE_SECURITY, OnViewSecurity)
		COMMAND_ID_HANDLER(ID_VIEW_SECTIONS, OnViewSections)
		COMMAND_ID_HANDLER(ID_VIEW_DIRECTORIES, OnViewDataDirs)
		COMMAND_ID_HANDLER(ID_PE_DISASSEMBLEENTRYPOINT, OnDisassembleEntryPoint)
		COMMAND_ID_HANDLER(ID_FILE_NEW, OnNewWindow)
		COMMAND_ID_HANDLER(ID_OPTIONS_ALWAYSONTOP, OnAlwaysOnTop)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		MESSAGE_HANDLER(WM_MENUSELECT, OnMenuSelect)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnToggleDarkMode)
		MESSAGE_HANDLER(WM_UPDATE_DARKMODE, OnUpdateDarkMode)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_RANGE_HANDLER(ATL_IDS_MRU_FILE, ATL_IDS_MRU_FILE + 29, OnRecentFile)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		CHAIN_MSG_MAP(CTreeViewHelper<CMainFrame>)
		CHAIN_MSG_MAP(COwnerDrawnMenu<CMainFrame>)
		CHAIN_MSG_MAP(CAutoUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
		REFLECT_NOTIFICATIONS_EX()
	END_MSG_MAP()

private:
	// IMainFrame
	HWND GetHwnd() const override;
	BOOL TrackPopupMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd) override;
	CUpdateUIBase& GetUI() override;
	HIMAGELIST GetImageList() const override;
	int GetDataDirectoryIconIndex(int index) const override;
	int GetIconIndex(UINT icon) const override;
	void SetStatusText(int index, PCWSTR text) override;
	CFindReplaceDialog* GetFindDialog() override;
	DiaSession const& GetSymbols() const override;
	std::vector<FlatResource> const& GetFlatResources() const override;
	int GetResourceIconIndex(WORD resType) const override;
	DiaSymbol GetSymbolForName(PCWSTR mod, PCWSTR name) const override;
	bool AddToolBar(HWND tb) override;
	bool DeleteTreeItem(HTREEITEM hItem) override;
	bool CreateAssemblyView(std::span<const std::byte> code, uint64_t address, uint32_t rva, PCWSTR title, TreeItemType parent) override;

	std::pair<IView*, CMessageMap*> CreateView(TreeItemType type);
	std::pair<IView*, CMessageMap*> CreateResourceView(TreeItemType type);
	void SetAlwaysOnTop(bool onTop);
	void InitDarkTheme() const;
	bool ShowView(HTREEITEM hItem);
	bool ShowView(TreeItemType type, HTREEITEM hItem = nullptr, UINT icon = 0);
	void UpdateRecentFilesMenu();
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
	static TreeItemType GetIndex(TreeItemType value);

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnShowWindow(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMenuSelect(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) const;
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) const;
	LRESULT OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnNewWindow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowActivate(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAsAdmin(WORD, WORD, HWND, BOOL&);
	LRESULT OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFileOpen(WORD, WORD, HWND, BOOL&);
	LRESULT OnFileClose(WORD, WORD, HWND, BOOL&);
	LRESULT OnEditFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRecentFile(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewExports(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewImports(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewResources(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewDebug(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewSecurity(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewSections(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewDataDirs(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewManifest(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewVersion(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDisassembleEntryPoint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateDarkMode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnToggleDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CCustomTabView m_Tabs;
	CCustomSplitterWindow m_Splitter;
	CTreeViewCtrl m_Tree;
	CMultiPaneStatusBarCtrl m_StatusBar;
	PEFile m_PE;
	std::vector<FlatResource> m_FlatResources;
	CImageList m_TreeImages;
	std::unordered_map<TreeItemType, IView*> m_Views;
	std::unordered_map<HWND, TreeItemType> m_Views2;
	HTREEITEM m_hRoot;
	CFindReplaceDialog* m_pFindDlg{ nullptr };
	CString m_SearchText;
	DiaSession m_Symbols;
	RecentFilesManager m_RecentFiles;
	HTREEITEM m_hResVersion, m_hResManifest;
	inline static std::unordered_map<UINT, int> s_ImageIndices;
	inline static int s_Frames{ 0 };
	inline static Theme s_DarkTheme;
};
