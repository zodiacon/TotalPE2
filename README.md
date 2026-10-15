# Total PE (take 2)

Yet another PE Viewer.

An improved version over **Total PE**, for Windows executables (EXE, DLL, SYS...) and more: static libraries, COFF object files and ELF files.

As usual, it's a work in progress.

![The summary of a PE file: hashes, checksum validation, import hash](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-1.png)
![The strings of the file, filtered](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-2.png)
![Exceptions, with the unwind information of a function](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-3.png)
![Disassembly with symbols and cross-references](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-4.png)
![Comparing two PE files](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-5.png)
![Search All: imports, exports, strings, resources, symbols and sections](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-6.png)
![An ELF file](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-7.png)
![A COFF object file with its CodeView debug information](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-8.png)

## What it opens

* **PE files** - executables, DLLs, drivers, 32 and 64 bit
* **Static libraries** (`.lib`, `.a`) - the members, the symbol index and the records of import libraries; an object member opens with everything an object file has
* **COFF object files** (`.obj`, regular and bigobj) - header, linker directives, sections, symbols, relocations and line numbers, and the CodeView debug information (`/Z7`, `/Zi`): symbols, source lines and files, types (or the PDB that has them)
* **ELF files** (Linux and friends) - executables, shared objects, relocatable objects and core dumps; 32 and 64 bit, either byte order

The kind of file is recognized by its contents, not its extension. Open files from the menu, the command line, by drag and drop, or from the recent files list.

## PE files

* Headers: DOS, NT and optional headers, the Rich header (with its key checked), shown as structures
* Summary: file hashes (MD5, SHA-1, SHA-256), import hash (imphash), and the checksum checked against the file
* Sections, with their entropy (high entropy suggests packed or encrypted data)
* All data directories:
  * Exports and imports (API sets resolved, imports by ordinal named), delay imports, bound imports, the IAT
  * Resources (see below)
  * Exceptions, with the x64 unwind information decoded: prolog codes, frame register, handler, chained functions
  * Security: Authenticode signatures and certificates, decoded and verified
  * Relocations, debug directory, TLS (with callbacks), global pointer
  * Load configuration, with the **Control Flow Guard** tables listed: CFG functions (with their flags: suppressed, export suppressed, XFG...), address-taken IAT entries, long jump targets and EH continuation targets
  * CLR (.NET) header and metadata
* Anomalies: heuristics that point out what looks unusual or malformed
* Overlay: the data after the last section, identified (installers, archives...) with its entropy and hashes
* Strings: the ASCII and UTF-16 strings of the whole file, with filtering, a minimum length, and cross references to the code that uses them
* Symbols (PDB) for functions, data, types and enums, loaded in the background; the symbol path is configurable
* VirusTotal: look up (or upload) the file with your API key

## Resources

* Version, manifest, string tables, message tables, accelerators
* Graphical views of dialogs and menus
* Icons and cursors (single and groups, animated ones too) - exported as `.ico` / `.cur` files
* Bitmaps and images (PNG, JPEG, GIF), with transparency
* Text resources (XML, HTML, INF scripts of `REGINST` resources, registry scripts, plain text) with syntax highlighting
* Type libraries, shown as IDL
* Event manifests of ETW providers (`WEVT_TEMPLATE`): events, templates and their fields, channels, levels, tasks, opcodes, keywords and value maps, with their message texts
* Fonts, with a preview
* The raw bytes of any resource, whatever view it has: Resource as Hex (Ctrl+Shift+H) or Open as Hex on its tree item, with the offsets of the file
* Any resource or section can be saved to a file; bitmaps get their file header, so they open as `.bmp` files

## Code

* Disassembly (x86, x64 and ARM64) with symbol names, cross references and switch tables (x86 and x64); the addresses that ARM64 code builds with `adrp` and `add` / `ldr` are resolved; the Disassemble and Decompile Function boxes list the functions whose names match what is typed (exports, symbols, the ones you named), with their parameters, and with nothing typed the ones opened last (kept with the file's names); a part of an address lists the functions there, those without names too
* Cross references: who calls a function, who uses an import, a string or an address
* Flow graphs of functions, exported as SVG
* Go To (address, RVA or file offset), Back and Forward
* Decompiled C (Ghidra's decompiler): rename and retype variables and functions (N, Y), comment instructions (;) and bookmark them (Ctrl+B), kept for the file; the functions of Windows that the code calls have their prototypes, and their constants (flags, `HRESULT`, `NTSTATUS`...) their names, from the Win32 metadata (a value of several names shows the one chosen: the tip of a constant lists them, its menu shows another); functions show their `__try`/`__except`/`__finally` regions and C++ `try`/`catch` blocks (x64 and ARM64: from the exception data, `__C_specific_handler`, `__CxxFrameHandler3` and `4`; x86: from the handler the prologue registers, `_except_handler3` and `4`, `__CxxFrameHandler3`, and the try levels the code sets), with the code of the handlers that are functions of their own (`__finally`, `catch`) in them (the function's variables they use by name: `frame->h`, x86's `ADJ(frame)->h`; x86's try level and C++ state are `__try_level` and `__state`) and links to the others; the local variables of the PDB have their names, those optimized code keeps in registers too (x86, x64)
* Library functions (without symbols): the functions of the static libraries of Visual C++ (the CRT, the C++ library, the runtime) are found by their code (signatures of their bytes, with what is relocated left out, made from the libraries of the toolsets of x64, x86 and ARM64) and named, with the functions they call; Tools > Add Library Signatures makes signatures of other static libraries (`.lib`, `.obj`) and names their functions
* C++ classes (without symbols): the classes that have virtual functions, from the run-time type information of Visual C++ (x64, x86 and ARM64), in the Classes view: their bases, vtables (`Class::vftable`) and virtual functions (`Class::vfunc_N`, named for the class that declares them); in the decompiled C, a virtual function's `this` is a pointer to its class, whose `vftable` points to the structure of its virtual functions. A vtable of an interface the class implements (COM's: of the Win32 metadata, or of the file's type library) has its methods' names (`CComObject<CMolecule>::get_X`, `SimpleChannelHook::ClientGetSize`), and those of the metadata have their prototypes (`HRESULT ClientGetSize(IChannelHook *This, GUID *uExtent, GUID *riid, uint *pDataSize)`)
* COM: the GUIDs of Windows in the data are named from the Win32 metadata (`IID_IFileOpenDialog`, `CLSID_FileOpenDialog`, `FOLDERID_...`); in the decompiled C, a variable a function puts an interface in (`CoCreateInstance(..., &IID_IFoo, &p)`, `QueryInterface`) is a pointer to the interface (`pFoo`), whose methods are named (`pFoo->lpVtbl->Show(pFoo, ...)`); calls through Control Flow Guard's dispatch are the calls they make (without symbols too: the dispatch is found from the load configuration); a stub that jumps to an import (`jmp [__imp_X]`) is named for it
* Kernel drivers: the routines the entry point stores in the `DRIVER_OBJECT` (DriverUnload, DriverStartIo, AddDevice, the dispatch routine of each `IRP_MJ_...`) and the control codes the device control routine handles (decoded: device type, function, method, access), in the Driver view; without symbols the routines are named (`DispatchDeviceControl`...), and the routines have their prototypes (`NTSTATUS DriverEntry(DRIVER_OBJECT *DriverObject, UNICODE_STRING *RegistryPath)`, `NTSTATUS DispatchDeviceControl(DEVICE_OBJECT *DeviceObject, IRP *Irp)`...) with the structures of the kernel drivers use (`Irp->Tail.Overlay.CurrentStackLocation`, `IrpSp->Parameters.DeviceIoControl.IoControlCode`); `MajorFunction` is indexed by name (`MajorFunction[IRP_MJ_DEVICE_CONTROL]`), and the code's comparisons with the control codes have their `CTL_CODE` as comments; a file system minifilter's callbacks, from the `FLT_REGISTRATION` it passes to `FltRegisterFilter` (a constant one, or one it fills on its stack): its unload and instance callbacks, and the pre-operation and post-operation callbacks of each operation it filters (an array of them it fills when it runs is found by the constant one it copies), typed as the filter manager calls them (`FLT_PREOP_CALLBACK_STATUS PreCreate(FLT_CALLBACK_DATA *Data, FLT_RELATED_OBJECTS *FltObjects, void **CompletionContext)`: `Data->Iopb->Parameters.Create.Options`, `Data->IoStatus.Status`); a KMDF driver's: the version of the framework it binds to (its `WDF_BIND_INFO`), and the callbacks it gives the framework (`EvtDriverDeviceAdd`, `EvtIoDeviceControl`, `EvtDevicePrepareHardware`, `EvtInterruptIsr`, `EvtTimerFunc`...: the fields of the configuration structures it passes, `WDF_DRIVER_CONFIG`, `WDF_IO_QUEUE_CONFIG`, `WDF_PNPPOWER_EVENT_CALLBACKS`..., and the callbacks it passes), named and typed (`void EvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength, ULONG IoControlCode)`), with the control codes its queues handle; the calls of the framework's functions, through its table, are shown as the driver's source has them (`WdfDeviceCreate(&DeviceInit, &attributes, &device)`, of the prototypes of the WDK's 458 functions), and the structures it fills are typed (`config.EvtIoDeviceControl = EvtIoDeviceControl;`)
* COM view: the classes a DLL serves (it exports `DllGetClassObject`): their CLSIDs, found in the code of `DllGetClassObject` and what it calls (the CLSIDs it compares with, the tables of classes it looks them up in) and in the tables of classes of its data (ATL's object map, WRL's creator map, DirectShow's templates); those an EXE serves: the CLSIDs it passes `CoRegisterClassObject` (an array of them it registers in a loop too), and those of the same tables, with the functions that make them and what this computer's registry has of them (their names, their servers); the classes its type library declares are there too (those `DllGetClassObject` does not make: made by others); the GUIDs of the file's type library are named for it (`CLSID_Molecule`, `IID_IMolecule`), other CLSIDs that have no names for the registry's names of the classes (`CLSID_ColorCalibrationLoader`). And the calls that pass the GUIDs of Windows, with the function each is in: the classes the program creates (`CoCreateInstance` of `CLSID_FileOpenDialog`, for `IID_IFileOpenDialog`), the interfaces it asks for (`QueryInterface`, through a pointer), the other GUIDs it passes (`FOLDERID_LocalAppData` to `SHGetKnownFolderPath`)
* Windows API: the functions and types of Windows from the Win32 metadata (all of them, or the file's imports), with their C declarations: prototypes, enums and their values, structs with the offsets of their fields, callbacks, COM interfaces; in a window of its own (Tools > Windows API), with Back/Forward through the types followed, and the places the file calls a function (References); Microsoft's documentation of the one selected below its declaration (Microsoft Edge WebView2), or in the default browser (a search of it for one that has no page); the C's context menu has the documentation of a function of Windows (of a constant: of the function it is passed to)

## Hex view

* Highlighting of the file's structure (headers, sections, directories, overlay)
* Data inspector, bookmarks, search, box selection
* Copy as hex, C array, Base64 and more; display in other bases; big endian

## ELF files

* Header: class, byte order, OS/ABI, type, machine, entry point, interpreter, needed libraries, SONAME, RUNPATH, build ID
* Program headers (with the sections in each segment), sections, symbols (`.symtab` and `.dynsym`), the dynamic section, relocations (REL, RELA and packed RELR) and notes
* Functions, code sections, executable segments, the init and fini functions and the entry point are disassembled (x86, x64 and ARM64), with symbol names; Go To takes addresses and file offsets
* C++ names are demangled (the Itanium ABI of GCC and Clang): symbols, relocations, the disassembly, Search All
* Debug information: the DWARF (versions 2 to 5, zlib-compressed sections too) compile units and functions; a separate debug file is found by `.gnu_debuglink` or the build ID (next to the file, in its `.debug` directory, or in `/usr/lib/debug` of a WSL distribution)
* Strings (ASCII and UTF-16) with their sections and addresses

## General

* Tabs, a tree of everything in the file, Find in every view
* Compare With (File menu): compares the PE file with another - file, headers, data directories, sections (and their content), imports, exports, resources, version and debug information; differences are colored, and an item opens in the file with a double-click
* Search All (Ctrl+Shift+F): one search through the imports, exports, strings, resources, symbols and section names of the file (PE, object, library or ELF); double-click a result to go to it
* Save (Ctrl+S) saves what the active view shows: a list as CSV or text, text, the bytes of a hex view, a flow graph as SVG
* Export List (Ctrl+Shift+S) saves any list as CSV or tab separated text
* Dark mode

## Building

Visual Studio 2026 (toolset v145), C++20. The dependencies come from [vcpkg](https://vcpkg.io) (classic mode, triplet `x64-windows-static`):

```
vcpkg install lief capstone scintilla lexilla wil nlohmann-json catch2 webview2 --triplet x64-windows-static
```

The `WTLHelper` submodule is needed as well (`git submodule update --init`).

`PECore` has everything that is not UI: the parsers (PE, COFF, libraries, ELF, CodeView, resources), the analysis (disassembly, flow graphs, anomalies, signatures, comparison) and the formatting of values. `TotalPE` is the UI on top of it.

The prototypes of the functions of Windows come from the Win32 metadata (`Windows.Win32.winmd`), which is under the license of the Windows SDK and is not part of the sources: `TotalPE\GetWin32Metadata.ps1` downloads it into `Win32Metadata`, and the build copies it next to `TotalPE.exe`. Without it, imports are decompiled without their prototypes.

The signatures of the static libraries of Visual C++ (`TotalPE\Signatures\msvc.sig`, copied next to `TotalPE.exe` by the build) are made by `Tools\BuildSignatures.ps1`, from the libraries of the toolsets and Windows SDKs installed (run it again when a new toolset comes out). The signatures of the user's libraries go to `%LOCALAPPDATA%\TotalPE\Signatures`.

`PECore.Tests` has the unit tests (Catch2). Some of them use files of the system (`kernel32.dll` and the like); the ones that need the Windows SDK or WSL skip themselves when those are not installed. `Decompiler.Tests` has those of the decompiler, on small pieces of x86, x64 and ARM64 code.

Enjoy!
