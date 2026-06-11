# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TotalPE2 is a Windows PE (Portable Executable) file viewer with a tabbed GUI. It shows PE headers, sections, exports, imports, resources, symbols, disassembly, and many other PE components.

## Build

Open `TotalPE2.sln` in Visual Studio 2022 and build. Requires:
- Visual Studio 2022 (v145 toolset), C++20
- Vcpkg for dependencies (LIEF binary parser, Capstone disassembler)
- Windows SDK

Configurations: Debug/Release/ReleaseSigned × Win32/x64/ARM64. Output goes to `x64/Debug/`, `x64/Release/`, etc.

No test projects — verification is manual via the application.

## Architecture

### Projects

| Project | Type | Role |
|---------|------|------|
| `TotalPE` | Executable | Main GUI application |
| `PECore` | Static lib | PE file parsing (wraps LIEF) |
| `HexControl` | Static lib | Custom hex editor WTL control |
| `DiaHelper` | Static lib | DIA SDK wrapper for debug symbols |
| `WTLHelper` | Git submodule | Reusable WTL UI components |

### View-Based UI Pattern

The main window (`TotalPE/MainFrm.h`) hosts a tree (left) and a `NativeCustomTabView` (right). Clicking a tree node opens a specialized view as a new tab.

- `TotalPE/Interfaces.h` — defines `TreeItemType` enum (60+ node types) and `IMainFrame` interface
- `TotalPE/ViewFactory.cpp` — factory that maps `TreeItemType` → concrete view class instantiation
- All views inherit from `CViewBase<T>` (defined in `TotalPE/View.h`)

To add a new view: add an entry to `TreeItemType`, register it in `ViewFactory.cpp`, and create a `C*View` class inheriting `CViewBase<T>`.

### PE File Model

`PECore/PEFile.h` / `PECore/PEFile.cpp` is the central abstraction. It wraps a LIEF `Binary` object and exposes structured accessors for headers, sections, directories, imports, exports, resources, relocations, TLS, debug info, etc.

`PECore/libpe.h` provides lookup maps for PE constants (machine types, subsystems, characteristics, etc.) as `std::unordered_map<uint32_t, std::string_view>`.

### Technology Stack

- **GUI**: WTL (Windows Template Library) / ATL — no MFC, no Qt
- **Binary parsing**: LIEF (via Vcpkg)
- **Disassembly**: Capstone engine (via Vcpkg), displayed in Scintilla/Lexilla editor control
- **Debug symbols**: Microsoft DIA SDK (wrapped by `DiaHelper`)
- **Windows helpers**: WIL (Windows Implementation Library)
- **Security verification**: WinTrust API
