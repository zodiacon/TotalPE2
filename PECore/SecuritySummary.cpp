#include "pch.h"
#include "SecuritySummary.h"
#include "PEFile.h"
#include "Authenticode.h"
#include <cstddef>
#include <mscat.h>

#pragma comment(lib, "wintrust.lib")

namespace {
	// the flags of the extended DLL characteristics (a debug directory entry): winnt.h of newer SDKs
	constexpr DWORD ExCetCompat = 0x01;
	constexpr DWORD ExCetStrict = 0x02;
	constexpr DWORD ExCetContextIpRelaxed = 0x04;
	constexpr DWORD ExCetDynamicApisInProc = 0x08;
	constexpr DWORD ExForwardCfiCompat = 0x40;

	constexpr DWORD GuardCfInstrumented = 0x00000100;
	constexpr DWORD GuardCfwInstrumented = 0x00000200;
	constexpr DWORD GuardProtectDelayLoadIat = 0x00001000;
	constexpr DWORD GuardRetpolinePresent = 0x00100000;
	constexpr DWORD GuardEhContinuation = 0x00400000;
	constexpr DWORD GuardXfg = 0x00800000;

	std::wstring FileTimeText(FILETIME const& ft) {
		SYSTEMTIME st;
		if (!::FileTimeToSystemTime(&ft, &st))
			return {};
		return std::format(L"{}-{:02}-{:02} {:02}:{:02}:{:02} UTC", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	}

	// the name of a certificate's subject: its common name if it has one
	std::wstring CommonName(std::wstring const& subject) {
		auto at = subject.find(L"CN=");
		if (at == std::wstring::npos)
			return subject;
		auto end = subject.find(L", ", at);
		auto name = subject.substr(at + 3, end == std::wstring::npos ? std::wstring::npos : end - at - 3);
		if (name.size() > 1 && name.front() == L'"' && name.back() == L'"')
			name = name.substr(1, name.size() - 2);
		return name;
	}

	// the value of an attribute of the first element that has that attribute: name="value" or name='value'
	std::string Attribute(std::string_view xml, std::string_view element, std::string_view name) {
		auto at = xml.find(element);
		if (at == std::string_view::npos)
			return {};
		auto end = xml.find('>', at);
		auto tag = xml.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
		for (size_t pos = 0; (pos = tag.find(name, pos)) != std::string_view::npos; pos += name.size()) {
			// a whole attribute name
			if (pos > 0 && !isspace((unsigned char)tag[pos - 1]))
				continue;
			auto p = pos + name.size();
			while (p < tag.size() && isspace((unsigned char)tag[p]))
				p++;
			if (p >= tag.size() || tag[p] != '=')
				continue;
			p++;
			while (p < tag.size() && isspace((unsigned char)tag[p]))
				p++;
			if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\''))
				continue;
			auto quote = tag[p];
			auto close = tag.find(quote, p + 1);
			if (close == std::string_view::npos)
				return {};
			return std::string(tag.substr(p + 1, close - p - 1));
		}
		return {};
	}

	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	// the manifest as UTF-8 (it may be UTF-16)
	std::string ManifestText(std::span<const std::byte> data) {
		if (data.size() >= 2 && data[0] == std::byte{ 0xFF } && data[1] == std::byte{ 0xFE }) {
			std::wstring w((data.size() - 2) / 2, L'\0');
			memcpy(w.data(), data.data() + 2, w.size() * 2);
			int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
			std::string s(len, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
			return s;
		}
		return std::string((const char*)data.data(), data.size());
	}
}

// The catalog (of the system) that has the hash of a file: system files are signed that way, not in the file
static bool FindCatalog(std::wstring const& path, std::wstring& catalog) {
	wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr));
	if (!file)
		return false;
	// newer catalogs have SHA-256 hashes, older ones SHA-1
	for (auto algorithm : { BCRYPT_SHA256_ALGORITHM, (PCWSTR)nullptr }) {
		HCATADMIN admin = nullptr;
		if (!::CryptCATAdminAcquireContext2(&admin, nullptr, algorithm, nullptr, 0))
			continue;
		DWORD size = 0;
		::CryptCATAdminCalcHashFromFileHandle2(admin, file.get(), &size, nullptr, 0);
		std::vector<BYTE> hash(size);
		bool found = false;
		if (size && ::CryptCATAdminCalcHashFromFileHandle2(admin, file.get(), &size, hash.data(), 0)) {
			if (auto info = ::CryptCATAdminEnumCatalogFromHash(admin, hash.data(), size, 0, nullptr)) {
				CATALOG_INFO ci{ sizeof(ci) };
				if (::CryptCATCatalogInfoFromContext(info, &ci, 0)) {
					catalog = ci.wszCatalogFile;
					found = true;
				}
				::CryptCATAdminReleaseCatalogContext(admin, info, 0);
			}
		}
		::CryptCATAdminReleaseContext(admin, 0);
		if (found)
			return true;
	}
	return false;
}

const wchar_t* SecurityStateToString(SecurityState state) {
	switch (state) {
		case SecurityState::Enabled: return L"Yes";
		case SecurityState::Partial: return L"Partly";
		case SecurityState::Disabled: return L"No";
		case SecurityState::NotApplicable: return L"Not applicable";
		case SecurityState::Info: return L"";
	}
	return L"";
}

ManifestTrust ParseManifestTrust(std::string_view xml) {
	return { Widen(Attribute(xml, "requestedExecutionLevel", "level")), Widen(Attribute(xml, "requestedExecutionLevel", "uiAccess")) };
}

std::vector<SecurityFeature> AnalyzeSecurity(PEFile const& pe, bool verifyTrust) {
	std::vector<SecurityFeature> features;
	auto info = pe.GetFileInfo();
	auto nt = pe.GetNTHeader();
	if (!info || !nt)
		return features;
	bool is64 = info->IsPE64;
	auto machine = nt->NTHdr32.FileHeader.Machine;
	auto characteristics = nt->NTHdr32.FileHeader.Characteristics;
	auto dll = is64 ? nt->NTHdr64.OptionalHeader.DllCharacteristics : nt->NTHdr32.OptionalHeader.DllCharacteristics;
	bool x86 = machine == IMAGE_FILE_MACHINE_I386;
	bool intel = x86 || machine == IMAGE_FILE_MACHINE_AMD64;
	auto add = [&](std::wstring name, SecurityState state, std::wstring details) {
		features.push_back({ std::move(name), state, std::move(details) });
	};

	// a .NET file without native code: the protections of native code are the runtime's
	bool managed = false, ilOnly = false;
	IMAGE_COR20_HEADER cor{};
	if (auto dirs = pe.GetDataDirs(); dirs && dirs->size() > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR) {
		auto& dd = (*dirs)[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].DataDir;
		if (dd.VirtualAddress && dd.Size >= sizeof(cor))
			if (auto off = pe.GetOffsetFromRVA(dd.VirtualAddress); off && pe.Read((uint32_t)off, sizeof(cor), &cor)) {
				managed = true;
				ilOnly = (cor.Flags & COMIMAGE_FLAGS_ILONLY) != 0;
			}
	}

	// ASLR: the flag, and the relocations without which the loader cannot move the image
	bool relocs = !(characteristics & IMAGE_FILE_RELOCS_STRIPPED) && info->HasReloc;
	if (!(dll & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
		add(L"ASLR", SecurityState::Disabled, L"DYNAMIC_BASE is not set: the image is loaded at its preferred address");
	else if (!relocs)
		add(L"ASLR", SecurityState::Partial, L"DYNAMIC_BASE is set, but the file has no relocations: the image cannot be moved");
	else
		add(L"ASLR", SecurityState::Enabled, L"DYNAMIC_BASE, with relocations");

	if (!is64)
		add(L"High-entropy ASLR", SecurityState::NotApplicable, L"32-bit code has a 32-bit address space");
	else if (dll & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA)
		add(L"High-entropy ASLR", SecurityState::Enabled, L"HIGH_ENTROPY_VA: addresses anywhere in the 64-bit address space");
	else
		add(L"High-entropy ASLR", SecurityState::Disabled, L"HIGH_ENTROPY_VA is not set: the image stays below 4 GB");

	if (dll & IMAGE_DLLCHARACTERISTICS_NX_COMPAT)
		add(L"DEP", SecurityState::Enabled, L"NX_COMPAT: data is not executable");
	else if (is64)
		add(L"DEP", SecurityState::Enabled, L"NX_COMPAT is not set, but DEP is always on for 64-bit processes");
	else
		add(L"DEP", SecurityState::Disabled, L"NX_COMPAT is not set");

	// the load configuration: what the compiler and linker added
	auto lc = pe.GetLoadConfig();
	DWORD lcSize = 0;
	if (lc)
		lcSize = is64 ? lc->LCD64.Size : lc->LCD32.Size;
	// a field is there if the structure is as long as the file says
#define LC_FIELD(field) (lc && lcSize >= (is64 ? offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, field) + sizeof(lc->LCD64.field) : \
		offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, field) + sizeof(lc->LCD32.field)))
#define LC_VALUE(field) (is64 ? (uint64_t)lc->LCD64.field : (uint64_t)lc->LCD32.field)
	DWORD guardFlags = LC_FIELD(GuardFlags) ? (DWORD)LC_VALUE(GuardFlags) : 0;

	if (ilOnly)
		add(L"Control Flow Guard", SecurityState::NotApplicable, L"IL only: the code is compiled by the runtime");
	else if ((dll & IMAGE_DLLCHARACTERISTICS_GUARD_CF) && (guardFlags & GuardCfInstrumented)) {
		auto details = std::format(L"GUARD_CF, {} valid call targets", LC_FIELD(GuardCFFunctionCount) ? LC_VALUE(GuardCFFunctionCount) : 0);
		if (guardFlags & GuardCfwInstrumented)
			details += L", write integrity checks";
		if (guardFlags & GuardProtectDelayLoadIat)
			details += L", read-only delay-load IAT";
		add(L"Control Flow Guard", SecurityState::Enabled, details);
	}
	else if (dll & IMAGE_DLLCHARACTERISTICS_GUARD_CF)
		add(L"Control Flow Guard", SecurityState::Partial, L"GUARD_CF is set, but the load configuration says the code is not instrumented");
	else
		add(L"Control Flow Guard", SecurityState::Disabled, L"GUARD_CF is not set (/guard:cf)");

	if (!ilOnly && (dll & IMAGE_DLLCHARACTERISTICS_GUARD_CF))
		add(L"Extended Flow Guard (XFG)", (guardFlags & GuardXfg) ? SecurityState::Enabled : SecurityState::Disabled,
			(guardFlags & GuardXfg) ? L"Indirect calls check the type of their target" : L"/guard:xfg was not used");

	// CET shadow stacks: x86 and x64 only
	DWORD exFlags = 0;
	bool exFound = false;
	if (auto debug = pe.GetDebug()) {
		for (auto& d : *debug) {
			if (d.Directory.Type == IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS && d.Directory.SizeOfData >= 4 && d.Directory.PointerToRawData)
				exFound = pe.Read(d.Directory.PointerToRawData, 4, &exFlags);
		}
	}
	if (!intel)
		add(L"CET shadow stack", SecurityState::NotApplicable, L"CET is in x86 and x64 processors");
	else if (ilOnly)
		add(L"CET shadow stack", SecurityState::NotApplicable, L"IL only: the code is compiled by the runtime");
	else if (exFound && (exFlags & ExCetCompat)) {
		std::wstring details = L"CET_COMPAT (/CETCOMPAT): return addresses are checked against a shadow stack";
		if (exFlags & ExCetStrict)
			details += L", strict mode";
		if (exFlags & ExCetContextIpRelaxed)
			details += L", relaxed context IP validation";
		if (exFlags & ExCetDynamicApisInProc)
			details += L", dynamic APIs allowed in the process";
		add(L"CET shadow stack", SecurityState::Enabled, details);
	}
	else
		add(L"CET shadow stack", SecurityState::Disabled, L"The file is not marked CET compatible (/CETCOMPAT)");
	if (exFound && (exFlags & ExForwardCfiCompat))
		add(L"Forward CFI", SecurityState::Enabled, L"FORWARD_CFI_COMPAT: compatible with forward control-flow integrity");

	if (machine == IMAGE_FILE_MACHINE_AMD64 && !ilOnly)
		add(L"EH continuation targets", (guardFlags & GuardEhContinuation) ? SecurityState::Enabled : SecurityState::Disabled,
			(guardFlags & GuardEhContinuation) ? std::format(L"{} valid targets for exception handling to continue at (/guard:ehcont)",
				LC_FIELD(GuardEHContinuationCount) ? LC_VALUE(GuardEHContinuationCount) : 0) : std::wstring(L"/guard:ehcont was not used"));

	// SafeSEH: the exception handlers of 32-bit code
	if (!x86)
		add(L"SafeSEH", SecurityState::NotApplicable, L"Exception handlers are in tables in this kind of code");
	else if (ilOnly)
		add(L"SafeSEH", SecurityState::NotApplicable, L"IL only");
	else if (dll & IMAGE_DLLCHARACTERISTICS_NO_SEH)
		add(L"SafeSEH", SecurityState::Enabled, L"NO_SEH: the file has no structured exception handlers");
	else if (LC_FIELD(SEHandlerCount) && LC_VALUE(SEHandlerCount))
		add(L"SafeSEH", SecurityState::Enabled, std::format(L"{} registered exception handlers", LC_VALUE(SEHandlerCount)));
	else
		add(L"SafeSEH", SecurityState::Disabled, L"No table of exception handlers (/SAFESEH)");

	// /GS: the stack cookie that the code checks
	if (ilOnly)
		add(L"Stack cookies (/GS)", SecurityState::NotApplicable, L"IL only");
	else if (LC_FIELD(SecurityCookie) && LC_VALUE(SecurityCookie))
		add(L"Stack cookies (/GS)", SecurityState::Enabled, std::format(L"The cookie is at VA 0x{:X}", LC_VALUE(SecurityCookie)));
	else
		add(L"Stack cookies (/GS)", SecurityState::Disabled, lc ? L"The load configuration has no security cookie" : L"There is no load configuration");
	if (lc && guardFlags & GuardRetpolinePresent)
		add(L"Retpoline", SecurityState::Enabled, L"The file has the metadata of retpolines (Spectre variant 2)");
#undef LC_FIELD
#undef LC_VALUE

	// code that can be changed: a section that is both writable and executable
	std::wstring wx;
	if (auto sections = pe.GetSecHeaders()) {
		for (auto& s : *sections) {
			auto c = s.SecHdr.Characteristics;
			if ((c & IMAGE_SCN_MEM_WRITE) && (c & IMAGE_SCN_MEM_EXECUTE)) {
				if (!wx.empty())
					wx += L", ";
				wx += std::wstring(s.SectionName.begin(), s.SectionName.end());
			}
		}
	}
	add(L"No writable code", wx.empty() ? SecurityState::Enabled : SecurityState::Disabled,
		wx.empty() ? L"No section is both writable and executable" : L"Writable and executable: " + wx);

	// few files ask for it (drivers and protected processes): it is shown when it is set
	if (dll & IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY)
		add(L"Force Integrity", SecurityState::Enabled, L"FORCE_INTEGRITY: the signature is checked when the file is loaded");
	if (dll & IMAGE_DLLCHARACTERISTICS_APPCONTAINER)
		add(L"AppContainer", SecurityState::Info, L"APPCONTAINER: the image runs in an app container");

	// the signature
	std::wstring catalog;
	if (!info->HasSecurity && verifyTrust && FindCatalog(pe.GetPath(), catalog))
		add(L"Signature", SecurityState::Enabled, L"Signed in a catalog of the system: " + catalog);
	else if (!info->HasSecurity)
		add(L"Signature", SecurityState::Disabled, L"The file is not signed");
	else {
		auto auth = AnalyzeAuthenticode(pe, verifyTrust);
		SignatureSummary const* sig = auth.Signatures.empty() ? nullptr : &auth.Signatures[0];
		if (!sig || !sig->Decoded)
			add(L"Signature", SecurityState::Partial, sig ? L"The signature cannot be read: " + sig->Error : L"The certificate table has no signature");
		else {
			auto signer = sig->HasSigner ? CommonName(sig->Signer.Subject) : std::wstring(L"(unknown signer)");
			auto details = std::format(L"Signed by {} ({})", signer, sig->DigestAlgorithm);
			auto state = SecurityState::Enabled;
			if (!sig->ComputedDigest.empty() && !sig->DigestMatches()) {
				state = SecurityState::Disabled;
				details += L"; the file was changed after it was signed";
			}
			else if (!auth.Trust.empty()) {
				details += L"; " + auth.Trust;
				if (auth.TrustCode != 0)
					state = SecurityState::Partial;
			}
			add(L"Signature", state, details);
			if (sig->Timestamps.empty())
				add(L"Signature timestamp", SecurityState::Partial, L"Not timestamped: the signature is valid only while its certificate is");
			else {
				auto& ts = sig->Timestamps[0];
				add(L"Signature timestamp", SecurityState::Enabled,
					std::format(L"{} by {} ({})", FileTimeText(ts.Time), CommonName(ts.Signer), ts.Kind));
			}
		}
	}

	if (managed)
		add(L"Strong name", (cor.Flags & COMIMAGE_FLAGS_STRONGNAMESIGNED) ? SecurityState::Enabled : SecurityState::Disabled,
			(cor.Flags & COMIMAGE_FLAGS_STRONGNAMESIGNED) ? L"The assembly is strong-name signed" : L"The assembly is not strong-name signed");

	// what the manifest asks for when the program starts
	for (auto& res : pe.GetFlatResources()) {
		if (res.TypeID != 24)	// RT_MANIFEST
			continue;
		auto trust = ParseManifestTrust(ManifestText(res.Data));
		if (!trust.Level.empty())
			add(L"Execution level", SecurityState::Info, trust.Level + (trust.UiAccess == L"true" ? L", with UI access" : L""));
		break;
	}
	return features;
}
