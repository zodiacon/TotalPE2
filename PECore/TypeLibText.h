#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// An IDL-like listing of a type library (the data of a TYPELIB resource, or of a .tlb file): its enums, structures,
// interfaces and classes. Empty, with 'error' set, if the data is not a type library that OLE can load.
std::wstring DescribeTypeLib(std::span<const std::byte> data, std::wstring& error);

// The GUIDs a type library names: its coclasses' (CLSIDs), its interfaces' and dispinterfaces' (IIDs), its own (LIBID); the
// 16 bytes of each as they are in memory. Empty if it is not one.
struct TypeLibGuid {
	enum class Kinds { Library, Class, Interface, Dispatch };
	Kinds Kind;
	std::string Bytes;
	std::wstring Name;
	std::wstring Help;
};
std::vector<TypeLibGuid> TypeLibGuids(std::span<const std::byte> data);

// The interfaces a type library describes (and the interface parts of its dual dispinterfaces): each one's name and the names
// of its methods in the order of its vtable (its bases' first: IUnknown's, IDispatch's), a property's accessors as MIDL names
// them ("get_Name", "put_Name")
struct TypeLibInterface {
	std::wstring Name;
	std::vector<std::wstring> Methods;
};
std::vector<TypeLibInterface> TypeLibInterfaces(std::span<const std::byte> data);
