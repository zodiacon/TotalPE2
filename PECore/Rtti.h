#pragma once

#include <cstdint>
#include <string>
#include <vector>

class PEFile;

//
// The classes of an MSVC C++ program that have virtual functions, from their run-time type information (RTTI): before each
// vtable is a pointer to a complete object locator, which points to the class's type descriptor (its decorated name) and to
// its class hierarchy descriptor (its base classes). x64 and ARM64 have RVAs in them (and a locator has its own), x86 VAs.
//
struct RttiClass {
	std::string Decorated;		// the type descriptor's name (".?AVWidget@ui@@")
	std::string Name;			// "ui::Widget"
	bool Struct{ false };		// declared as a struct (".?AU")
	uint32_t TypeDescriptor{};	// RVA
	std::vector<std::string> Bases;	// the base classes, all of them, in the order of the hierarchy (direct ones first)
	std::vector<int32_t> BaseOffsets;	// where each is in the object (-1: a virtual base, whose place is not fixed)
	// A vtable of the class: its own (offset 0) and those of its bases that are not at its start (multiple inheritance)
	struct Vtable {
		uint32_t Rva{};			// of the first function's pointer
		uint32_t Offset{};		// where the part of the object it is of is
		uint32_t Locator{};		// RVA of the complete object locator
		std::vector<uint32_t> Functions;	// RVAs
	};
	std::vector<Vtable> Vtables;
};

// The classes, in the order of their names; none if the program has no RTTI
std::vector<RttiClass> ReadRtti(PEFile const& pe);

// "ui::Widget" of ".?AVWidget@ui@@" (struct: ".?AU"); empty if it is not a type descriptor's name
std::string RttiTypeName(std::string const& decorated);
