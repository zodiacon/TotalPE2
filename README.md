# Total PE (take 2)

Yet another PE Viewer.

An improved version over **Total PE**, for Windows executables (EXE, DLL, SYS...) and more: static libraries, COFF object files and ELF files.

As usual, it's a work in progress.

![](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-1.png)
![](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-2.png)
![](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-3.png)
![](https://github.com/zodiacon/TotalPE2/blob/master/totalpe2-4.png)

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
* Text resources (XML, HTML, registry scripts, plain text) with syntax highlighting
* Type libraries, shown as IDL
* Fonts, with a preview
* Any resource or section can be saved to a file; bitmaps get their file header, so they open as `.bmp` files

## Code

* Disassembly (x86 and x64) with symbol names, cross references and switch tables
* Cross references: who calls a function, who uses an import, a string or an address
* Flow graphs of functions, exported as SVG
* Go To (address, RVA or file offset), Back and Forward

## Hex view

* Highlighting of the file's structure (headers, sections, directories, overlay)
* Data inspector, bookmarks, search, box selection
* Copy as hex, C array, Base64 and more; display in other bases; big endian

## ELF files

* Header: class, byte order, OS/ABI, type, machine, entry point, interpreter, needed libraries, SONAME, RUNPATH, build ID
* Program headers (with the sections in each segment), sections, symbols (`.symtab` and `.dynsym`), the dynamic section, relocations (REL, RELA and packed RELR) and notes
* Functions and the entry point are disassembled (x86 and x64), with symbol names

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
vcpkg install lief capstone scintilla lexilla wil nlohmann-json catch2 --triplet x64-windows-static
```

The `WTLHelper` submodule is needed as well (`git submodule update --init`).

`PECore.Tests` has the unit tests (Catch2). Some of them use files of the system (`kernel32.dll` and the like); the ones that need the Windows SDK or WSL skip themselves when those are not installed.

Enjoy!
