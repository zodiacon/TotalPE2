#pragma once

#include <DiaHelper.h>
#include <PEFile.h>
#include "PEAnomalies.h"
#include "CodeAnalysis.h"
#include "Overlay.h"
#include "VirusTotal.h"

static const UINT WM_UPDATE_DARKMODE = WM_APP + 56;
// posted by the symbol loading thread: wParam is the generation of the request, lParam a DiaSession* (or null)
static const UINT WM_SYMBOLS_LOADED = WM_APP + 57;
// posted by the VirusTotal thread: wParam is the generation of the scan, lParam a vt::Status* that the receiver deletes
static const UINT WM_VT_STATUS = WM_APP + 58;

constexpr uint32_t ItemShift = 8;

enum class TreeItemType : int64_t {
	None = 0,
	Image,
	Directories,
	Directory,
	DirectoryExports = Directory,
	DirectoryImports,
	DirectoryResources,
	DirectoryExceptions,
	DirectorySecurity,
	DirectoryReloc,
	DirectoryDebug,
	DirectoryArch,
	DirectoryGlobalPtr,
	DirectoryTLS,
	DirectoryLoadConfig,
	DirectoryBoundImport,
	DirectoryIAT,
	DirectoryDelayImport,
	DirectoryCLR,
	Sections = Directory + 16,
	Section,
	Headers,
	NTHeader,
	DOSHeader,
	OptionalHeader,
	RichHeader,
	Debug,
	Resources,
	ResourceTypeName,
	ResourceName,
	ResourceLnaguage,
	ContextMenu,
	CLR,
	
	Symbols,
	SymbolsFunctions,
	SymbolsGlobalData,
	SymbolsTypes,
	SymbolsEnums,

	Language,
	Resource,
	Disassembly,
	AsmEntryPoint,
	FileInHex,
	Anomalies,
	Xrefs,
	Overlay,
	FlowGraph,

	// a library (an archive of object files or import records)
	ArchiveMembers,
	ArchiveSymbols,
	ArchiveImports,
	ArchiveMember,

	Strings,

	// a COFF object file
	ObjectHeader,
	ObjectSections,
	ObjectSymbols,
	ObjectRelocations,
	ObjectSection,

	// a Control Flow Guard table (the index is its position in the tables of the file)
	GuardTable,

	// an ELF file
	ElfHeader,
	ElfProgramHeaders,
	ElfSections,
	ElfSymbols,
	ElfDynamic,
	ElfRelocations,
	ElfNotes,
	ElfSection,
	ElfFileInHex,
	ElfCode,

	// more of a COFF object file: its line numbers and its CodeView information. The items of an object have the member of the
	// library it is in (if it is in one) and a section (for ObjectSection) as their index: see CMainFrame::ObjectItem.
	ObjectLineNumbers,
	CodeView,
	CodeViewSymbols,
	CodeViewLines,
	CodeViewFiles,
	CodeViewTypes,

	ItemMask = 255,
};
DEFINE_ENUM_FLAG_OPERATORS(TreeItemType);

enum class SymViewType {
	None, Function, Data, UDT, Enum,
};

struct FlatResource : PEResFlat {
	std::wstring Name, Type, Language;
};

struct IMainFrame abstract {
	virtual HWND GetHwnd() const = 0;
	virtual BOOL ShowContextMenu(HMENU hMenu, DWORD flags, int x, int y, HWND hWnd = nullptr) = 0;
	virtual CUpdateUIBase& GetUI() = 0;
	virtual HIMAGELIST GetImageList() const = 0;
	virtual int GetIconIndex(UINT id) const = 0;
	virtual int GetDataDirectoryIconIndex(int index) const = 0;
	virtual void SetStatusText(int index, PCWSTR text) = 0;
	virtual CFindReplaceDialog* GetFindDialog() = 0;
	virtual DiaSession const& GetSymbols() const = 0;
	virtual std::vector<FlatResource> const& GetFlatResources() const = 0;
	virtual int GetResourceIconIndex(WORD resType) const = 0;
	virtual DiaSymbol GetSymbolForName(PCWSTR mod, PCWSTR name) const = 0;
	// Resolves an address to "name" or "name+0x1A" using the PDB (if any), falling back to exports and import slots.
	// Returns an empty string if nothing is known about the address.
	virtual std::wstring ResolveRva(DWORD rva) const = 0;
	virtual std::wstring ResolveVa(ULONGLONG va) const = 0;
	virtual std::vector<Anomaly> const& GetAnomalies() const = 0;
	// Shows a place in the file: in the hex view, or as disassembly if the offset is in a code section.
	virtual bool GoToFileOffset(int64_t offset, bool disassemble = false) = 0;
	// Shows an address (a virtual address): in a disassembly view that already contains it, in a new one if it is in a code
	// section, or in the hex view.
	virtual bool GoToVa(uint64_t va) = 0;
	// The places in the code that refer to addresses. Built from the whole file on first use.
	virtual XrefMap const& GetXrefs() = 0;
	// Lists the references to an address in a view of its own.
	virtual bool ShowXrefs(uint64_t va) = 0;
	// The flow graph of the function that contains the address (the code from the address if the function is not known)
	virtual bool ShowFlowGraph(uint64_t va) = 0;
	// A member of the library: an object as an object file (its header, sections...), anything else in the hex view
	virtual bool ShowArchiveMember(int member) = 0;
	// The data of a section of an object in the hex view, at an offset in the section (-1 for the start). The object is the object
	// file (member -1), or a member of the library.
	virtual bool ShowObjectSection(int section, int64_t offset, int member = -1) = 0;
	// The places of an ELF file: code is disassembled (x86, x64), anything else is shown in the hex view.
	// An address is a virtual address; an offset in a section is for relocatable objects, whose sections are all at 0.
	virtual bool ShowElfAddress(uint64_t address, bool code) = 0;
	virtual bool ShowElfSection(int section, int64_t offset, bool code) = 0;
	virtual bool ShowElfFileOffset(int64_t offset) = 0;
	// Remembers where the user is, so that Back returns to it.
	virtual void RecordNavigation() = 0;
	virtual bool AddToolBar(HWND tb) = 0;
	virtual bool DeleteTreeItem(HTREEITEM hItem) = 0;
	virtual bool CreateAssemblyView(std::span<const std::byte> code, uint64_t address, uint32_t rva, PCWSTR title, TreeItemType parent) = 0;
};

struct IView abstract {
	virtual CString GetTitle() const = 0;
	virtual HWND GetHwnd() const = 0;
	virtual void SetHTreeItem(HTREEITEM hItem) = 0;
	virtual HTREEITEM GetHTreeItem() const = 0;
	virtual bool DeleteFromTree() const = 0;
	// Where the user is in the view (a file offset for hex views, -1 if the view has no notion of position).
	// The history of Go To / Back / Forward uses these to return to the same place.
	virtual int64_t GetNavigationPosition() const { return -1; }
	virtual void SetNavigationPosition(int64_t) {}
	// Brings an address (a virtual address) into view if the view shows it. False if it does not.
	virtual bool GoToAddress(uint64_t va) { return false; }
	// The progress and the result of the VirusTotal scan, for the view that shows it
	virtual void SetVirusTotalStatus(vt::Status const&) {}
	// True if the view handles Save itself (ID_FILE_SAVE): its data, an image... Views with a list or text need not:
	// the main frame saves those.
	virtual bool CanSave() const { return false; }
};
