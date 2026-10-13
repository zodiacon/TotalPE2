# Decompiler

Functions as C, by the decompiler of [Ghidra](https://github.com/NationalSecurityAgency/ghidra), built into a static
library. `GhidraDecompiler.h` is the interface TotalPE uses; nothing of Ghidra is visible through it.

## What is here

| Path | What |
| --- | --- |
| `GhidraDecompiler.h`, `.cpp` | The interface: an image read through a callback, functions by address, C text out |
| `ghidra/cpp` | Ghidra's decompiler (`Ghidra/Features/Decompiler/src/decompile/cpp`), the files of its library: all the headers, the sources of `CORE`, `DECCORE` and `SLEIGH` in its Makefile, and `inject_sleigh`, `sleigh_arch`, `raw_arch`, `loadimage_xml`, `xml_arch` and `libdecomp` (not the console, the BFD loaders or the SLEIGH compiler) |
| `ghidra/zlib` | The subset of zlib 1.3.1 that Ghidra keeps with the decompiler (`.sla` files are compressed) |
| `ghidra/LICENSE`, `ghidra/NOTICE` | Ghidra's license (Apache 2.0) and notices |
| `languages/x86` | The x86 language definitions (`.ldefs`, `.pspec`, `.cspec`) and the compiled specifications `x86.sla` (32 bit) and `x86-64.sla` |
| `languages/AARCH64` | The ARM64 language definitions and the compiled specification `AARCH64.sla` |
| `BuildSla.ps1` | Compiles the `.sla` files again (see below) |

Source: Ghidra **12.1.4** (tag `Ghidra_12.1.4_build`). The files are unchanged, but for one fix: in `bitfield.cc`,
`BitFieldPullTransform::PullRecord::operator<` made two records without a read op each less than the other (an invalid
comparator, which the debug library's `list::sort` stops at). Apply it again after an update, if it is not fixed there.

`languages/AARCH64` has the ARM64 language: `AARCH64.ldefs`, `AARCH64.pspec`, `AARCH64.cspec` and `AARCH64_win.cspec` from
`Ghidra/Processors/AARCH64/data/languages`, and `AARCH64.sla` compiled from `AARCH64.slaspec` (the other languages of
the `.ldefs` have no `.sla`: they are not used).

TotalPE copies `languages` next to its executable (`Decompiler\languages`) after it is built.

## Building

The project (`Decompiler.vcxproj`) builds the sources as Ghidra does on Windows (`WINDOWS`, `WIN32`, `WIN64`, `LOCAL_ZLIB`,
`/EHsc`), optimized in Debug as well. The decompiler registers its parts (the C printer, the architectures) through static
objects that nothing refers to, so the executable links the library whole: `/WHOLEARCHIVE:Decompiler.lib`.

## The `.sla` files

A `.sla` file is a processor's SLEIGH specification (`.slaspec` and its `.sinc` files, in Ghidra's
`Ghidra/Processors/<processor>/data/languages`) compiled by Ghidra's SLEIGH compiler. Its format belongs to the
decompiler's version: when the sources are updated, the `.sla` files are compiled again with the compiler of the same version.

`BuildSla.ps1` does it: it clones the sources of the tag (sparse: the decompiler and the languages of x86 and AARCH64),
builds the compiler (the files of `CORE`, `SLEIGH` and `SLACOMP` in the decompiler's Makefile, with zlib, with Visual
Studio's compiler and the defines above), compiles `x86.slaspec`, `x86-64.slaspec` and `AARCH64.slaspec`, and puts the
`.sla` files in `languages`. The clone and the compiler are kept in a work folder (`%TEMP%\TotalPE-sla`) for the next run.

```
.\BuildSla.ps1                               # compile, and update the .sla files that changed
.\BuildSla.ps1 -Check                        # compile, and only tell whether they are the same (exit code 1 if not)
.\BuildSla.ps1 -Tag Ghidra_12.2_build -Definitions   # another version, with its .ldefs, .pspec and .cspec files
.\BuildSla.ps1 -Source C:\src\ghidra          # a checkout of Ghidra's sources instead of a clone
```

The `.sla` files of 12.1.4 that are here come out of it the same, byte for byte.

## Updating

1. Copy the files listed above from the new version of Ghidra (the list of sources is in the Makefile of
   `src/decompile/cpp`; a new source file there goes into `Decompiler.vcxproj` as well).
2. Compile the `.sla` files again with that version's SLEIGH compiler: `.\BuildSla.ps1 -Tag <the tag> -Definitions`.
3. Update the version above, and the default of `-Tag` in `BuildSla.ps1`.
4. Run `Decompiler.Tests`.

## Tests

`Decompiler.Tests` (Catch2) decompiles small pieces of machine code that it puts together in memory (`TestImage.h`): x64,
x86 and ARM64 functions, calls of functions by name (given ahead or asked for), calls through imports, thunks of imports and
tail calls (jumps through the import address table), strings in read-only memory, prototypes with the caller's types,
global variables and the addresses of the lines. It copies `languages` next to its executable, as TotalPE does.
