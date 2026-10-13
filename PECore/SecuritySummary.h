#pragma once

#include <string>
#include <vector>

class PEFile;

// How well a file uses the protections of Windows: ASLR, DEP, Control Flow Guard, CET, /GS, its signature and more. Each
// feature is read from the headers, the load configuration, the debug directory, the sections, the signature and the
// manifest; none of them runs the file.

enum class SecurityState : uint8_t {
	Enabled,		// the protection is on
	Partial,		// on, but with a gap (ASLR without relocations, a signature that is not trusted...)
	Disabled,		// off
	NotApplicable,	// does not apply to this kind of file (SafeSEH in 64-bit code, CET on ARM64)
	Info,			// a fact rather than a protection (the execution level of the manifest)
};

struct SecurityFeature {
	std::wstring Name;
	SecurityState State{ SecurityState::Disabled };
	std::wstring Details;
};

// 'verifyTrust' also asks the system (WinVerifyTrust) whether the signature is valid and trusted, which needs the file on disk
std::vector<SecurityFeature> AnalyzeSecurity(PEFile const& pe, bool verifyTrust = true);

const wchar_t* SecurityStateToString(SecurityState state);

// The execution level and UI access of an application manifest (its XML): "asInvoker", "requireAdministrator"... Empty
// strings if the manifest does not say.
struct ManifestTrust {
	std::wstring Level;
	std::wstring UiAccess;
};
ManifestTrust ParseManifestTrust(std::string_view xml);
