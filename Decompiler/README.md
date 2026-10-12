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

Source: Ghidra **12.1.4** (tag `Ghidra_12.1.4_build`). The files are unchanged.

TotalPE copies `languages` next to its executable (`Decompiler\languages`) after it is built.

## Building

The project (`Decompiler.vcxproj`) builds the sources as Ghidra does on Windows (`WINDOWS`, `WIN32`, `WIN64`, `LOCAL_ZLIB`,
`/EHsc`), optimized in Debug as well. The decompiler registers its parts (the C printer, the architectures) through static
objects that nothing refers to, so the executable links the library whole: `/WHOLEARCHIVE:Decompiler.lib`.

## The `.sla` files

A `.sla` file is a processor's SLEIGH specification (`.slaspec` and its `.sinc` files, in Ghidra's
`Ghidra/Processors/<processor>/data/languages`) compiled by Ghidra's SLEIGH compiler. Its format belongs to the
decompiler's version: when the sources are updated, the `.sla` files are compiled again with the compiler of the same version.

The compiler is in the same folder of Ghidra's sources: the files of `CORE`, `SLEIGH` and `SLACOMP` in the Makefile
(`slgh_compile`, `slghparse`, `slghscan`), with zlib, built as a console program (same defines as above). Then:

```
sleigh x86-64.slaspec x86-64.sla
sleigh x86.slaspec x86.sla
```

## Updating

1. Copy the files listed above from the new version of Ghidra (the list of sources is in the Makefile of
   `src/decompile/cpp`; a new source file there goes into `Decompiler.vcxproj` as well).
2. Compile the `.sla` files again with that version's SLEIGH compiler.
3. Update the version above.
