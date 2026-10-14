// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DockViews.h"
#include <GhidraDecompiler.h>
#include <Annotations.h>
#include "Win32Api.h"
class CWin32ApiFrame;
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
#include <atomic>
#include <shared_mutex>
#include <unordered_set>
#include <thread>

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
		COMMAND_ID_HANDLER(ID_PE_FUNCTIONS, OnViewFunctions)
		COMMAND_ID_HANDLER(ID_PE_CLRTYPES, OnViewClrTypes)
		COMMAND_ID_HANDLER(ID_PE_SECURITYSUMMARY, OnViewSecuritySummary)
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
		COMMAND_ID_HANDLER(ID_PE_SIDEBYSIDE, OnSideBySide)
		COMMAND_ID_HANDLER(ID_FILE_NEW, OnNewWindow)
		COMMAND_ID_HANDLER(ID_OPTIONS_ALWAYSONTOP, OnAlwaysOnTop)
		COMMAND_ID_HANDLER(ID_HELP_ABOUTWINDOWS, OnWindowsProperties)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		MESSAGE_HANDLER(WM_MENUSELECT, OnMenuSelect)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnToggleDarkMode)
		COMMAND_ID_HANDLER(ID_OPTIONS_SYMBOLS, OnSymbolSettings)
		COMMAND_ID_HANDLER(ID_NAV_GOTO, OnNavGoTo)
		COMMAND_ID_HANDLER(ID_NAV_GOANYWHERE, OnNavGoAnywhere)
		COMMAND_ID_HANDLER(ID_NAV_BACK, OnNavBack)
		COMMAND_ID_HANDLER(ID_NAV_FORWARD, OnNavForward)
		COMMAND_ID_HANDLER(ID_NAV_BOOKMARKS, OnViewBookmarks)
		COMMAND_ID_HANDLER(ID_PE_WIN32API, OnViewWin32Api)
		COMMAND_ID_HANDLER(ID_NAV_COMMENT, OnNotInView)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_TOGGLE, OnNotInView)
		MESSAGE_HANDLER(WM_SYMBOLS_LOADED, OnSymbolsLoaded)
		MESSAGE_HANDLER(WM_DECOMPILER_ASK, OnDecompilerAsk)
		MESSAGE_HANDLER(WM_DECOMPILED, OnDecompiled)
		MESSAGE_HANDLER(WM_XREFS_BUILT, OnXrefsBuilt)
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
	bool XrefsReady() const override {
		return m_XrefsReady;
	}
	bool ShowXrefs(uint64_t va) override;
	bool ShowFlowGraph(uint64_t va) override;
	bool ShowCallGraph(uint64_t va) override;
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
	LRESULT OnViewFunctions(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewClrTypes(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewSecuritySummary(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
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
	bool ShowSideBySide(uint64_t va) override;
	bool RenameFunction(uint64_t va, std::wstring const& name) override;
	bool ChangeVariable(uint64_t function, GhidraDecompiler::Variable const& variable, std::optional<std::string> const& name,
		std::optional<std::string> const& type, GhidraDecompiler::Variables const& variables) override;
	// What the user added to the file (names, types, bookmarks, comments), kept by its content; none for a file of another kind
	Annotations m_Annotations;
	void LoadAnnotations(std::wstring const& path);
	void SaveAnnotations();
	Annotations const& GetAnnotations() const override {
		return m_Annotations;
	}
	std::wstring CommentAt(uint64_t va) const override;
	bool IsBookmarked(uint64_t va) const override;
	bool EditComment(uint64_t va) override;
	bool ToggleBookmark(uint64_t va) override;
	bool SetBookmark(uint64_t va, std::wstring const& name) override;
	bool RemoveBookmark(uint64_t va) override;
	bool ShowBookmarks() override;
	bool ShowWin32Api(std::string const& name) override;
	bool IsWin32Api(std::string const& name) override;
	bool ShowImportReferences(std::string const& name) override;
	std::vector<std::string> ConstantNames(std::string const& name, uint64_t& value, std::string& type) override;
	void PreferConstantName(std::string const& name) override;
	// the Win32 API of the file's machine; null if there is no metadata (or no PE file)
	Win32Api* Win32ForFile() const;
	// the names of the functions the file imports
	std::unordered_set<std::string> FileImportNames() const;
	// the views of the file go when another is opened (or it is closed)
	void CloseFileViews();
	// the Windows API window (if it is open) shows the machine and imports of the file now open
	void RefreshWin32View();
	CString Win32WindowTitle() const;
	LRESULT OnViewWin32Api(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the views show the comments and bookmarks again (and, if the comments changed, the code is decompiled again)
	void NotifyAnnotationsChanged(bool comments);
	LRESULT OnViewBookmarks(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// a command of the code views (a comment, a bookmark) that no view took: there is no code here
	LRESULT OnNotInView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the RVA of an address of the image (false if it is not in it)
	bool RvaOf(uint64_t va, uint32_t& rva) const;
	// The views of decompiled functions: the function, and how its C is made again (when the user's names or types change)
	struct DecompiledView {
		uint64_t Function;
		std::function<void()> Refresh;
	};
	std::unordered_map<HWND, DecompiledView> m_DecompiledViews;
	// the views of a function's C (all of them for 0) are decompiled again, where they were scrolled to
	void RedecompileViews(uint64_t function = 0);
	LRESULT OnSideBySide(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	TreeItemType FunctionPlace(uint32_t rva, std::wstring& name) const;
	HTREEITEM PlaceItem(TreeItemType parent);
	std::unordered_map<std::wstring, uint64_t> FunctionLinks(GhidraDecompiler::FunctionRefs const& functions) const;
	bool FunctionCode(uint64_t address, std::span<const std::byte>& code);
	bool ReplaceView(HWND hView, std::function<bool()> const& open) override;
	// The decompiler of an open PE file. It is used on the decompiler's thread, and made there when it is first used, from
	// what is prepared here (on the frame's thread): the functions, imports and global variables it is told of. What it asks
	// for while it decompiles (names, and from the symbols, which are the frame thread's, types, prototypes and locals) is
	// asked of the frame's thread; the image is read on the decompiler's thread, as long as the file is open.
	struct DecompilerSession {
		CMainFrame* Frame;
		HWND Window;
		std::string Target;
		std::wstring Languages;		// the folder of the language definitions
		std::vector<std::pair<uint64_t, std::string>> Functions;	// the exports
		uint64_t ExternalArea{ 0 };
		struct Import {
			uint64_t Slot;		// in the import address table
			std::string Name;
			std::string Module;	// the DLL, and for an API set the DLL that implements it
			std::string Dll;	// the DLL as the file names it
		};
		std::vector<Import> Imports;
		std::unordered_map<uint64_t, size_t> ImportAt;	// the imports by the addresses the decompiler gave them
		Win32Api* Win32{ nullptr };	// the prototypes of the functions of Windows (if the metadata is there)
		std::vector<std::pair<uint64_t, uint64_t>> ReadOnly;	// the sections that are not written (address, size)
		struct Global {
			uint64_t Address;
			std::string Name;
			uint32_t Type;
		};
		std::vector<Global> Globals;
		bool Symbols{ false };		// the types, prototypes and locals of the symbols are asked for
		std::unique_ptr<GhidraDecompiler> Decompiler;
		std::string Error;			// why it could not be made
		std::atomic<bool> Closed{ false };	// the file is closed: nothing more is read or asked
		bool Stale{ false };		// the symbols changed: their types are not asked for (by their ids in the symbols before)
		std::shared_mutex Image;	// held (shared) while the image is read, (exclusive) when the file is closed

		// on the decompiler's thread
		GhidraDecompiler* Open(std::string& error);
		// runs 'ask' on the frame's thread (unless the file was closed); false if it did not run
		bool Ask(std::function<void()> const& ask);
	};
	// the decompiler of the PE file; null, with the reason, if there is none for its machine
	std::shared_ptr<DecompilerSession> Decompiler(std::wstring& error);
	// the file is closed (or what the decompiler was told of changes): the decompiler's work in progress reads nothing
	// more of it, and the decompiler is destroyed on its thread
	void CloseDecompiler(bool closed = true);
	// A function decompiled on the decompiler's thread: 'done' is called on the frame's thread with the result, if the
	// view it is for is still there then
	struct Decompiled {
		uint64_t Id;
		bool Ok{ false };
		std::string Code, Error;
		GhidraDecompiler::FunctionRefs Functions;
		GhidraDecompiler::LineAddresses Lines;
		GhidraDecompiler::Variables Variables;
	};
	void DecompileAsync(uint64_t address, std::wstring const& name, bool lines, HWND hView, std::function<void(Decompiled&)> done);
	static std::string DecompilingText(uint64_t address, std::wstring const& name);
	static std::string NotDecompiledText(uint64_t address, std::string const& error);
	struct PendingDecompile {
		HWND View;
		std::wstring Name;
		std::function<void(Decompiled&)> Done;
		std::shared_ptr<std::atomic<bool>> Cancel;	// set when the view goes: the decompiler's thread skips it if not started
	};
	// the work for a view that is closed (it is not done, unless it is being done already)
	void CancelDecompiles(HWND hView);
	// the status bar's panes of the work in the background: the decompiling, the cross-references
	static constexpr int DecompilerPane = 3, XrefsPane = 4;
	void UpdateDecompilerStatus();
	std::unordered_map<uint64_t, PendingDecompile> m_PendingDecompiles;
	uint64_t m_LastDecompile{ 0 };
	LRESULT OnDecompilerAsk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDecompiled(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	// the PE image as it is in memory: the headers, and the sections at their addresses (zeros past their data)
	bool ReadImage(uint64_t va, uint8_t* buffer, size_t size) const;
	// the decompiler's types and prototypes from the symbols: a type by its symbol's id, the prototype of a function
	bool DescribeType(uint32_t id, GhidraDecompiler::Type& type) const;
	bool FunctionPrototype(uint64_t va, uint32_t& type, std::vector<std::string>& parameterNames) const;
	// the import a function only jumps to (a thunk: jmp [slot]), by its slot; null if it is not one
	DecompilerSession::Import const* ThunkImport(uint64_t va) const;
	std::string ExportName(uint64_t va) const;
	std::string FileModuleName() const;
	bool IsWindowsFile() const;
	// the local variables of a function on the stack, from the symbols (of private symbols)
	bool FunctionLocals(uint64_t va, std::vector<uint64_t> const& instructions, std::vector<GhidraDecompiler::Local>& locals) const;
	// the frame an x64 function's prologue makes, from its unwind information: its size (what is pushed and allocated), and
	// the frame register (its CodeView number; 0 if there is none) and its offset from the stack pointer at the entry
	bool X64Frame(uint32_t rva, uint32_t& size, uint32_t& frameRegister, int64_t& frameOffset) const;
	// the name of the function at an address for the decompiler (of an incremental linking stub, the function it jumps to)
	std::string DecompilerFunctionName(uint64_t va) const;
	// the __try regions and C++ try blocks of an x64 function (from its unwind information), as blocks of its C code
	std::vector<GhidraDecompiler::Block> ExceptionBlocks(uint64_t va) const;
	// for the decompiler: the user's changes to the variables of a function, a type of the symbols by name, and the user's
	// names of functions and global variables
	std::vector<GhidraDecompiler::VariableChange> VariableChanges(uint64_t va) const;
	uint32_t TypeIdByName(std::string const& name) const;
	// the type of the symbols that a type of the Win32 metadata is (a struct, union or enum, of the same size); 0 if none
	uint32_t SymbolsTypeOf(GhidraDecompiler::Type const& type) const;
	void UserNames(std::vector<std::pair<uint64_t, std::string>>& functions, std::vector<std::pair<uint64_t, GhidraDecompiler::VariableChange>>& globals) const;
	// the target of an incremental linking stub ("jmp rel32") at an address; 0 if it is not one
	uint64_t StubTarget(uint64_t va) const;
	std::shared_ptr<DecompilerSession> m_Decompiler;
	LRESULT OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateDarkMode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnToggleDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolSettings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnNavGoTo(WORD, WORD, HWND, BOOL&);
	LRESULT OnNavGoAnywhere(WORD, WORD, HWND, BOOL&);
	std::vector<SearchItem> AddressItems(std::wstring const& text) override;
	void AddViewsToSearchIndex(HTREEITEM hParent, std::wstring const& parent, int depth);
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
	CWin32ApiFrame* m_Win32Frame{ nullptr };	// the Windows API, in its window (Tools > Windows API)
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
	// The cross-references of the PE file, found on a thread of their own when it is opened (a large file takes seconds).
	// What needs them before they are there is done when they are.
	XrefMap m_Xrefs;
	bool m_XrefsReady{ false };		// found (or there are none to find)
	std::thread m_XrefThread;
	std::shared_ptr<std::atomic<bool>> m_XrefCancel;
	uint32_t m_XrefGeneration{ 0 };
	std::vector<std::function<void()>> m_AfterXrefs;
	void StartXrefs();
	// before the file goes: the thread ends (and what waits for the cross-references is not done)
	void CancelXrefs();
	// 'action' now if the cross-references are there, otherwise when they are; true if it was done now
	bool WithXrefs(std::function<void()> action);
	LRESULT OnXrefsBuilt(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
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
