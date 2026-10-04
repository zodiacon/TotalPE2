#pragma once

#include <cstddef>
#include <span>
#include <string>

// An IDL-like listing of a type library (the data of a TYPELIB resource, or of a .tlb file): its enums, structures,
// interfaces and classes. Empty, with 'error' set, if the data is not a type library that OLE can load.
std::wstring DescribeTypeLib(std::span<const std::byte> data, std::wstring& error);
