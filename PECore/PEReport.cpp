#include "pch.h"
#include "PEReport.h"
#include <PEFile.h>
#include "Authenticode.h"
#include "ImportAnalysis.h"
#include "Overlay.h"
#include "PEAnomalies.h"
#include "Rtti.h"
#include "SecuritySummary.h"
#include "VersionInfo.h"
#include "PEDiff.h"
#include "YaraRules.h"
#include "Capabilities.h"
#include "PEStrings.h"
#include <bcrypt.h>

namespace {
	using Object = JsonValue::Object;
	using Array = JsonValue::Array;

	std::string Utf8(std::wstring_view w) {
		if (w.empty())
			return {};
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	// Names read from the file (sections, imports, exports, the PDB) can be any bytes: invalid UTF-8 would make invalid
	// JSON, so bytes that are not UTF-8 are written as "\xNN"
	std::string Text(std::string_view s) {
		if (s.empty() || ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0)
			return std::string(s);
		std::string result;
		for (unsigned char c : s)
			result += c < 0x80 ? std::string(1, (char)c) : std::format("\\x{:02X}", c);
		return result;
	}

	std::string Lower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	JsonValue Hex(uint64_t value) {
		return std::format("0x{:X}", value);
	}

	JsonValue Num(uint64_t value) {
		return (double)value;
	}

	// 2 decimals are plenty for entropy, and keep the output stable
	JsonValue Round2(double value) {
		return std::round(value * 100) / 100;
	}

	std::string FileTimeToIso(FILETIME const& ft) {
		SYSTEMTIME st;
		if ((ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) || !::FileTimeToSystemTime(&ft, &st))
			return {};
		return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	}

	std::string UnixTimeToIso(uint32_t secs) {
		auto ticks = (uint64_t)secs * 10000000ULL + 116444736000000000ULL;
		FILETIME ft{ (DWORD)ticks, (DWORD)(ticks >> 32) };
		return FileTimeToIso(ft);
	}

	JsonValue IsoOrNull(std::string s) {
		return s.empty() ? JsonValue() : JsonValue(std::move(s));
	}

	// "AMD64" of IMAGE_FILE_MACHINE_AMD64: the names of winnt.h without their prefix
	std::string Token(std::unordered_map<DWORD, std::wstring_view> const& map, DWORD value, std::wstring_view prefix) {
		if (auto it = map.find(value); it != map.end() && it->second.starts_with(prefix))
			return Utf8(it->second.substr(prefix.size()));
		return std::format("0x{:X}", value);
	}

	struct Flag {
		uint32_t Value;
		const char* Name;
	};

	JsonValue Flags(uint32_t value, std::span<const Flag> flags) {
		Array result;
		for (auto const& f : flags)
			if ((value & f.Value) == f.Value) {
				result.emplace_back(f.Name);
				value &= ~f.Value;
			}
		if (value)
			result.emplace_back(std::format("0x{:X}", value));
		return result;
	}

	constexpr Flag FileFlags[] = {
		{ IMAGE_FILE_RELOCS_STRIPPED, "RELOCS_STRIPPED" },
		{ IMAGE_FILE_EXECUTABLE_IMAGE, "EXECUTABLE_IMAGE" },
		{ IMAGE_FILE_LINE_NUMS_STRIPPED, "LINE_NUMS_STRIPPED" },
		{ IMAGE_FILE_LOCAL_SYMS_STRIPPED, "LOCAL_SYMS_STRIPPED" },
		{ IMAGE_FILE_AGGRESIVE_WS_TRIM, "AGGRESIVE_WS_TRIM" },
		{ IMAGE_FILE_LARGE_ADDRESS_AWARE, "LARGE_ADDRESS_AWARE" },
		{ IMAGE_FILE_BYTES_REVERSED_LO, "BYTES_REVERSED_LO" },
		{ IMAGE_FILE_32BIT_MACHINE, "32BIT_MACHINE" },
		{ IMAGE_FILE_DEBUG_STRIPPED, "DEBUG_STRIPPED" },
		{ IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP, "REMOVABLE_RUN_FROM_SWAP" },
		{ IMAGE_FILE_NET_RUN_FROM_SWAP, "NET_RUN_FROM_SWAP" },
		{ IMAGE_FILE_SYSTEM, "SYSTEM" },
		{ IMAGE_FILE_DLL, "DLL" },
		{ IMAGE_FILE_UP_SYSTEM_ONLY, "UP_SYSTEM_ONLY" },
		{ IMAGE_FILE_BYTES_REVERSED_HI, "BYTES_REVERSED_HI" },
	};

	constexpr Flag DllFlags[] = {
		{ IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA, "HIGH_ENTROPY_VA" },
		{ IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE, "DYNAMIC_BASE" },
		{ IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY, "FORCE_INTEGRITY" },
		{ IMAGE_DLLCHARACTERISTICS_NX_COMPAT, "NX_COMPAT" },
		{ IMAGE_DLLCHARACTERISTICS_NO_ISOLATION, "NO_ISOLATION" },
		{ IMAGE_DLLCHARACTERISTICS_NO_SEH, "NO_SEH" },
		{ IMAGE_DLLCHARACTERISTICS_NO_BIND, "NO_BIND" },
		{ IMAGE_DLLCHARACTERISTICS_APPCONTAINER, "APPCONTAINER" },
		{ IMAGE_DLLCHARACTERISTICS_WDM_DRIVER, "WDM_DRIVER" },
		{ IMAGE_DLLCHARACTERISTICS_GUARD_CF, "GUARD_CF" },
		{ IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE, "TERMINAL_SERVER_AWARE" },
	};

	constexpr Flag SectionFlags[] = {
		{ IMAGE_SCN_CNT_CODE, "CODE" },
		{ IMAGE_SCN_CNT_INITIALIZED_DATA, "INITIALIZED_DATA" },
		{ IMAGE_SCN_CNT_UNINITIALIZED_DATA, "UNINITIALIZED_DATA" },
		{ IMAGE_SCN_LNK_INFO, "LNK_INFO" },
		{ IMAGE_SCN_LNK_REMOVE, "LNK_REMOVE" },
		{ IMAGE_SCN_LNK_COMDAT, "LNK_COMDAT" },
		{ IMAGE_SCN_GPREL, "GPREL" },
		{ IMAGE_SCN_LNK_NRELOC_OVFL, "LNK_NRELOC_OVFL" },
		{ IMAGE_SCN_MEM_DISCARDABLE, "DISCARDABLE" },
		{ IMAGE_SCN_MEM_NOT_CACHED, "NOT_CACHED" },
		{ IMAGE_SCN_MEM_NOT_PAGED, "NOT_PAGED" },
		{ IMAGE_SCN_MEM_SHARED, "SHARED" },
		{ IMAGE_SCN_MEM_EXECUTE, "EXECUTE" },
		{ IMAGE_SCN_MEM_READ, "READ" },
		{ IMAGE_SCN_MEM_WRITE, "WRITE" },
	};

	const char* DebugTypeToken(uint32_t type) {
		switch (type) {
			case IMAGE_DEBUG_TYPE_COFF: return "COFF";
			case IMAGE_DEBUG_TYPE_CODEVIEW: return "CODEVIEW";
			case IMAGE_DEBUG_TYPE_FPO: return "FPO";
			case IMAGE_DEBUG_TYPE_MISC: return "MISC";
			case IMAGE_DEBUG_TYPE_EXCEPTION: return "EXCEPTION";
			case IMAGE_DEBUG_TYPE_FIXUP: return "FIXUP";
			case IMAGE_DEBUG_TYPE_OMAP_TO_SRC: return "OMAP_TO_SRC";
			case IMAGE_DEBUG_TYPE_OMAP_FROM_SRC: return "OMAP_FROM_SRC";
			case IMAGE_DEBUG_TYPE_BORLAND: return "BORLAND";
			case IMAGE_DEBUG_TYPE_CLSID: return "CLSID";
			case IMAGE_DEBUG_TYPE_VC_FEATURE: return "VC_FEATURE";
			case IMAGE_DEBUG_TYPE_POGO: return "POGO";
			case IMAGE_DEBUG_TYPE_ILTCG: return "ILTCG";
			case IMAGE_DEBUG_TYPE_MPX: return "MPX";
			case IMAGE_DEBUG_TYPE_REPRO: return "REPRO";
			case 17: return "EMBEDDED_PORTABLE_PDB";
			case 19: return "PDBCHECKSUM";
			case IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS: return "EX_DLLCHARACTERISTICS";
		}
		return nullptr;
	}

	const char* SeverityToken(AnomalySeverity severity) {
		switch (severity) {
			case AnomalySeverity::Info: return "info";
			case AnomalySeverity::Warning: return "warning";
			case AnomalySeverity::Error: return "error";
		}
		return "unknown";
	}

	const char* SecurityStateToken(SecurityState state) {
		switch (state) {
			case SecurityState::Enabled: return "enabled";
			case SecurityState::Partial: return "partial";
			case SecurityState::Disabled: return "disabled";
			case SecurityState::NotApplicable: return "notApplicable";
			case SecurityState::Info: return "info";
		}
		return "unknown";
	}

	bool IsPE64(PEFile const& pe) {
		return pe.GetFileInfo()->IsPE64;
	}

	template<typename F>
	auto Optional(PEFile const& pe, F&& f) {
		auto const& nt = *pe.GetNTHeader();
		return IsPE64(pe) ? f(nt.NTHdr64.OptionalHeader) : f(nt.NTHdr32.OptionalHeader);
	}

	IMAGE_DATA_DIRECTORY Directory(PEFile const& pe, int index) {
		return Optional(pe, [&](auto const& opt) {
			return index < (int)opt.NumberOfRvaAndSizes ? opt.DataDirectory[index] : IMAGE_DATA_DIRECTORY{};
		});
	}

	std::span<const std::byte> FileBytes(PEFile const& pe) {
		return pe.GetSpan(0, pe.GetFileSize());
	}

	std::string HexOf(std::vector<uint8_t> const& bytes) {
		return Lower(Utf8(BytesToHexString(bytes)));
	}

	// ── the parts ──────────────────────────────────────────────────────────

	JsonValue FilePart(PEFile const& pe) {
		auto bytes = FileBytes(pe);
		Object file{
			{ "path", Utf8(pe.GetPath()) },
			{ "size", Num(pe.GetFileSize()) },
			{ "md5", HexOf(HashBytes(BCRYPT_MD5_ALGORITHM, bytes)) },
			{ "sha1", Lower(Sha1Hex(bytes)) },
			{ "sha256", Lower(Sha256Hex(bytes)) },
		};
		auto imphash = pe.GetImport() ? ComputeImphash(pe) : std::string();
		file["imphash"] = imphash.empty() ? JsonValue() : JsonValue(imphash);
		return file;
	}

	bool IsDriver(PEFile const& pe, uint16_t subsystem, uint16_t dllChars) {
		if (subsystem != IMAGE_SUBSYSTEM_NATIVE)
			return false;
		if (dllChars & IMAGE_DLLCHARACTERISTICS_WDM_DRIVER)
			return true;
		if (auto imports = pe.GetImport())
			for (auto const& mod : *imports) {
				auto name = Lower(mod.ModuleName);
				if (name == "ntoskrnl.exe" || name == "hal.dll" || name == "wdfldr.sys" || name == "fltmgr.sys")
					return true;
			}
		return false;
	}

	JsonValue HeadersPart(PEFile const& pe) {
		auto const& fh = pe.GetNTHeader()->NTHdr64.FileHeader;	// the same place in both layouts
		Object h;
		h["format"] = IsPE64(pe) ? "PE32+" : "PE32";
		h["machine"] = Token(MapFileHdrMachine, fh.Machine, L"IMAGE_FILE_MACHINE_");
		// a reproducible build (/Brepro) has a hash of the build in the place of the time
		bool repro = false;
		if (auto debug = pe.GetDebug())
			repro = std::any_of(debug->begin(), debug->end(), [](auto const& d) { return d.Directory.Type == IMAGE_DEBUG_TYPE_REPRO; });
		h["reproducible"] = repro;
		h["timestamp"] = repro ? JsonValue() : IsoOrNull(UnixTimeToIso(fh.TimeDateStamp));
		h["timestampRaw"] = Num(fh.TimeDateStamp);
		h["characteristics"] = Flags(fh.Characteristics, FileFlags);
		h["sectionCount"] = Num(fh.NumberOfSections);

		Optional(pe, [&](auto const& opt) {
			h["imageBase"] = Hex(opt.ImageBase);
			h["entryPoint"] = Hex(opt.AddressOfEntryPoint);
			h["sizeOfImage"] = Num(opt.SizeOfImage);
			h["sizeOfHeaders"] = Num(opt.SizeOfHeaders);
			h["sectionAlignment"] = Hex(opt.SectionAlignment);
			h["fileAlignment"] = Hex(opt.FileAlignment);
			h["subsystem"] = Token(MapOptHdrSubsystem, opt.Subsystem, L"IMAGE_SUBSYSTEM_");
			h["subsystemVersion"] = std::format("{}.{}", opt.MajorSubsystemVersion, opt.MinorSubsystemVersion);
			h["osVersion"] = std::format("{}.{}", opt.MajorOperatingSystemVersion, opt.MinorOperatingSystemVersion);
			h["linkerVersion"] = std::format("{}.{}", opt.MajorLinkerVersion, opt.MinorLinkerVersion);
			h["dllCharacteristics"] = Flags(opt.DllCharacteristics, DllFlags);
			auto checksum = ComputeFileChecksum(pe);
			h["checksum"] = Object{
				{ "stored", Hex(opt.CheckSum) },
				{ "computed", Hex(checksum) },
				{ "valid", opt.CheckSum == checksum },
			};
			h["isDll"] = (fh.Characteristics & IMAGE_FILE_DLL) != 0;
			h["isDriver"] = IsDriver(pe, opt.Subsystem, opt.DllCharacteristics);
			return 0;
		});
		h["isManaged"] = Directory(pe, IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR).VirtualAddress != 0;
		return h;
	}

	JsonValue SectionsPart(PEFile const& pe) {
		Array sections;
		for (auto const& s : *pe.GetSecHeaders()) {
			auto const& hdr = s.SecHdr;
			uint64_t size = 0;
			if (hdr.PointerToRawData < pe.GetFileSize())
				size = std::min<uint64_t>(hdr.SizeOfRawData, pe.GetFileSize() - hdr.PointerToRawData);
			auto data = pe.GetSpan(hdr.PointerToRawData, (uint32_t)size);
			auto entropy = data.empty() ? 0.0 : ComputeEntropy((const uint8_t*)data.data(), data.size());
			sections.emplace_back(Object{
				{ "name", Text(s.SectionName) },
				{ "rva", Hex(hdr.VirtualAddress) },
				{ "virtualSize", Num(hdr.Misc.VirtualSize) },
				{ "rawOffset", Hex(hdr.PointerToRawData) },
				{ "rawSize", Num(hdr.SizeOfRawData) },
				{ "flags", Flags(hdr.Characteristics, SectionFlags) },
				{ "entropy", Round2(entropy) },
				{ "highEntropy", IsHighEntropySection(entropy, hdr.Characteristics, data.size()) },
			});
		}
		return sections;
	}

	JsonValue ImportEntry(std::string const& name, uint16_t hint, bool byOrdinal, uint16_t ordinal, std::string const& module,
		bool resolve, bool is64) {
		if (!byOrdinal)
			return Object{ { "name", Text(name) }, { "hint", Num(hint) } };
		Object entry{ { "ordinal", Num(ordinal) } };
		if (resolve)
			if (auto resolved = ResolveOrdinalName(Lower(module), ordinal, !is64); !resolved.empty())
				entry["resolvedName"] = Text(resolved);
		return entry;
	}

	JsonValue ImportsPart(PEFile const& pe, bool resolve) {
		const bool is64 = IsPE64(pe);
		Array modules;
		if (auto imports = pe.GetImport())
			for (auto const& mod : *imports) {
				Array functions;
				for (auto const& fn : mod.ImportFunc) {
					auto name = fn.FuncName.empty() ? std::string(fn.ImpByName.Name) : fn.FuncName;
					functions.push_back(ImportEntry(name, fn.ImpByName.Hint, IsOrdinalImport(fn, is64), ImportOrdinal(fn),
						mod.ModuleName, resolve, is64));
				}
				modules.emplace_back(Object{ { "module", Text(mod.ModuleName) }, { "delay", false }, { "functions", std::move(functions) } });
			}
		if (auto delay = pe.GetDelayImport())
			for (auto const& mod : *delay) {
				Array functions;
				for (auto const& fn : mod.DelayImpFunc) {
					auto name = fn.FuncName.empty() ? std::string(fn.ImpByName.Name) : fn.FuncName;
					uint64_t thunk = is64 ? fn.unThunk.st64.ImportNameTable.u1.Ordinal : fn.unThunk.st32.ImportNameTable.u1.Ordinal;
					bool byOrdinal = name.empty() && (is64 ? (thunk & IMAGE_ORDINAL_FLAG64) : (thunk & IMAGE_ORDINAL_FLAG32)) != 0;
					functions.push_back(ImportEntry(name, fn.ImpByName.Hint, byOrdinal, (uint16_t)(thunk & 0xFFFF), mod.ModuleName, resolve, is64));
				}
				modules.emplace_back(Object{ { "module", Text(mod.ModuleName) }, { "delay", true }, { "functions", std::move(functions) } });
			}
		return modules;
	}

	JsonValue ExportsPart(PEFile const& pe, size_t max) {
		auto exports = pe.GetExport();
		if (!exports || Directory(pe, IMAGE_DIRECTORY_ENTRY_EXPORT).VirtualAddress == 0)
			return {};
		Array functions;
		for (auto const& fn : exports->Funcs) {
			if (functions.size() == max)
				break;
			Object entry{ { "ordinal", Num(fn.Ordinal) } };
			entry["name"] = fn.FuncName.empty() ? JsonValue() : JsonValue(Text(fn.FuncName));
			if (fn.ForwarderName.empty())
				entry["rva"] = Hex(fn.FuncRVA);
			else
				entry["forwarder"] = Text(fn.ForwarderName);
			functions.push_back(std::move(entry));
		}
		return Object{
			{ "dllName", Text(exports->ModuleName) },
			{ "count", Num(exports->Funcs.size()) },
			{ "truncated", functions.size() < exports->Funcs.size() },
			{ "functions", std::move(functions) },
		};
	}

	struct RsdsHeader {
		DWORD Signature;	// 'RSDS'
		GUID Guid;
		DWORD Age;
	};

	JsonValue DebugPart(PEFile const& pe) {
		Array entries;
		for (auto const& d : *pe.GetDebug()) {
			auto const& dir = d.Directory;
			Object entry;
			auto type = DebugTypeToken(dir.Type);
			entry["type"] = type ? JsonValue(type) : JsonValue(std::format("{}", dir.Type));
			entry["size"] = Num(dir.SizeOfData);
			entry["rawOffset"] = Hex(dir.PointerToRawData);
			if (dir.Type == IMAGE_DEBUG_TYPE_CODEVIEW && dir.SizeOfData > sizeof(RsdsHeader)) {
				auto data = pe.GetSpan(dir.PointerToRawData, dir.SizeOfData);
				if (data.size() > sizeof(RsdsHeader)) {
					RsdsHeader rsds;
					memcpy(&rsds, data.data(), sizeof(rsds));
					if (rsds.Signature == 'SDSR') {
						auto name = (const char*)data.data() + sizeof(rsds);
						std::string path(name, strnlen(name, data.size() - sizeof(rsds)));
						entry["pdb"] = Text(path);
						entry["guid"] = Utf8(PEStrings::GuidToString(rsds.Guid));
						entry["age"] = Num(rsds.Age);
					}
				}
			}
			entries.push_back(std::move(entry));
		}
		return entries;
	}

	JsonValue RichPart(PEFile const& pe) {
		auto rich = pe.GetRichHeader();
		if (!rich || !pe.GetFileInfo()->HasRichHdr)
			return {};
		Array entries;
		for (auto const& e : rich->Entries)
			entries.emplace_back(Object{ { "productId", Num(e.ProductId) }, { "build", Num(e.BuildId) }, { "count", Num(e.Count) } });
		return Object{
			{ "key", Hex(rich->Key) },
			{ "keyValid", ComputeRichKey(pe.GetData(), *rich) == rich->Key },
			{ "entries", std::move(entries) },
		};
	}

	JsonValue TlsPart(PEFile const& pe) {
		if (!pe.GetFileInfo()->HasTLS)
			return {};
		Array callbacks;
		for (auto cb : pe.GetTLS()->TLSCallbacks)
			callbacks.push_back(Hex(cb));
		return Object{ { "callbacks", std::move(callbacks) } };
	}

	std::string VersionString(VS_FIXEDFILEINFO const& f, bool product) {
		auto ms = product ? f.dwProductVersionMS : f.dwFileVersionMS, ls = product ? f.dwProductVersionLS : f.dwFileVersionLS;
		return std::format("{}.{}.{}.{}", HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls));
	}

	JsonValue VersionPart(PEFile const& pe) {
		for (auto const& res : pe.GetFlatResources()) {
			if (!res.TypeStr.empty() || res.TypeID != (WORD)(ULONG_PTR)RT_VERSION)
				continue;
			auto info = ParseVersionInfo(res.Data);
			if (!info.Valid)
				continue;
			Object version;
			if (info.HasFixedInfo)
				version["fixed"] = Object{
					{ "fileVersion", VersionString(info.Fixed, false) },
					{ "productVersion", VersionString(info.Fixed, true) },
				};
			Object strings;
			if (!info.Tables.empty())
				for (auto const& [name, value] : info.Tables[0].Strings)
					strings[Utf8(name)] = Utf8(value);
			version["strings"] = std::move(strings);
			if (!info.Tables.empty())
				version["language"] = Utf8(info.Tables[0].LangCodePage);
			return version;
		}
		return {};
	}

	JsonValue Certificate(CertificateSummary const& c) {
		return Object{
			{ "subject", Utf8(c.Subject) },
			{ "issuer", Utf8(c.Issuer) },
			{ "serialNumber", Utf8(c.SerialNumber) },
			{ "thumbprint", Utf8(c.Thumbprint) },
			{ "notBefore", IsoOrNull(FileTimeToIso(c.NotBefore)) },
			{ "notAfter", IsoOrNull(FileTimeToIso(c.NotAfter)) },
		};
	}

	JsonValue Signature(SignatureSummary const& sig) {
		Object result;
		result["decoded"] = sig.Decoded;
		if (!sig.Error.empty())
			result["error"] = Utf8(sig.Error);
		result["digestAlgorithm"] = Utf8(sig.DigestAlgorithm);
		result["digestMatches"] = sig.DigestMatches();
		if (sig.HasSigner)
			result["signer"] = Certificate(sig.Signer);
		Array timestamps;
		for (auto const& t : sig.Timestamps)
			timestamps.emplace_back(Object{ { "kind", Utf8(t.Kind) }, { "time", IsoOrNull(FileTimeToIso(t.Time)) }, { "signer", Utf8(t.Signer) } });
		result["timestamps"] = std::move(timestamps);
		if (!sig.Nested.empty()) {
			Array nested;
			for (auto const& n : sig.Nested)
				nested.push_back(Signature(n));
			result["nested"] = std::move(nested);
		}
		return result;
	}

	// The catalog of the system that signs a file that has no signature of its own: looked for once, for the signature
	// and the security parts
	class CatalogLookup {
	public:
		CatalogLookup(PEFile const& pe, ReportOptions const& options) : m_PE(pe), m_Options(options) {}

		CatalogSignature const* Get() {
			if (!m_Looked) {
				m_Looked = true;
				auto info = m_PE.GetFileInfo();
				m_Found = m_Options.CheckCatalogs && !info->HasSecurity && !m_PE.GetPath().empty() &&
					FindCatalogSignature(m_PE.GetPath(), m_Catalog, m_Options.VerifyTrust);
			}
			return m_Found ? &m_Catalog : nullptr;
		}

	private:
		PEFile const& m_PE;
		ReportOptions const& m_Options;
		CatalogSignature m_Catalog;
		bool m_Looked{ false }, m_Found{ false };
	};

	void AddTrust(Object& part, long code, std::wstring const& message) {
		part["trust"] = Utf8(message);
		part["trustCode"] = std::format("0x{:08X}", (uint32_t)code);
		part["trusted"] = code == 0;
	}

	JsonValue SignaturePart(PEFile const& pe, bool verifyTrust, CatalogLookup& catalogs) {
		auto result = AnalyzeAuthenticode(pe, verifyTrust);
		Array signatures;
		for (auto const& sig : result.Signatures)
			signatures.push_back(Signature(sig));
		auto catalog = result.Signatures.empty() ? catalogs.Get() : nullptr;
		const char* kind = !result.Signatures.empty() ? "embedded" : catalog ? "catalog" : "none";
		Object part{ { "kind", kind }, { "present", kind != std::string_view("none") }, { "signatures", std::move(signatures) } };

		if (catalog) {
			Object c{
				{ "path", Utf8(catalog->Catalog) },
				{ "hashAlgorithm", Utf8(catalog->HashAlgorithm) },
				{ "memberTag", Lower(Utf8(catalog->MemberTag)) },
				{ "signature", Signature(catalog->Signature) },
			};
			part["catalog"] = std::move(c);
		}
		if (verifyTrust) {
			if (catalog)
				AddTrust(part, catalog->TrustCode, catalog->Trust);
			else if (result.Signatures.empty())
				AddTrust(part, (long)TRUST_E_NOSIGNATURE, TrustMessage(TRUST_E_NOSIGNATURE));
			else
				AddTrust(part, result.TrustCode, result.Trust);
		}
		return part;
	}

	JsonValue SecurityPart(PEFile const& pe, bool verifyTrust, CatalogLookup& catalogs) {
		// without a signature of its own, verifying the trust of a file is looking for its catalog: done here once
		auto embedded = pe.GetFileInfo()->HasSecurity;
		auto catalog = embedded ? nullptr : catalogs.Get();
		Array features;
		for (auto const& f : AnalyzeSecurity(pe, verifyTrust && embedded)) {
			auto state = f.State;
			auto details = f.Details;
			if (catalog && f.Name == L"Signature") {
				state = !verifyTrust || catalog->TrustCode == 0 ? SecurityState::Enabled : SecurityState::Partial;
				details = L"Signed in a catalog of the system: " + catalog->Catalog;
				if (verifyTrust)
					details += L"; " + catalog->Trust;
			}
			features.emplace_back(Object{ { "name", Utf8(f.Name) }, { "state", SecurityStateToken(state) }, { "details", Utf8(details) } });
		}
		return features;
	}

	JsonValue OverlayPart(PEFile const& pe) {
		auto overlay = FindOverlay(pe);
		return Object{
			{ "offset", Hex(overlay.Offset) },
			{ "size", Num(overlay.Size) },
			{ "kind", overlay.Kind.empty() ? JsonValue() : JsonValue(Utf8(overlay.Kind)) },
			{ "entropy", Round2(overlay.Entropy) },
		};
	}

	JsonValue AnomaliesPart(PEFile const& pe) {
		Array anomalies;
		for (auto const& a : FindAnomalies(pe))
			anomalies.emplace_back(Object{
				{ "severity", SeverityToken(a.Severity) },
				{ "category", Utf8(a.Category) },
				{ "message", Utf8(a.Message) },
				{ "offset", a.FileOffset < 0 ? JsonValue() : Hex(a.FileOffset) },
			});
		return anomalies;
	}

	JsonValue RttiPart(PEFile const& pe) {
		Array classes;
		for (auto const& c : ReadRtti(pe)) {
			Array bases;
			for (auto const& b : c.Bases)
				bases.push_back(Text(b));
			classes.emplace_back(Object{
				{ "name", Text(c.Name) },
				{ "decorated", Text(c.Decorated) },
				{ "struct", c.Struct },
				{ "bases", std::move(bases) },
				{ "vtables", Num(c.Vtables.size()) },
			});
		}
		return classes;
	}

	// where a file offset is: the section and the RVA (null if it is not in the raw data of a section)
	std::pair<JsonValue, JsonValue> SectionOf(PEFile const& pe, uint64_t offset) {
		if (auto sections = pe.GetSecHeaders())
			for (auto const& s : *sections) {
				auto const& h = s.SecHdr;
				if (h.SizeOfRawData && offset >= h.PointerToRawData && offset < (uint64_t)h.PointerToRawData + h.SizeOfRawData)
					return { Text(s.SectionName), Hex(h.VirtualAddress + (offset - h.PointerToRawData)) };
			}
		return {};
	}

	JsonValue YaraPart(PEFile const& pe, YaraRules const& rules, int timeout, Array& errors) {
		Object part{ { "rules", Utf8(rules.Source()) }, { "ruleCount", Num(rules.RuleCount()) } };
		std::vector<YaraMatch> matches;
		std::string error;
		if (!rules.Scan(FileBytes(pe), pe.GetPath(), matches, &error, timeout)) {
			part["error"] = error;
			errors.emplace_back("yara: " + error);
		}
		Array list;
		for (auto const& m : matches) {
			Array tags;
			for (auto const& t : m.Tags)
				tags.emplace_back(Text(t));
			Object meta;
			for (auto const& [name, value] : m.Meta)
				if (!meta.contains(Text(name)))
					meta[Text(name)] = Text(value);
			Array strings;
			for (auto const& s : m.Strings) {
				auto [section, rva] = SectionOf(pe, s.Offset);
				std::string data;
				for (auto b : s.Data)
					data += std::format("{:02x}", b);
				strings.emplace_back(Object{
					{ "id", Text(s.Identifier) },
					{ "offset", Hex(s.Offset) },
					{ "section", section },
					{ "rva", rva },
					{ "length", Num(s.Length) },
					{ "data", data },
				});
			}
			Object entry{
				{ "rule", Text(m.Rule) },
				{ "namespace", Text(m.Namespace) },
				{ "tags", std::move(tags) },
				{ "meta", std::move(meta) },
				{ "strings", std::move(strings) },
				{ "stringMatches", Num(m.StringMatchCount) },
			};
			if (m.TooManyMatches)
				entry["tooManyMatches"] = true;
			list.push_back(std::move(entry));
		}
		part["matches"] = std::move(list);
		return part;
	}

	struct PartName {
		ReportPart Part;
		const char* Name;
	};

	constexpr PartName PartNames[] = {
		{ ReportPart::File, "file" }, { ReportPart::Headers, "headers" }, { ReportPart::Sections, "sections" },
		{ ReportPart::Imports, "imports" }, { ReportPart::Exports, "exports" }, { ReportPart::Debug, "debug" },
		{ ReportPart::Rich, "rich" }, { ReportPart::Tls, "tls" }, { ReportPart::Version, "version" },
		{ ReportPart::Signature, "signature" }, { ReportPart::Security, "security" }, { ReportPart::Overlay, "overlay" },
		{ ReportPart::Anomalies, "anomalies" }, { ReportPart::Rtti, "rtti" },
		{ ReportPart::Capabilities, "capabilities" },
	};
}

JsonValue BuildReport(PEFile const& pe, ReportOptions const& options) {
	Object report{ { "schema", ReportSchema } };
	Array errors;
	if (!pe || !pe.GetNTHeader() || !pe.GetFileInfo() || !(pe.GetFileInfo()->IsPE32 || pe.GetFileInfo()->IsPE64)) {
		report["file"] = Object{ { "path", Utf8(pe.GetPath()) } };
		report["error"] = "not a PE file";
		return report;
	}

	auto add = [&](ReportPart part, const char* name, auto&& build) {
		if ((options.Parts & part) == ReportPart::None)
			return;
		try {
			JsonValue value = build();
			if (!value.IsNull())
				report[name] = std::move(value);
		}
		catch (std::exception const& ex) {
			errors.emplace_back(std::format("{}: {}", name, ex.what()));
		}
		catch (...) {
			errors.emplace_back(std::format("{}: failed", name));
		}
	};

	add(ReportPart::File, "file", [&] { return FilePart(pe); });
	if ((options.Parts & ReportPart::File) == ReportPart::None)
		report["file"] = Object{ { "path", Utf8(pe.GetPath()) } };
	add(ReportPart::Headers, "headers", [&] { return HeadersPart(pe); });
	add(ReportPart::Sections, "sections", [&] { return pe.GetSecHeaders() ? SectionsPart(pe) : JsonValue(); });
	add(ReportPart::Imports, "imports", [&] { return ImportsPart(pe, options.ResolveOrdinals); });
	add(ReportPart::Exports, "exports", [&] { return ExportsPart(pe, options.MaxExports); });
	add(ReportPart::Debug, "debug", [&] { return pe.GetDebug() ? DebugPart(pe) : JsonValue(); });
	add(ReportPart::Rich, "rich", [&] { return RichPart(pe); });
	add(ReportPart::Tls, "tls", [&] { return TlsPart(pe); });
	add(ReportPart::Version, "version", [&] { return VersionPart(pe); });
	CatalogLookup catalogs(pe, options);
	add(ReportPart::Signature, "signature", [&] { return SignaturePart(pe, options.VerifyTrust, catalogs); });
	add(ReportPart::Security, "security", [&] { return SecurityPart(pe, options.VerifyTrust, catalogs); });
	add(ReportPart::Overlay, "overlay", [&] { return OverlayPart(pe); });
	add(ReportPart::Anomalies, "anomalies", [&] { return AnomaliesPart(pe); });
	add(ReportPart::Rtti, "rtti", [&] { return RttiPart(pe); });
	add(ReportPart::Capabilities, "capabilities", [&] {
		Array list;
		auto const& rules = options.Capabilities ? *options.Capabilities : CapabilityRules::BuiltIn();
		for (auto const& m : rules.Match(pe)) {
			Array attack, evidence;
			for (auto const& a : m.Attack)
				attack.emplace_back(a);
			for (auto const& e : m.Evidence)
				evidence.emplace_back(Text(e));
			Object entry{ { "name", m.Name }, { "namespace", m.Namespace }, { "attack", std::move(attack) }, { "evidence", std::move(evidence) } };
			if (!m.Description.empty())
				entry["description"] = m.Description;
			list.push_back(std::move(entry));
		}
		return JsonValue(std::move(list));
	});
	if (options.Yara)
		add(ReportPart::All, "yara", [&] { return YaraPart(pe, *options.Yara, options.YaraTimeout, errors); });

	report["errors"] = std::move(errors);
	return report;
}

JsonValue BuildErrorReport(std::wstring const& path, std::string const& error) {
	return Object{
		{ "schema", ReportSchema },
		{ "file", Object{ { "path", Utf8(path) } } },
		{ "error", error },
	};
}

std::vector<std::string> const& ReportSummaryColumns() {
	static const std::vector<std::string> columns{
		"path", "sha256", "machine", "subsystem", "isDll", "isDriver", "timestamp", "company", "fileVersion",
		"signed", "signatureKind", "signer", "trusted", "aslr", "dep", "cfg", "cet", "maxEntropy", "overlaySize", "anomalyErrors", "anomalyWarnings",
		"imphash", "pdb", "capabilities", "yara", "error",
	};
	return columns;
}

JsonValue SummarizeReport(JsonValue const& report) {
	Object row;
	for (auto const& c : ReportSummaryColumns())
		row[c] = JsonValue();

	auto copy = [&](const char* column, JsonValue const& value) {
		if (!value.IsNull())
			row[column] = value;
	};
	auto const& file = report["file"];
	copy("path", file["path"]);
	copy("sha256", file["sha256"]);
	copy("imphash", file["imphash"]);
	copy("error", report["error"]);

	auto const& h = report["headers"];
	copy("machine", h["machine"]);
	copy("subsystem", h["subsystem"]);
	copy("isDll", h["isDll"]);
	copy("isDriver", h["isDriver"]);
	copy("timestamp", h["timestamp"]);

	auto const& strings = report["version"]["strings"];
	copy("company", strings["CompanyName"]);
	copy("fileVersion", report["version"]["fixed"]["fileVersion"]);
	if (row["fileVersion"].IsNull())
		copy("fileVersion", strings["FileVersion"]);

	auto const& sig = report["signature"];
	copy("signed", sig["present"]);
	copy("signatureKind", sig["kind"]);
	copy("trusted", sig["trusted"]);
	auto const& signer = sig["kind"].AsString() == "catalog" ? sig["catalog"]["signature"]["signer"] : sig["signatures"].AsArray().empty()
		? JsonValue() : sig["signatures"].AsArray()[0]["signer"];
	copy("signer", signer["subject"]);

	static const std::pair<const char*, const char*> features[] = {
		{ "aslr", "ASLR" }, { "dep", "DEP" }, { "cfg", "Control Flow Guard" }, { "cet", "CET shadow stack" },
	};
	for (auto const& f : report["security"].AsArray())
		for (auto const& [column, name] : features)
			if (f["name"].AsString() == name)
				row[column] = f["state"];

	if (report["sections"].IsArray()) {
		double max = 0;
		for (auto const& s : report["sections"].AsArray())
			max = std::max(max, s["entropy"].AsNumber());
		row["maxEntropy"] = max;
	}
	copy("overlaySize", report["overlay"]["size"]);
	if (report["capabilities"].IsArray()) {
		std::string names;
		for (auto const& c : report["capabilities"].AsArray())
			names += (names.empty() ? "" : ";") + c["name"].AsString();
		row["capabilities"] = names;
	}
	if (report["yara"].IsObject()) {
		std::string rules;
		for (auto const& m : report["yara"]["matches"].AsArray())
			rules += (rules.empty() ? "" : ";") + m["rule"].AsString();
		row["yara"] = rules;
	}

	if (report["anomalies"].IsArray()) {
		int errors = 0, warnings = 0;
		for (auto const& a : report["anomalies"].AsArray()) {
			auto severity = a["severity"].AsString();
			errors += severity == "error";
			warnings += severity == "warning";
		}
		row["anomalyErrors"] = errors;
		row["anomalyWarnings"] = warnings;
	}

	for (auto const& d : report["debug"].AsArray())
		if (d["pdb"].IsString()) {
			row["pdb"] = d["pdb"];
			break;
		}
	return row;
}

bool ParseReportParts(std::string_view text, ReportPart& parts, std::string* unknown) {
	ReportPart result = ReportPart::None;
	while (!text.empty()) {
		auto comma = text.find(',');
		auto name = text.substr(0, comma);
		text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
		while (!name.empty() && name.front() == ' ')
			name.remove_prefix(1);
		while (!name.empty() && name.back() == ' ')
			name.remove_suffix(1);
		if (name.empty())
			continue;
		auto lower = Lower(std::string(name));
		if (lower == "all") {
			result |= ReportPart::All;
			continue;
		}
		if (lower == "default") {
			result |= ReportPart::Default;
			continue;
		}
		auto it = std::find_if(std::begin(PartNames), std::end(PartNames), [&](auto const& p) { return lower == p.Name; });
		if (it == std::end(PartNames)) {
			if (unknown)
				*unknown = std::string(name);
			return false;
		}
		result |= it->Part;
	}
	parts = result;
	return true;
}

//
// Differences
//

namespace {
	const char* DiffStatusToken(DiffStatus status) {
		switch (status) {
			case DiffStatus::Same: return "same";
			case DiffStatus::Changed: return "changed";
			case DiffStatus::Added: return "added";
			case DiffStatus::Removed: return "removed";
		}
		return "unknown";
	}

	const char* DiffCategoryToken(DiffCategory category) {
		switch (category) {
			case DiffCategory::File: return "file";
			case DiffCategory::Header: return "header";
			case DiffCategory::Directory: return "directory";
			case DiffCategory::Section: return "section";
			case DiffCategory::Import: return "import";
			case DiffCategory::Export: return "export";
			case DiffCategory::Resource: return "resource";
			case DiffCategory::Version: return "version";
			case DiffCategory::Debug: return "debug";
		}
		return "unknown";
	}

	JsonValue DiffSide(PEFile const& pe) {
		return Object{
			{ "path", Utf8(pe.GetPath()) },
			{ "size", Num(pe.GetFileSize()) },
			{ "sha256", Lower(Sha256Hex(FileBytes(pe))) },
		};
	}
}

JsonValue BuildDiffReport(PEFile const& left, PEFile const& right, bool includeSame) {
	auto diff = ComparePEFiles(left, right);
	Array items;
	for (auto const& item : diff.Items) {
		if (item.Status == DiffStatus::Same && !includeSame)
			continue;
		Object entry{
			{ "category", DiffCategoryToken(item.Category) },
			{ "name", Utf8(item.Name) },
			{ "status", DiffStatusToken(item.Status) },
		};
		if (!item.Left.empty())
			entry["left"] = Utf8(item.Left);
		if (!item.Right.empty())
			entry["right"] = Utf8(item.Right);
		if (!item.Details.empty())
			entry["details"] = Utf8(item.Details);
		items.push_back(std::move(entry));
	}
	auto changed = diff.Count(DiffStatus::Changed), added = diff.Count(DiffStatus::Added), removed = diff.Count(DiffStatus::Removed);
	return Object{
		{ "schema", DiffSchema },
		{ "left", DiffSide(left) },
		{ "right", DiffSide(right) },
		{ "status", diff.Identical ? "identical" : "changed" },
		{ "counts", Object{ { "changed", Num(changed) }, { "added", Num(added) }, { "removed", Num(removed) } } },
		{ "items", std::move(items) },
	};
}

JsonValue BuildOneSidedDiffReport(std::wstring const& path, bool added) {
	Object side{ { "path", Utf8(path) } };
	return Object{
		{ "schema", DiffSchema },
		{ "left", added ? JsonValue() : JsonValue(side) },
		{ "right", added ? JsonValue(side) : JsonValue() },
		{ "status", added ? "added" : "removed" },
	};
}

//
// Requirements
//

std::vector<std::pair<std::string, std::string>> const& RequirementNames() {
	static const std::vector<std::pair<std::string, std::string>> names{
		{ "aslr", "ASLR" },
		{ "high-entropy-aslr", "High-entropy ASLR" },
		{ "dep", "DEP" },
		{ "cfg", "Control Flow Guard" },
		{ "xfg", "Extended Flow Guard (XFG)" },
		{ "cet", "CET shadow stack" },
		{ "ehcont", "EH continuation targets" },
		{ "gs", "Stack cookies (/GS)" },
		{ "safeseh", "SafeSEH" },
		{ "no-wx", "No writable code" },
		{ "retpoline", "Retpoline" },
		{ "force-integrity", "Force Integrity" },
		{ "signed", "a signature, in the file or in a catalog of the system" },
		{ "trusted", "a signature that the system trusts" },
	};
	return names;
}

bool ParseRequirements(std::string_view text, std::vector<std::string>& requirements, std::string* unknown) {
	std::vector<std::string> result;
	auto add = [&](std::string const& token) {
		if (std::find(result.begin(), result.end(), token) == result.end())
			result.push_back(token);
	};
	while (!text.empty()) {
		auto comma = text.find(',');
		auto name = text.substr(0, comma);
		text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
		while (!name.empty() && name.front() == ' ')
			name.remove_prefix(1);
		while (!name.empty() && name.back() == ' ')
			name.remove_suffix(1);
		if (name.empty())
			continue;
		auto token = Lower(std::string(name));
		if (token == "default") {
			std::vector<std::string> defaults;
			ParseRequirements(DefaultRequirements, defaults);
			for (auto const& d : defaults)
				add(d);
			continue;
		}
		auto const& names = RequirementNames();
		if (std::find_if(names.begin(), names.end(), [&](auto const& n) { return n.first == token; }) == names.end()) {
			if (unknown)
				*unknown = std::string(name);
			return false;
		}
		add(token);
	}
	requirements = std::move(result);
	return true;
}

std::vector<std::string> CheckReport(JsonValue const& report, std::vector<std::string> const& requirements, std::string_view failOn) {
	std::vector<std::string> failures;
	if (report["error"].IsString()) {
		failures.push_back("not analyzed: " + report["error"].AsString());
		return failures;
	}

	auto const& names = RequirementNames();
	auto const& sig = report["signature"];
	for (auto const& token : requirements) {
		if (token == "signed") {
			if (!sig.IsObject())
				failures.push_back("signed: the report has no signature part");
			else if (!sig["present"].AsBool())
				failures.push_back("signed: the file is not signed");
			continue;
		}
		if (token == "trusted") {
			if (!sig["trusted"].IsBool())
				failures.push_back("trusted: the trust of the signature was not verified");
			else if (!sig["trusted"].AsBool())
				failures.push_back("trusted: " + sig["trust"].AsString());
			continue;
		}

		auto it = std::find_if(names.begin(), names.end(), [&](auto const& n) { return n.first == token; });
		if (it == names.end())
			continue;
		if (!report["security"].IsArray()) {
			failures.push_back(token + ": the report has no security part");
			continue;
		}
		JsonValue const* feature = nullptr;
		for (auto const& f : report["security"].AsArray())
			if (f["name"].AsString() == it->second) {
				feature = &f;
				break;
			}
		// features that are shown only when they are set (Force Integrity) are missing when they are not
		auto state = feature ? (*feature)["state"].AsString() : std::string("disabled");
		if (state == "enabled" || state == "notApplicable" || state == "info")
			continue;
		auto details = feature ? (*feature)["details"].AsString() : std::string();
		failures.push_back(std::format("{} ({}): {}{}", token, it->second, state, details.empty() ? "" : " - " + details));
	}

	if (!failOn.empty()) {
		auto rank = [](std::string_view severity) { return severity == "error" ? 3 : severity == "warning" ? 2 : severity == "info" ? 1 : 0; };
		auto threshold = rank(failOn);
		int counts[4]{};
		for (auto const& a : report["anomalies"].AsArray()) {
			auto r = rank(a["severity"].AsString());
			if (threshold && r >= threshold)
				counts[r]++;
		}
		std::string text;
		for (int r = 3; r >= 1; r--)
			if (counts[r])
				text += std::format("{}{} {}{}", text.empty() ? "" : ", ", counts[r], r == 3 ? "error" : r == 2 ? "warning" : "info",
					counts[r] > 1 && r != 1 ? "s" : "");
		if (!text.empty())
			failures.push_back("anomalies: " + text);
	}

	if (report["yara"].IsObject()) {
		std::string rules;
		for (auto const& m : report["yara"]["matches"].AsArray())
			rules += (rules.empty() ? "" : ", ") + m["rule"].AsString();
		if (!rules.empty())
			failures.push_back("yara: " + rules);
		if (report["yara"]["error"].IsString())
			failures.push_back("yara: " + report["yara"]["error"].AsString());
	}
	return failures;
}
