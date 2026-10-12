// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DockViews.h"
#include <GhidraDecompiler.h>
#include <TabViewHelper.h>
#include <CustomSplitterWindow.h>
#include <TreeViewHelper.h>
#include <PEFile.h>
#include "LibArchive.h"
#include "CoffObject.h"
#include "CodeViewInfo.h"
#include "GuardTables.h"
#include "ElfFile.h"
#include "ElfDebugInfo.h"
#include "Interfaces.h"
#include "GoToDlg.h"
#include "SearchDlg.h"
#include "RecentFilesManager.h"
#include <Theme.h>
#include <map>

class CHexView;

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public CTreeViewHelper<CMainFrame>,
	public IMainFrame,
	public ISearchHost,
	public CMessageFilter, 
	public CIdleHandler {
public:
	DECLARE_FRAME_WND_CLASS(L"TotalPEMainWndClass", IDR_MAINFRAME)

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;
	void OnFinalMessage(HWND) override;

	bool OnTreeDoubleClick(HWND tree, HTREEITEM hItem);
	bool OnTreeRightClick(HWND tree, HTREEITEM hItem, POINT const& pt);

	BEGIN_MSG_MAP(CMainFrame)
		COMMAND_TABVIEW_HANDLER(m_Tabs, 1)
		MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles)
		COMMAND_ID_HANDLER(ID_FILE_RUNASADMINISTRATOR, OnRunAsAdmin)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		NOTIFY_CODE_HANDLER(TBVN_PAGEACTIVATED, OnPageActivated)
		NOTIFY_CODE_HANDLER(TBVN_TABCLOSEBTN, OnPageCloseButton)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnEditFind)
		COMMAND_ID_HANDLER(ID_EDIT_SEARCHALL, OnEditSearchAll)
		COMMAND_ID_HANDLER(ID_FILE_COMPARE, OnFileCompare)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE, OnWindowClose)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE_ALL, OnWindowCloseAll)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSEOTHERS, OnWindowCloseOthers)
		COMMAND_ID_HANDLER(ID_WINDOW_NEWHGROUP, OnWindowDockCommand)
		COMMAND_ID_HANDLER(ID_WINDOW_NEWVGROUP, OnWindowDockCommand)
		COMMAND_ID_HANDLER(ID_WINDOW_NEXTGROUP, OnWindowDockCommand)
		COMMAND_ID_HANDLER(ID_WINDOW_PREVGROUP, OnWindowDockCommand)
		COMMAND_ID_HANDLER(ID_WINDOW_WINDOWS, OnWindowWindows)
		COMMAND_ID_HANDLER(ID_WINDOW_RESETLAYOUT, OnWindowResetLayout)
		COMMAND_ID_HANDLER(ID_VIEW_EXPLORER, OnViewExplorer)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnFileOpen)
		COMMAND_ID_HANDLER(ID_FILE_OPENINANEWWINDOW, OnFileOpenNewWindow)
		COMMAND_RANGE_HANDLER(ID_WINDOW_TABFIRST, ID_WINDOW_TABLAST, OnWindowActivate)
		COMMAND_ID_HANDLER(ID_FILE_CLOSE, OnFileClose)
		COMMAND_ID_HANDLER(ID_VIEW_EXPORTS, OnViewExports)
		COMMAND_ID_HANDLER(ID_VIEW_IMPORTS, OnViewImports)
		COMMAND_ID_HANDLER(ID_VIEW_RESOURCES, OnViewResources)
		COMMAND_ID_HANDLER(ID_VIEW_DEBUG, OnViewDebug)
		COMMAND_ID_HANDLER(ID_VIEW_MANIFEST, OnViewManifest)
		COMMAND_ID_HANDLER(ID_VIEW_VERSION, OnViewVersion)
		COMMAND_ID_HANDLER(ID_VIEW_OVERLAY, OnViewOverlay)
		COMMAND_ID_HANDLER(ID_PE_STRINGS, OnViewStrings)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnFileSave)
		COMMAND_ID_HANDLER(ID_FILE_EXPORTLIST, OnFileExportList)
		COMMAND_ID_HANDLER(ID_PE_SECURITY, OnViewSecurity)
		COMMAND_ID_HANDLER(ID_PE_ENTIREFILEINHEX, OnViewFileInHex)
		COMMAND_ID_HANDLER(ID_VIEW_RESOURCEHEX, OnViewResourceHex)
		COMMAND_ID_HANDLER(ID_VIEW_SECTIONS, OnViewSections)
		COMMAND_ID_HANDLER(ID_VIEW_DIRECTORIES, OnViewDataDirs)
		COMMAND_ID_HANDLER(ID_PE_DISASSEMBLEENTRYPOINT, OnDisassembleEntryPoint)
		COMMAND_ID_HANDLER(ID_PE_DISASSEMBLEFUNCTION, OnDisassembleFunction)
		COMMAND_ID_HANDLER(ID_PE_DECOMPILEFUNCTION, OnDecompileFunction)
		COMMAND_ID_HANDLER(ID_FILE_NEW, OnNewWindow)
		COMMAND_ID_HANDLER(ID_OPTIONS_ALWAYSONTOP, OnAlwaysOnTop)
		COMMAND_ID_HANDLER(ID_HELP_ABOUTWINDOWS, OnWindowsProperties)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		MESSAGE_HANDLER(WM_MENUSELECT, OnMenuSelect)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnToggleDarkMode)
		COMMAND_ID_HANDLER(ID_OPTIONS_SYMBOLS, OnSymbolSettings)
		COMMAND_ID_HANDLER(ID_NAV_GOTO, OnNavGoTo)
		COMMAND_ID_HANDLER(ID_NAV_BACK, OnNavBack)
		COMMAND_ID_HANDLER(ID_NAV_FORWARD, OnNavForward)
		MESSAGE_HANDLER(WM_SYMBOLS_LOADED, OnSymbolsLoaded)
		MESSAGE_HANDLER(WM_VT_STATUS, OnVirusTotalStatus)
		COMMAND_ID_HANDLER(ID_PE_VIRUSTOTAL, OnVirusTotal)
		COMMAND_ID_HANDLER(ID_PE_VTAPIKEY, OnVirusTotalKey)
		MESSAGE_HANDLER(WM_UPDATE_DARKMODE, OnUpdateDarkMode)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_RANGE_HANDLER(ATL_IDS_MRU_FILE, ATL_IDS_MRU_FILE + 29, OnRecentFile)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		CHAIN_MSG_MAP(CTreeViewHelper<CMainFrame>)
		CHAIN_MSG_MAP(CAutoUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
		REFLECT_NOTIFICATIONS_EX()
	END_MSG_MAP()

private:
	// IMainFrame
	HWND GetHwnd() const override;
	BOOL ShowContextMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd) override;
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
	std::wstring ResolveRva(DWORD rva) const override;
	std::wstring ResolveVa(ULONGLONG va) const override;
	std::vector<Anomaly> const& GetAnomalies() const override;
	bool GoToFileOffset(int64_t offset, bool disassemble = false) override;
	bool GoToVa(uint64_t va) override;
	XrefMap const& GetXrefs() override;
	bool ShowXrefs(uint64_t va) override;
	bool ShowFlowGraph(uint64_t va) override;
	bool DisassembleFunction(uint64_t va) override;
	bool ShowType(DiaSymbol const& type) override;
	bool ShowTypeDefinition(DiaSymbol const& type, TypeFormat format) override;
	bool ShowArchiveMember(int member) override;
	bool ShowObjectSection(int section, int64_t offset, int member = -1) override;
	bool ShowElfAddress(uint64_t address, bool code) override;
	bool ShowElfSection(int section, int64_t offset, bool code) override;
	bool ShowElfFileOffset(int64_t offset) override;
	void RecordNavigation() override;
	bool AddToolBar(HWND tb) override;
	bool DeleteTreeItem(HTREEITEM hItem) override;
	bool CreateAssemblyView(std::span<const std::byte> code, uint64_t address, uint32_t rva, PCWSTR title, TreeItemType parent) override;

	std::pair<IView*, CMessageMap*> CreateView(TreeItemType type);
	std::pair<IView*, CMessageMap*> CreateResourceView(TreeItemType type);
	void SetResourceHexData(CHexView& view, FlatResource const& res) const;
	int ActiveResource();
	bool ShowResourceHex(int index);
	void SetAlwaysOnTop(bool onTop);
	void InitDarkTheme() const;
	bool ShowView(HTREEITEM hItem);
	bool ShowView(TreeItemType type, HTREEITEM hItem = nullptr, UINT icon = 0);
	void UpdateRecentFilesMenu();
	void UpdateUI();
	void InitMenu(HMENU hMenu);
	void BuildTree(int iconSize = 16);
	bool OpenPE(PCWSTR path);
	bool OpenArchive(PCWSTR path);
	void BuildArchiveTree(int iconSize);
	bool OpenObject(PCWSTR path);
	void BuildObjectTree(int iconSize);
	// the items of an object under its own: sections, symbols... the CodeView information
	void InsertObjectItems(HTREEITEM hObject, CoffObject const& obj, CodeViewInfo const& cv, int member);

	// An object: the object file that is open (member -1), or a member of the library that was opened as an object
	struct ObjectRef {
		CoffObject const* Object{ nullptr };
		CodeViewInfo const* CodeView{ nullptr };
		PCWSTR Owner{ nullptr };	// the name of the member, for the titles of its views
	};
	ObjectRef FindObject(int member) const;
	// The tree item type of an item of an object (see TreeItemType): the member (if any) in bits 8-31, the section in bits 32-62
	static TreeItemType ObjectItem(TreeItemType type, int member, int section = -1);
	static int ObjectItemMember(TreeItemType type);
	static int ObjectItemSection(TreeItemType type);
	bool OpenElf(PCWSTR path);
	void BuildElfTree(int iconSize);
	bool ShowElfCode(int section, uint64_t offset);
	void ResetFileState();
	std::wstring CurrentPath() const;
	CString DoFileOpen() const;
	bool BuildTreeImageList(int iconSize = 16);
	void ParseCommandLine();
	void CloseTab(int page);
	void RegisterContextMenu(TreeItemType type, UINT menuId, int subMenu);

	static int GetTreeIcon(UINT id);
	static TreeItemType TreeItemWithIndex(TreeItemType type, int64_t index);
	static int ResourceTypeToIcon(WORD resType);
	static int DirectoryIndexToIcon(int index);
	static TreeItemType GetIndex(TreeItemType value);
	// the views of lists to come back to (symbols, references) are tool panes, not documents
	static bool IsToolView(TreeItemType type);
	// the id of the tool pane of a kind of list, that keeps its place (empty for those that have none: references)
	static std::wstring ToolViewId(TreeItemType type);
	static std::wstring ToolViewTitle(std::wstring const& id);
public:
	// The name of a function without what its signature has around it (the access, the return type, the calling convention,
	// the parameters): "public: virtual long __cdecl ns::C::F(int) const" is "ns::C::F". A decorated name is undecorated.
	static std::wstring FunctionName(std::wstring const& name);
private:

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
	LRESULT OnWindowCloseOthers(WORD, WORD, HWND, BOOL&);
	LRESULT OnWindowDockCommand(WORD, WORD id, HWND, BOOL&);
	LRESULT OnWindowWindows(WORD, WORD, HWND, BOOL&);
	LRESULT OnWindowResetLayout(WORD, WORD, HWND, BOOL&);
	LRESULT OnViewExplorer(WORD, WORD, HWND, BOOL&);
	// the commands of the docking area that depend on the active view
	void UpdateDockUI();
	// the file the arrangement of the docking area is kept in
	static std::wstring LayoutPath();
	LRESULT OnNewWindow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowActivate(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAsAdmin(WORD, WORD, HWND, BOOL&);
	LRESULT OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFileOpen(WORD, WORD, HWND, BOOL&);
	LRESULT OnFileOpenNewWindow(WORD, WORD, HWND, BOOL&);
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
	LRESULT OnViewOverlay(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStrings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFileSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFileExportList(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	void UpdateSaveUI();
	bool SaveTreeItemData(TreeItemType type);
	bool SaveText(HWND hText, CString const& name);
	LRESULT OnDisassembleEntryPoint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDisassembleFunction(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDecompileFunction(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// The function at an address as C (Ghidra's decompiler), in a view of its own next to where its disassembly goes
	bool DecompileFunction(uint64_t va) override;
	// the decompiler of the PE file (made on first use); null, with the reason, if there is none for its machine
	GhidraDecompiler* Decompiler(std::wstring& error);
	// the PE image as it is in memory: the headers, and the sections at their addresses (zeros past their data)
	bool ReadImage(uint64_t va, uint8_t* buffer, size_t size) const;
	// the decompiler's types and prototypes from the symbols: a type by its symbol's id, the prototype of a function
	bool DescribeType(uint32_t id, GhidraDecompiler::Type& type) const;
	bool FunctionPrototype(uint64_t va, uint32_t& type, std::vector<std::string>& parameterNames) const;
	// the name of the function at an address for the decompiler (of an incremental linking stub, the function it jumps to)
	std::string DecompilerFunctionName(uint64_t va) const;
	// the target of an incremental linking stub ("jmp rel32") at an address; 0 if it is not one
	uint64_t StubTarget(uint64_t va) const;
	std::unique_ptr<GhidraDecompiler> m_Decompiler;
	LRESULT OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateDarkMode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnToggleDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolSettings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnNavGoTo(WORD, WORD, HWND, BOOL&);
	LRESULT OnEditSearchAll(WORD, WORD, HWND, BOOL&);
	LRESULT OnFileCompare(WORD, WORD, HWND, BOOL&);
	int m_Comparisons{ 0 };

	// Search All (see SearchDlg.h)
	SearchIndex const& GetSearchIndex() override;
	bool GoToSearchItem(SearchItem const& item) override;
	void BuildSearchIndex();
	static int SearchItemImage(SearchItem const& item);
	// the strings of the data; 'keep' (if any) says which can be shown
	void AddStringsToSearchIndex(std::span<const std::byte> data, std::function<bool(uint32_t offset)> const& keep = {});
	// the file, or what is known about it, changed: the index is built again when it is needed
	void SearchTargetChanged();
	// the tree item of a type (null if there is none)
	HTREEITEM FindTreeItem(TreeItemType type) const;
	LRESULT OnNavBack(WORD, WORD, HWND, BOOL&);
	LRESULT OnNavForward(WORD, WORD, HWND, BOOL&);
	LRESULT OnSymbolsLoaded(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVirusTotalStatus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVirusTotal(WORD, WORD, HWND, BOOL&);
	LRESULT OnVirusTotalKey(WORD, WORD, HWND, BOOL&);
	void CancelVirusTotal();
	std::wstring GetVirusTotalKey(bool ask);
	LRESULT OnPageActivated(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnViewFileInHex(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewResourceHex(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPageCloseButton(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnWindowsProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	struct ContextMenuInfo {
		UINT MenuId;
		int SubMenu;
	};

	CDockViews m_Tabs;	// the views, in a docking area (with the tree)
	CTreeViewCtrl m_Tree;
	CMultiPaneStatusBarCtrl m_StatusBar;
	PEFile m_PE;
	LibArchive m_Archive;		// a library is open instead of a PE file
	CoffObject m_Object;		// an object file is open instead of a PE file
	CodeViewInfo m_ObjectCodeView;
	// the members of the library that were opened as objects, by their index
	struct MemberObject {
		CoffObject Object;
		CodeViewInfo CodeView;
		std::wstring Name;
		HTREEITEM hItem{};
	};
	std::map<int, MemberObject> m_MemberObjects;
	ElfFile m_Elf;				// an ELF file is open instead of a PE file
	struct ElfName {
		uint64_t Address, Size;
		std::wstring Name;
	};
	std::vector<ElfName> m_ElfNames;
	ElfDebugInfo m_ElfDebug;			// of the ELF file: in it, or in its debug file	// the functions and data of the ELF file, by address: the disassembly shows their names
	int m_ElfCodeViews{ 0 };
	std::unordered_map<uint64_t, TreeItemType> m_ElfCodeItems;	// the code views of the ELF file, by address
	std::vector<GuardTable> m_GuardTables;	// of the PE file
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
	std::unordered_map<TreeItemType, ContextMenuInfo> m_ContextMenus;
	mutable std::unordered_map<std::wstring, DiaSession> m_SymbolsForModules;
	std::vector<std::pair<DWORD, std::wstring>> m_NamedRvas;	// exports, import slots; sorted by RVA
	std::vector<Anomaly> m_Anomalies;
	XrefMap m_Xrefs;
	OverlayInfo m_Overlay;
	HTREEITEM m_hOverlay{ nullptr };
	HWND m_SaveUIPage{ (HWND)-1 };	// the page Save and Export List were last enabled for

	// Go To, Back and Forward
	struct NavigationEntry {
		HWND Page;
		int64_t Position;
	};
	std::vector<NavigationEntry> m_History;
	int m_HistoryIndex{ -1 };
	bool m_Navigating{ false };
	GoToOptions m_GoTo;
	CSearchDlg m_SearchDlg{ this };
	SearchIndex m_SearchIndex;
	bool m_SearchIndexValid{ false };
	bool NavigateHistory(int delta);
	void UpdateNavigationUI();
	void ResetNavigation();
	bool ResolveGoTo(GoToOptions const& options, int64_t& offset, std::wstring& error) const;
	std::wstring m_DisassembleFunction;	// the last name or address entered
	bool ResolveFunction(std::wstring const& text, uint64_t& address, std::wstring& error) const;
	void InsertSymbolsNode(HTREEITEM hRoot);
	HTREEITEM TypeGroupItem(DiaSymbol const& type) const;

	// symbols are loaded on a worker thread; results of an older request are ignored
	uint32_t m_SymbolGeneration{ 0 };
	vt::Status m_Vt;							// the VirusTotal scan of the current file
	std::shared_ptr<std::atomic_bool> m_VtCancel;
	uint32_t m_VtGeneration{ 0 };
	void StartSymbolLoad(std::wstring path);
	void RefreshViews();
	void BuildNamedRvas();
	inline static std::unordered_map<UINT, int> s_ImageIndices;
	inline static int s_Frames{ 1 };
	inline static Theme s_DarkTheme;
};
