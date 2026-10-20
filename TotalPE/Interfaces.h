#pragma once

#include <DiaHelper.h>
#include <PEFile.h>
#include "PEAnomalies.h"
#include "CodeAnalysis.h"
#include "Overlay.h"
#include "VirusTotal.h"
#include <RpcInterfaces.h>
#include <WinRtServer.h>
#include "TypeExporter.h"
#include <GhidraDecompiler.h>
#include <optional>
#include <map>
#include "Rtti.h"
#include "Driver.h"
#include "ComServer.h"
#include "Authenticode.h"

class Annotations;

static const UINT WM_UPDATE_DARKMODE = WM_APP + 56;
// posted by the symbol loading thread: wParam is the generation of the request, lParam a DiaSession* (or null)
static const UINT WM_SYMBOLS_LOADED = WM_APP + 57;
// posted by the VirusTotal thread: wParam is the generation of the scan, lParam a vt::Status* that the receiver deletes
static const UINT WM_VT_STATUS = WM_APP + 58;
// sent by the decompiler's thread: lParam is a std::function<void()> to call (what the decompiler asks of the frame)
static const UINT WM_DECOMPILER_ASK = WM_APP + 59;
// posted by the decompiler's thread: lParam is the result of a function's decompilation, that the receiver deletes
static const UINT WM_DECOMPILED = WM_APP + 60;
// posted by the thread that finds the cross-references: wParam is the generation of the request, lParam an XrefMap* (null
// if there are none) that the receiver deletes
static const UINT WM_XREFS_BUILT = WM_APP + 61;
// posted by the decompiler's thread: wParam is the generation of the request, lParam a DriverInfo* that the receiver deletes
static const UINT WM_DRIVER_ANALYZED = WM_APP + 62;
// posted by the thread that looks for the catalog of the system that signs the file: wParam is the generation of the request,
// lParam a CatalogSignature* (null if no catalog has the file) that the receiver deletes
static const UINT WM_CATALOG_FOUND = WM_APP + 63;

constexpr uint32_t ItemShift = 8;

// A call that passes GUIDs of Windows (of COM): the function it is in (RVA), where it is (VA), what it calls ("CoCreateInstance";
// one through a pointer, QueryInterface: "(through a pointer)"), and the GUIDs it passes (RVAs): of classes, of interfaces,
// others (FOLDERID_...)
struct ComUse {
	DWORD Function;
	uint64_t Call;
	std::wstring Callee;
	std::vector<DWORD> Classes, Interfaces, Others;
};

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

	// a comparison with another PE file (the index tells the comparisons apart)
	Comparison,

	// the debug information of an ELF file (DWARF)
	ElfDebugInfo,
	ElfCompileUnits,
	ElfFunctions,

	// the data of a resource in hex (the index is that of the resource, as for Resource)
	ResourceHex,

	// the node of the functions disassembled by name or address (the ones that are not exports)
	DisassemblyFunctions,

	// a type from the symbols (the index is the symbol's ID)
	SymbolType,

	// the symbols of a kind, under Symbols (functions and enums have their own above)
	SymbolsClasses,
	SymbolsStructs,

	// the definition of a type from the symbols as source (the index is the symbol's ID and the format)
	TypeDefinition,

	// a function decompiled (the index is its RVA)
	Decompiled,
	// a function's disassembly and C side by side (the index is its RVA)
	SideBySide,
	// the functions of the image
	Functions,
	// the callers and callees of a function (the index is its RVA)
	CallGraph,
	// the types of a .NET file and the IL of their methods
	ClrTypes,
	// the protections of the file: ASLR, DEP, CFG, CET, its signature...
	SecuritySummary,
	// the user's bookmarks of the file
	Bookmarks,
	// the functions and types of Windows (from the Win32 metadata)
	Win32Api,
	// the C++ classes of the run-time type information
	Classes,
	// what a kernel driver sets up: its routines, its control codes
	Driver,
	// the calls that pass the GUIDs of COM: the classes the program creates, the interfaces it asks for
	Com,
	// the RPC interfaces the program serves (their procedures) and those it calls
	Rpc,
	// the Windows Runtime classes the program serves (their factories, their interfaces)
	WinRt,
	// the catalog of the system that signs a file that has no signature of its own
	CatalogSignature,

	ItemMask = 255,
};
DEFINE_ENUM_FLAG_OPERATORS(TreeItemType);

enum class SymViewType {
	None, Function, Data, UDT, Enum, All, Class, Struct,
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
	// the functions of static libraries in the image (by their code), by RVA
	virtual std::map<DWORD, std::wstring> const& LibraryNames() const = 0;
	// the classes of the program (from its run-time type information)
	virtual std::vector<RttiClass> const& Classes() const = 0;
	// what the file sets up, if it is a kernel driver (once the decompiler has found it)
	virtual DriverInfo const& Driver() const = 0;
	// the RPC interfaces of the image (its RPC_SERVER_INTERFACEs and RPC_CLIENT_INTERFACEs), and the name of one: of the
	// symbols ("srvsvc" of srvsvc___RpcServerInterface), of Windows' protocols, or of its UUID ("Rpc_4B324FC8")
	virtual std::vector<RpcInterface> const& RpcInterfaces() const = 0;
	virtual std::wstring RpcInterfaceName(RpcInterface const& rpc) const = 0;
	// the Windows Runtime classes the image serves (of WRL's creator map, and of the registry)
	virtual std::vector<WinRtClass> const& WinRtClasses() const = 0;
	// the calls that pass the GUIDs of Windows (once the cross-references are there: empty until then)
	virtual std::vector<ComUse> const& ComUses() = 0;
	// the COM classes the DLL serves (DllGetClassObject; once the cross-references are there)
	virtual std::vector<ComServerClass> const& ComServerClasses() const = 0;
	// What the file's type library declares of a GUID (at an RVA): "coclass Molecule", "interface IMolecule"; empty if none
	virtual std::wstring TypeLibDeclaration(DWORD guid) const = 0;
	// shows that declaration in the view of the type library
	virtual bool ShowTypeLibDeclaration(DWORD guid) = 0;
	// shows a GUID (its 16 bytes) in the COM view: the class of that CLSID the file serves, or the first call that passes it
	virtual bool ShowInComView(std::string const& guid) = 0;
	// the name of the function at an RVA without its parameters and return type ("InvokeOpenDialog", "ui::Widget::Draw")
	virtual std::wstring ShortFunctionName(DWORD rva) const = 0;
	virtual std::wstring ResolveVa(ULONGLONG va) const = 0;
	virtual std::vector<Anomaly> const& GetAnomalies() const = 0;
	// the catalog of the system that signs the file, if it has no signature of its own (null if none does, or not known yet)
	virtual CatalogSignature const* GetCatalogSignature() const = 0;
	// Shows a place in the file: in the hex view, or as disassembly if the offset is in a code section.
	virtual bool GoToFileOffset(int64_t offset, bool disassemble = false) = 0;
	// Shows an address (a virtual address): in a disassembly view that already contains it, in a new one if it is in a code
	// section, or in the hex view.
	virtual bool GoToVa(uint64_t va) = 0;
	// The places in the code that refer to addresses. Found from the whole file on a thread of its own, when the file is
	// opened: empty until then (views are told when they are there: IView::XrefsChanged).
	virtual XrefMap const& GetXrefs() = 0;
	virtual bool XrefsReady() const = 0;
	// Lists the references to an address in a view of its own.
	virtual bool ShowXrefs(uint64_t va) = 0;
	// The flow graph of the function that contains the address (the code from the address if the function is not known)
	virtual bool ShowFlowGraph(uint64_t va) = 0;
	// The functions that the function at an address calls, and those that call it, as a graph.
	virtual bool ShowCallGraph(uint64_t va) = 0;
	// The code of the function at an address in a view of its own: under the exports for an export, otherwise under Disassembly
	virtual bool DisassembleFunction(uint64_t va) = 0;
	// The function at an address as C (decompiled), in a view of its own next to where its disassembly goes
	virtual bool DecompileFunction(uint64_t va) = 0;
	// The function's disassembly and C side by side, aligned, in a view of its own (next to where its disassembly goes)
	virtual bool ShowSideBySide(uint64_t va) = 0;
	// The user's names and types, kept for the file: of a function (an empty name: the one it had), and of a variable that
	// the C of a function shows ('variables': all those it shows). A name or type that is not given stays as it is; an empty
	// name undoes the changes to the variable, an empty type the change of its type. The views of C are decompiled again.
	// False if there is nothing to change.
	virtual bool RenameFunction(uint64_t va, std::wstring const& name) = 0;
	virtual bool ChangeVariable(uint64_t function, GhidraDecompiler::Variable const& variable, std::optional<std::string> const& name,
		std::optional<std::string> const& type, GhidraDecompiler::Variables const& variables) = 0;
	// The user's comments and bookmarks, kept for the file (with the names and types). A change is shown by the views that
	// show them (IView::AnnotationsChanged), and by the code decompiled again.
	virtual Annotations const& GetAnnotations() const = 0;
	virtual std::wstring CommentAt(uint64_t va) const = 0;
	virtual bool IsBookmarked(uint64_t va) const = 0;
	// the comment of an address as the user enters it (an empty one: none); false if there can be none there
	virtual bool EditComment(uint64_t va) = 0;
	// a bookmark at an address (named by the user), or the one there removed
	virtual bool ToggleBookmark(uint64_t va) = 0;
	virtual bool SetBookmark(uint64_t va, std::wstring const& name) = 0;
	virtual bool RemoveBookmark(uint64_t va) = 0;
	virtual bool ShowBookmarks() = 0;
	// The Windows API view, at a function or type (by its name); whether the Win32 metadata has one of the name
	virtual bool ShowWin32Api(std::string const& name, bool documentation = false) = 0;
	virtual bool IsWin32Api(std::string const& name) = 0;
	// a function or type of Windows that Microsoft's documentation has a page for
	virtual bool IsWin32Documented(std::string const& name) = 0;
	// the places the file calls a function it imports (by its name), in the main window; false if it does not import it
	virtual bool ShowImportReferences(std::string const& name) = 0;
	// A constant of Windows (of an enum): the names of its value, the one shown first; empty if it is not one
	virtual std::vector<std::string> ConstantNames(std::string const& name, uint64_t& value, std::string& type) = 0;
	// one of the names of a value is shown wherever the value is (the code decompiled is decompiled again)
	virtual void PreferConstantName(std::string const& name) = 0;
	// A view in place of another ("in the same window"): 'open' opens it (as DecompileFunction does), then it takes the
	// place of the view, which is closed. Nothing is closed if 'open' fails, or brings up the view itself.
	virtual bool ReplaceView(HWND hView, std::function<bool()> const& open) = 0;
	// The layout of a type from the symbols (a struct, class, union or enum) in a view of its own, under Symbols
	virtual bool ShowType(DiaSymbol const& type) = 0;
	// The definition of a type from the symbols as source (C, C++ or C#) in a view of its own, under Symbols
	virtual bool ShowTypeDefinition(DiaSymbol const& type, TypeFormat format) = 0;
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
	// Selects the row of an item: an export ("#ordinal" or its name), an imported module, or a function of a module
	// ("#ordinal" or its name, 'owner' is the module). False if the view has no such item.
	virtual bool SelectItem(std::wstring_view name, std::wstring_view owner = {}) { return false; }
	// The cross-references were found (on a thread of their own, after the file was opened): a view that shows them shows
	// itself again.
	virtual void XrefsChanged() {}
	// The user's comments or bookmarks changed: a view that shows them shows them again
	virtual void AnnotationsChanged() {}
};
