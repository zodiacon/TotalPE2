#pragma once

#include <PEFile.h>
#include <CodeAnalysis.h>
#include <functional>
#include <string>
#include <vector>

//
// The COM classes a DLL serves (it exports DllGetClassObject): their CLSIDs, found in the code of DllGetClassObject and the
// functions it calls (it compares rclsid with them); those an EXE serves, that it passes CoRegisterClassObject; and in the
// tables of classes of their data, by their layouts (ATL's object map: the CLSID and the functions of the class; WRL's
// creator map; DirectShow's templates). The CLSIDs of the Win32 metadata have its names; the others are those of this
// computer's registry (HKEY_CLASSES_ROOT\CLSID).
//
struct ComServerClass {
	DWORD Guid;					// RVA of the CLSID
	std::wstring Source;		// how it was found: "DllGetClassObject", "object map (ATL)"...
	DWORD Creator{};			// the function of the table that makes the class (or its factory), if there is one
	std::wstring Registered;	// its name in the registry of this computer
	std::wstring Server;		// and its server there (InprocServer32, LocalServer32)
};

// what this computer's registry has of a CLSID ("{...}"): its name, its server (InprocServer32, LocalServer32)
void ComClassRegistry(std::wstring const& guid, std::wstring& name, std::wstring& server);
// "{6D5140C1-7436-11CE-8034-00AA006009FA}" of 16 bytes as they are in memory
std::wstring GuidText(uint8_t const* bytes);
// 16 bytes that are a GUID: of RFC 4122 (version 1 to 5), or of Microsoft's variant ({xxxxxxxx-0000-0000-C000-000000000046})
bool LooksLikeGuid(uint8_t const* bytes);
// a COM server: a DLL that exports DllGetClassObject, or an EXE that registers its classes (it imports CoRegisterClassObject)
bool IsComServer(PEFile const& pe);
// 'isNotClass' tells the GUIDs known to be others' (the IIDs of interfaces, the metadata's other GUIDs): they are not CLSIDs
std::vector<ComServerClass> FindComServerClasses(PEFile const& pe, XrefMap const& xrefs, std::function<bool(DWORD rva)> const& isNotClass);
