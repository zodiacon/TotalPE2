#pragma once

constexpr uint32_t ItemShift = 8;

enum class TreeItemType : uint32_t {
	None = 0,
	Image,
	Directories,
	Directory,
	Sections,
	Section,
	Headers,
	FileHeader,
	DOSHeader,
	OptionalHeader,
	RichHeader,
	Resources,
	ResourceTypeName,
	ResourceName,
	ResourceLnaguage,
	ContextMenu,
	CLR,
	Symbols,
	Language,
	Resource,

	ItemMask = 255,
};
DEFINE_ENUM_FLAG_OPERATORS(TreeItemType);

struct IMainFrame abstract {
	virtual HWND GetHwnd() const = 0;
	virtual BOOL TrackPopupMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd = nullptr) = 0;
	virtual CUpdateUIBase& GetUI() = 0;
	virtual HIMAGELIST GetImageList() const = 0;
	virtual int GetDataDirectoryIconIndex(int index) const = 0;
//	virtual bool AddToolBar(HWND tb) = 0;
	//virtual void SetStatusText(int index, PCWSTR text) = 0;
	//virtual CString GetSelectedTreeItemPath() const = 0;
	//virtual CString GetTreeItemText(int parents) const = 0;
	//virtual HIMAGELIST GetTreeImageList() const = 0;
	//virtual bool GotoTreeItemResource(PCWSTR path) = 0;
	//virtual DiaSession const& GetSymbols() const = 0;
};

struct IView abstract {
	virtual CString GetTitle() const = 0;
	virtual HWND GetHwnd() const = 0;
};
