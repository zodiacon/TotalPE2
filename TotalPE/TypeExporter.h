#pragma once

#include <DiaHelper.h>

struct IMainFrame;

enum class TypeFormat {
	C,
	Cpp,
	CSharp,
};

// Definitions of types from the symbols in source form
struct TypeExporter abstract final {
	// A type that can be exported: a struct, class, union or enum (a typedef gives the type it names); empty if none
	static DiaSymbol ExportableType(DiaSymbol const& sym);

	// The definition of a struct, class, union or enum, with the types it needs.
	// C and C++: definitions of the types it contains (ahead of it), declarations of the ones it points to. Unions and
	// structs without names (that DIA flattens into their parent) are reconstructed from the offsets of the members.
	// C++ has the base classes and virtual functions (for the vtable pointer) of a class.
	// C#: explicit layout ([FieldOffset]); bit fields are properties, arrays fixed buffers.
	// Sizes (and in C++ offsets) are checked with static assertions where the language has them.
	static std::wstring ToText(DiaSymbol const& type, TypeFormat format);

	// Asks for a file name and saves the definition as UTF-8; reports failures
	static bool Save(HWND hOwner, DiaSymbol const& type, TypeFormat format);

	// Copies the definition to the clipboard
	static bool Copy(HWND hOwner, DiaSymbol const& type, TypeFormat format);

	// The menu of a type (at a point on the screen): saving it in the formats there are
	static void ShowMenu(IMainFrame* frame, HWND hOwner, DiaSymbol const& type, POINT const& pt);
};
