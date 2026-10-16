#pragma once

#include <PEFile.h>
#include <string>
#include <vector>

//
// The Windows Runtime classes a file serves: a DLL that exports DllGetActivationFactory (of in-process classes), an EXE
// that registers its factories (RoRegisterActivationFactories). They are found in WRL's creator map (the entries of
// runtime classes: the function that makes the class's factory, the one that gives its name, the one that gives its trust
// level, the factory's cache) and in this computer's registry (the classes whose server is the file:
// HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId). The interfaces of a class are those of its type in the
// metadata of Windows (System32\WinMetadata): those of its factories and statics, which the factory implements, and those
// its instances implement.
//
struct WinRtClass {
	std::wstring Name;			// "Windows.Globalization.Calendar"
	std::wstring Source;		// how it was found: "creator map (WRL)", "registry"; both: "creator map (WRL), registry"
	DWORD Entry{};				// RVA of its creator map's entry (0 if it is not in one)
	DWORD Creator{};			// the function that makes the class's factory
	DWORD NameFunction{};		// the one that gives its name (getRuntimeName)
	DWORD TrustFunction{};		// the one that gives its trust level (getTrustLevel)
	// of the registry (-1: not registered)
	int ActivationType{ -1 };	// 0: in-process (a DLL), 1: out-of-process (a server)
	int Threading{ -1 };		// 0: both, 1: STA, 2: MTA
	int TrustLevel{ -1 };		// 0: base, 1: partial, 2: full
	std::wstring Server;		// the DLL (DllPath), or the server and its EXE ("Name: path")
};

// a server of runtime classes: it exports DllGetActivationFactory, or imports RoRegisterActivationFactories
bool IsWinRtServer(PEFile const& pe);
// The runtime classes of a file (its path: the registry's classes are those whose server it is), by their names
std::vector<WinRtClass> FindWinRtClasses(PEFile const& pe, std::wstring const& path);

// A method of an interface of the metadata, and its slot in the interface's vtable (after IInspectable's six)
struct WinRtMethod {
	std::wstring Name;
	std::wstring Declaration;	// "Calendar CreateCalendarDefaultCalendarAndClock(IIterable<string> languages, string calendar, string clock)"
	uint32_t Slot{};
};

struct WinRtInterface {
	std::wstring Name;			// "Windows.Globalization.ICalendarFactory"
	std::wstring Role;			// "factory", "statics", "composable factory", "instance", "default (instance)"
	std::wstring Guid;			// "{83F58412-E56B-4C75-A66E-0F63D57758A6}", of its [Guid]
	std::vector<WinRtMethod> Methods;	// (none of a generic one: IVector<T>...)
};

// What the metadata of Windows has of a runtime class: the file it is in (empty if it is not in one) and its interfaces;
// 'activatable' tells that its factory makes instances with no arguments (IActivationFactory::ActivateInstance)
struct WinRtClassMetadata {
	std::wstring File;
	bool Activatable{ false };
	std::vector<WinRtInterface> Interfaces;
};
WinRtClassMetadata FindWinRtClassMetadata(std::wstring const& name);
