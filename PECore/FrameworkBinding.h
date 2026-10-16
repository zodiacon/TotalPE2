#pragma once

#include <cstdint>
#include <functional>

class PEFile;

//
// A driver of the framework (KMDF): its WDF_BIND_INFO (the version of the framework it was built with, the pointer to the
// framework's table of functions that WdfVersionBind sets: WdfFunctions_NNNNN, how many it has; virtual addresses), and the
// pointer to its WDF_DRIVER_GLOBALS (WdfDriverGlobals), which the decompiler finds (what it passes WdfVersionBind).
//
struct FrameworkBinding {
	uint32_t Major{}, Minor{}, Build{};
	uint32_t Count{};
	uint64_t Table{};
	uint64_t Globals{};
};

// reads the image (the virtual addresses of its sections); false outside of it
using FrameworkImageReader = std::function<bool(uint64_t va, uint8_t* buffer, size_t size)>;

// The WDF_BIND_INFO of a driver of the framework among the data of an image: its size, a pointer to the name of the
// framework ("KmdfLibrary"), its version, the functions it has and the pointer to them (Table 0 if there is none)
FrameworkBinding FindFrameworkBinding(PEFile const& pe, FrameworkImageReader const& read);
// the table of a version before 1.15 is in the image (WDFFUNC WdfFunctions[]), not a pointer to it
inline bool FrameworkArray(FrameworkBinding const& binding) {
	return binding.Major == 1 && binding.Minor < 15;
}
