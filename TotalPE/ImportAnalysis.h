#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <PEFile.h>

// Whether an import is by ordinal rather than by name. The thunk of a 64-bit file is 8 bytes wide, that of a 32-bit file 4.
bool IsOrdinalImport(PEImportFunction const& fn, bool is64);
// The ordinal of an import by ordinal (0 for an import by name).
uint16_t ImportOrdinal(PEImportFunction const& fn);

// The name of an ordinal in a DLL, from the export table of the DLL on this system: "ws2_32.dll", 10 -> "ioctlsocket".
// Looks in the system directory of the importer's bitness. Empty if the DLL cannot be found or has no such export.
// This is what the loader would bind on this machine; the import hash uses the fixed tables of pefile instead.
std::string ResolveOrdinalName(std::string const& dll, uint16_t ordinal, bool importerIs32Bit);

using OrdinalNameLookup = std::function<std::string(std::string const& dll, uint16_t ordinal)>;

// The string whose MD5 is the import hash: "kernel32.exitprocess,user32.messageboxa". Follows the algorithm of the
// pefile library: modules without their .dll/.ocx/.sys extension, lowercase names, and "ordN" for ordinals. Ordinals of
// ws2_32, wsock32 and oleaut32 are replaced by the name that 'lookup' returns for them (null lookup: use pefile's tables).
std::string ImphashInput(PEFile const& pe, OrdinalNameLookup const& lookup = {});

// The import hash ("imphash") of the file: identical to the pefile library's get_imphash(). Empty if there are no imports.
std::string ComputeImphash(PEFile const& pe);
