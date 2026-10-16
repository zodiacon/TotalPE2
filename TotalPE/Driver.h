#pragma once

#include <GhidraDecompiler.h>
#include <PEFile.h>
#include <FrameworkBinding.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

//
// What a kernel driver (a native image that imports from the kernel) sets up when it is loaded: its entry point stores its
// routines in its DRIVER_OBJECT (DriverUnload, MajorFunction[IRP_MJ_...], DriverStartIo), which the decompiler finds (in the
// entry point and the functions it passes the object to: GsDriverEntry calls DriverEntry); and the I/O control codes its
// device control routine handles (the constants it compares the code with, or switches on), decoded from CTL_CODE.
// A file system minifilter registers its callbacks with the filter manager instead: the FLT_REGISTRATION it passes to
// FltRegisterFilter (a constant, or one it fills on its stack) has its unload and instance callbacks, and points to the
// operations it filters (IRP_MJ_..., with their pre-operation and post-operation callbacks).
//
struct DriverInfo {
	static constexpr int MajorCount = 28;	// IRP_MJ_MAXIMUM_FUNCTION + 1
	enum class States { None, Analyzing, Done, Failed };
	States State{ States::None };
	std::string Error;
	DWORD DriverEntry{};		// the function that stores them (the entry point, or one it calls)
	DWORD Unload{}, StartIo{};
	DWORD AddDevice{};			// of a PnP driver (DriverObject->DriverExtension->AddDevice)
	DWORD Default{};			// one stored in all of MajorFunction (a loop), before the others
	std::array<DWORD, MajorCount> Major{};
	struct Ioctl {
		uint32_t Code;
		DWORD Function;			// where it is compared with
	};
	std::vector<Ioctl> Ioctls;
	bool Kmdf{ false };			// a driver of the framework (KMDF): the framework has the routines
	bool Minifilter{ false };	// a file system minifilter: it registers its callbacks with the filter manager
	// a minifilter's callbacks: those of its registration (Operation is -1; What: "FilterUnloadCallback"), and those of the
	// operations it filters (Operation: the IRP_MJ_ code, as the filter manager has it (a UCHAR); Post or not)
	struct Callback {
		std::wstring What;
		DWORD Rva;
		int Operation{ -1 };
		bool Post{ false };
	};
	std::vector<Callback> Callbacks;
	// a driver of the framework (KMDF): its binding (FrameworkBinding.h), and its callbacks: those it gives the framework
	// (Name: the field or parameter it is given in, "EvtIoDeviceControl"; Type: "PFN_WDF_IO_QUEUE_IO_DEVICE_CONTROL";
	// Function: the framework's function, "WdfIoQueueCreate")
	using Framework = FrameworkBinding;
	Framework Wdf;
	struct FrameworkCallback {
		std::wstring Name, Type, Function;
		DWORD Rva;
	};
	std::vector<FrameworkCallback> FrameworkCallbacks;
};

// a kernel driver: native, and imports from the kernel (ntoskrnl.exe, hal.dll...)
bool IsDriver(PEFile const& pe);
// "IRP_MJ_CREATE"
PCWSTR MajorFunctionName(int major);
// "DispatchCreate" of IRP_MJ_CREATE ("DispatchCreateClose" of more than one)
std::wstring DispatchName(std::vector<int> const& majors);
// the name of an operation of the filter manager ("IRP_MJ_CREATE", "IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION"; "0xE0")
std::wstring FilterOperationName(int operation);
// "PreCreate", "PostAcquireForSectionSynchronization"
std::wstring FilterCallbackName(int operation, bool post);

// reads the image (the virtual addresses of its sections); false outside of it
using DriverImageReader = FrameworkImageReader;
// a minifilter's FLT_REGISTRATION at an address: its callbacks, and those of its operations (false if it is not one)
bool ReadFilterRegistration(DriverImageReader const& read, uint64_t base, uint64_t va, bool pe64, DriverInfo& info);
// the FLT_REGISTRATIONs among the data of an image (a constant one: the size and version it starts with, its operations)
std::vector<uint64_t> FindFilterRegistrations(PEFile const& pe, DriverImageReader const& read);
// the arrays of FLT_OPERATION_REGISTRATION among the data of an image: those a registration points to, and the constant
// ones a minifilter copies into one it fills when it runs (each entry an operation, callbacks in the code, up to
// IRP_MJ_OPERATION_END)
std::vector<uint64_t> FindFilterOperations(PEFile const& pe, DriverImageReader const& read);
// the parts of a control code (CTL_CODE(DeviceType, Function, Method, Access)): "FILE_DEVICE_UNKNOWN", "0x800",
// "METHOD_BUFFERED", "FILE_ANY_ACCESS"
std::wstring IoctlDeviceType(uint32_t code);
std::wstring IoctlMethod(uint32_t code);
std::wstring IoctlAccess(uint32_t code);
// "CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)"
std::wstring IoctlText(uint32_t code);
// a value that is likely a control code (a device type of Microsoft's or of its own, not an NTSTATUS)
bool LooksLikeIoctl(uint32_t value);
// the control codes in decompiled C: the cases of a switch, the constants compared with (==, !=)
std::vector<uint32_t> IoctlsIn(std::string const& code);

// A driver of the framework: its binding, the functions that call WdfVersionBind (which they give the WDF_DRIVER_GLOBALS
// pointer), and those that use the framework's table (that call its functions)
struct FrameworkUse {
	DriverInfo::Framework Binding;
	std::vector<uint64_t> Binders, Users;
};

// Finds what the driver sets up (on the decompiler's thread): 'base' is the image's base, 'entry' its entry point (RVA).
// A minifilter: 'registers' are the functions that call FltRegisterFilter, 'registrations' the constant FLT_REGISTRATIONs
// of its data (the decompiler finds what those functions pass; these are taken if it does not). A driver of the framework:
// the callbacks the users of the framework give it.
void AnalyzeDriver(GhidraDecompiler& decompiler, uint64_t base, uint64_t imageSize, DWORD entry, bool pe64, DriverInfo& info,
	std::vector<uint64_t> const& registers = {}, std::vector<uint64_t> const& registrations = {}, DriverImageReader const& read = {},
	std::vector<uint64_t> const& operations = {}, FrameworkUse const& framework = {});
