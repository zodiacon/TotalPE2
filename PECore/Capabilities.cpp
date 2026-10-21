#include "pch.h"
#include "Capabilities.h"
#include <PEFile.h>
#include "Json.h"
#include "FileStrings.h"
#include "ImportAnalysis.h"
#include "Overlay.h"
#include "PEAnomalies.h"
#include "ApiSetMap.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {
	std::string Lower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	std::string Utf8(std::wstring_view w) {
		if (w.empty())
			return {};
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	// "kernel32.dll" -> "kernel32"
	std::string ModuleBase(std::string module) {
		module = Lower(std::move(module));
		for (auto ext : { ".dll", ".exe", ".sys", ".drv", ".ocx" })
			if (module.ends_with(ext))
				return module.substr(0, module.size() - strlen(ext));
		return module;
	}

	// What the rules look at, gathered from the file once
	struct Features {
		struct Import {
			std::string Module;		// lowercase, without extension
			std::string Host;		// for an API set, the DLL that implements it on this system (else the module)
			std::string Name;		// as imported
			std::string Lower;
		};
		std::vector<Import> Imports;
		std::set<std::string> Modules;		// lowercase, without extension
		std::set<std::string> Exports;		// lowercase
		std::set<std::string> Sections;		// lowercase
		std::vector<std::string> Strings;	// as found
		std::vector<std::string> LowerStrings;
		std::set<std::string> Characteristics;
	};

	Features Gather(PEFile const& pe) {
		Features f;
		auto info = pe.GetFileInfo();
		bool is64 = info->IsPE64;
		size_t functions = 0;

		auto hostOf = [](std::string const& module) {
			if (ApiSetMap::IsApiSetName(Lower(module)))
				if (auto host = ApiSetMap::System().Resolve(Lower(module)))
					return ModuleBase(*host);
			return ModuleBase(module);
		};
		auto addModule = [&](std::string const& module) {
			f.Modules.insert(ModuleBase(module));
			f.Modules.insert(hostOf(module));
		};
		auto addImport = [&](std::string const& module, std::string name) {
			if (name.empty())
				return;
			addModule(module);
			f.Imports.push_back({ ModuleBase(module), hostOf(module), name, Lower(name) });
			functions++;
		};
		if (auto imports = pe.GetImport())
			for (auto const& mod : *imports) {
				addModule(mod.ModuleName);
				for (auto const& fn : mod.ImportFunc) {
					if (IsOrdinalImport(fn, is64)) {
						// ws2_32 and others are often imported by ordinal: the name is that of this system's DLL
						auto name = ResolveOrdinalName(Lower(mod.ModuleName), ImportOrdinal(fn), !is64);
						if (name.empty())
							functions++;
						addImport(mod.ModuleName, name);
					}
					else
						addImport(mod.ModuleName, fn.FuncName.empty() ? std::string(fn.ImpByName.Name) : fn.FuncName);
				}
			}
		if (auto delay = pe.GetDelayImport())
			for (auto const& mod : *delay) {
				addModule(mod.ModuleName);
				for (auto const& fn : mod.DelayImpFunc)
					addImport(mod.ModuleName, fn.FuncName.empty() ? std::string(fn.ImpByName.Name) : fn.FuncName);
			}

		if (auto exports = pe.GetExport())
			for (auto const& fn : exports->Funcs)
				if (!fn.FuncName.empty())
					f.Exports.insert(Lower(fn.FuncName));

		auto const& nt = *pe.GetNTHeader();
		uint16_t subsystem = is64 ? nt.NTHdr64.OptionalHeader.Subsystem : nt.NTHdr32.OptionalHeader.Subsystem;
		uint16_t dllChars = is64 ? nt.NTHdr64.OptionalHeader.DllCharacteristics : nt.NTHdr32.OptionalHeader.DllCharacteristics;
		if (auto sections = pe.GetSecHeaders())
			for (auto const& s : *sections) {
				f.Sections.insert(Lower(s.SectionName));
				auto const& h = s.SecHdr;
				if ((h.Characteristics & IMAGE_SCN_MEM_EXECUTE) && (h.Characteristics & IMAGE_SCN_MEM_WRITE))
					f.Characteristics.insert("writable-code");
				if (h.PointerToRawData < pe.GetFileSize()) {
					auto size = std::min<uint64_t>(h.SizeOfRawData, pe.GetFileSize() - h.PointerToRawData);
					auto data = pe.GetSpan(h.PointerToRawData, (uint32_t)size);
					if (!data.empty() && IsHighEntropySection(ComputeEntropy((const uint8_t*)data.data(), data.size()), h.Characteristics, data.size()))
						f.Characteristics.insert("high-entropy-section");
				}
			}

		if (info->HasTLS && pe.GetTLS() && !pe.GetTLS()->TLSCallbacks.empty())
			f.Characteristics.insert("tls-callbacks");
		if (!FindOverlay(pe).Empty())
			f.Characteristics.insert("overlay");
		if (info->HasCOMDescr)
			f.Characteristics.insert("dotnet");
		if (info->HasSecurity)
			f.Characteristics.insert("signed");
		if (nt.NTHdr64.FileHeader.Characteristics & IMAGE_FILE_DLL)
			f.Characteristics.insert("dll");
		if (subsystem == IMAGE_SUBSYSTEM_NATIVE && ((dllChars & IMAGE_DLLCHARACTERISTICS_WDM_DRIVER) || f.Modules.contains("ntoskrnl")))
			f.Characteristics.insert("driver");
		if (functions == 0)
			f.Characteristics.insert("no-imports");
		if (functions < 10)
			f.Characteristics.insert("few-imports");

		for (auto const& s : FindStrings(pe.GetSpan(0, pe.GetFileSize()))) {
			f.Strings.push_back(Utf8(s.Text));
			f.LowerStrings.push_back(Lower(f.Strings.back()));
		}
		return f;
	}

	constexpr size_t MaxEvidence = 10;

	// "CreateProcess" matches CreateProcessA, CreateProcessW, CreateProcessExW...; "kernel32.CreateFileW" only from kernel32
	bool ApiMatches(std::string const& rule, Features::Import const& import) {
		std::string_view name = rule;
		if (auto dot = rule.rfind('.'); dot != std::string::npos) {
			auto module = ModuleBase(rule.substr(0, dot));
			if (module != import.Module && module != import.Host)
				return false;
			name = std::string_view(rule).substr(dot + 1);
		}
		auto const& n = import.Lower;
		if (n.size() < name.size() || n.compare(0, name.size(), name) != 0)
			return false;
		auto rest = std::string_view(n).substr(name.size());
		return rest.empty() || rest == "a" || rest == "w" || rest == "ex" || rest == "exa" || rest == "exw";
	}
}

struct CapabilityRules::Rule {
	struct Node {
		enum class Type { Api, String, Regex, Section, ImportModule, Export, Characteristic, All, Any, NOf, Not };
			Type Kind{};
		std::vector<std::string> Values;		// lowercase
		std::shared_ptr<std::regex> Pattern;
		std::string PatternText;
		std::vector<Node> Children;
		size_t N{};
	};

	std::string Name, Namespace, Description;
	std::vector<std::string> Attack;
	Node Condition;

	bool Evaluate(Node const& node, Features const& f, std::vector<std::string>& evidence) const {
		auto add = [&](std::string text) {
			if (evidence.size() < MaxEvidence && std::find(evidence.begin(), evidence.end(), text) == evidence.end())
				evidence.push_back(std::move(text));
		};
		using K = Node::Type;
		switch (node.Kind) {
			case K::Api: {
				bool found = false;
				for (auto const& v : node.Values)
					for (auto const& i : f.Imports)
						if (ApiMatches(v, i)) {
							add("api: " + i.Module + "!" + i.Name);
							found = true;
						}
				return found;
			}
			case K::String: {
				bool found = false;
				for (auto const& v : node.Values)
					for (size_t i = 0; i < f.LowerStrings.size(); i++)
						if (f.LowerStrings[i].find(v) != std::string::npos) {
							add("string: \"" + f.Strings[i].substr(0, 120) + "\"");
							found = true;
							break;
						}
				return found;
			}
			case K::Regex:
				for (auto const& s : f.Strings)
					if (s.size() <= 1024 && std::regex_search(s, *node.Pattern)) {
						add("regex /" + node.PatternText + "/: \"" + s.substr(0, 120) + "\"");
						return true;
					}
				return false;
			case K::Section:
				for (auto const& v : node.Values)
					if (f.Sections.contains(v)) {
						add("section: " + v);
						return true;
					}
				return false;
			case K::ImportModule:
				for (auto const& v : node.Values)
					if (f.Modules.contains(ModuleBase(v))) {
						add("import module: " + v);
						return true;
					}
				return false;
			case K::Export:
				for (auto const& v : node.Values)
					if (f.Exports.contains(v)) {
						add("export: " + v);
						return true;
					}
				return false;
			case K::Characteristic:
				for (auto const& v : node.Values)
					if (f.Characteristics.contains(v)) {
						add("characteristic: " + v);
						return true;
					}
				return false;
			case K::All: {
				std::vector<std::string> mine;
				for (auto const& c : node.Children)
					if (!Evaluate(c, f, mine))
						return false;
				for (auto& e : mine)
					add(std::move(e));
				return true;
			}
			case K::Any:
			case K::NOf: {
				size_t need = node.Kind == K::Any ? 1 : node.N, count = 0;
				std::vector<std::string> mine;
				for (auto const& c : node.Children)
					if (Evaluate(c, f, mine))
						count++;
				if (count < need)
					return false;
				for (auto& e : mine)
					add(std::move(e));
				return true;
			}
			case K::Not: {
				std::vector<std::string> ignored;
				return !Evaluate(node.Children.at(0), f, ignored);
			}
		}
		return false;
	}
};

struct CapabilityRules::Impl {
	std::vector<Rule> Rules;
};

namespace {
	using Node = CapabilityRules::Rule::Node;

	std::vector<std::string> Values(JsonValue const& v) {
		std::vector<std::string> values;
		if (v.IsString())
			values.push_back(Lower(v.AsString()));
		else
			for (auto const& item : v.AsArray())
				if (item.IsString())
					values.push_back(Lower(item.AsString()));
		return values;
	}

	const std::set<std::string> KnownCharacteristics{ "tls-callbacks", "overlay", "high-entropy-section", "writable-code", "dotnet",
		"driver", "dll", "few-imports", "no-imports", "signed" };

	bool ParseNode(JsonValue const& json, Node& node, std::string& error) {
		if (!json.IsObject() || json.AsObject().size() != 1) {
			error = "a condition is an object with one key";
			return false;
		}
		auto const& [key, value] = *json.AsObject().begin();
		using K = Node::Type;
		static const std::pair<const char*, K> leaves[] = {
			{ "api", K::Api }, { "string", K::String }, { "section", K::Section }, { "import-module", K::ImportModule },
			{ "export", K::Export }, { "characteristic", K::Characteristic },
		};
		for (auto const& [name, kind] : leaves)
			if (key == name) {
				node.Kind = kind;
				node.Values = Values(value);
				if (node.Values.empty()) {
					error = std::format("\"{}\" needs a string or a list of strings", key);
					return false;
				}
				if (kind == K::Characteristic)
					for (auto const& c : node.Values)
						if (!KnownCharacteristics.contains(c)) {
							error = std::format("unknown characteristic \"{}\"", c);
							return false;
						}
				return true;
			}
		if (key == "regex") {
			node.Kind = K::Regex;
			if (!value.IsString()) {
				error = "\"regex\" needs a string";
				return false;
			}
			try {
				node.PatternText = value.AsString();
				node.Pattern = std::make_shared<std::regex>(node.PatternText, std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
			}
			catch (std::regex_error const& ex) {
				error = std::format("bad regex \"{}\": {}", value.AsString(), ex.what());
				return false;
			}
			return true;
		}
		if (key == "all" || key == "any") {
			node.Kind = key == "all" ? K::All : K::Any;
			if (value.AsArray().empty()) {
				error = std::format("\"{}\" needs a list of conditions", key);
				return false;
			}
			for (auto const& c : value.AsArray()) {
				node.Children.emplace_back();
				if (!ParseNode(c, node.Children.back(), error))
					return false;
			}
			return true;
		}
		if (key == "n-of") {
			node.Kind = K::NOf;
			auto n = value["n"].AsNumber(-1);
			if (n < 1 || value["of"].AsArray().empty() || n > value["of"].AsArray().size()) {
				error = "\"n-of\" needs \"n\" (1 or more) and \"of\" (at least n conditions)";
				return false;
			}
			node.N = (size_t)n;
			for (auto const& c : value["of"].AsArray()) {
				node.Children.emplace_back();
				if (!ParseNode(c, node.Children.back(), error))
					return false;
			}
			return true;
		}
		if (key == "not") {
			node.Kind = K::Not;
			node.Children.emplace_back();
			return ParseNode(value, node.Children.back(), error);
		}
		error = std::format("unknown condition \"{}\"", key);
		return false;
	}

	// The rules that come with TotalPE. They name capabilities by documented APIs and properties of the file; those that
	// nearly every program has (the C runtime imports IsDebuggerPresent and TerminateProcess) are left out, as at the scope of
	// a file they would tell nothing.
	constexpr char BuiltInRules[] = R"json({ "rules": [
	{ "name": "create a process", "namespace": "host-interaction/process/create", "attack": ["T1106"],
	  "condition": { "api": ["CreateProcess", "CreateProcessAsUser", "CreateProcessWithLogon", "CreateProcessWithToken", "WinExec",
	                         "ShellExecute", "NtCreateUserProcess"] } },
	{ "name": "enumerate processes", "namespace": "host-interaction/process/list", "attack": ["T1057"],
	  "condition": { "any": [
	    { "all": [ { "api": "CreateToolhelp32Snapshot" }, { "api": ["Process32First", "Process32Next"] } ] },
	    { "api": ["EnumProcesses", "K32EnumProcesses", "WTSEnumerateProcesses"] } ] } },
	{ "name": "write the memory of another process", "namespace": "host-interaction/process/inject", "attack": ["T1055"],
	  "condition": { "all": [ { "api": ["WriteProcessMemory", "NtWriteVirtualMemory"] },
	                          { "api": ["OpenProcess", "VirtualAllocEx", "NtAllocateVirtualMemory"] } ] } },
	{ "name": "create a thread in another process", "namespace": "host-interaction/process/inject", "attack": ["T1055"],
	  "condition": { "api": ["CreateRemoteThread", "NtCreateThreadEx", "RtlCreateUserThread"] } },
	{ "name": "check for a debugger", "namespace": "anti-analysis/anti-debugging", "attack": ["T1622"],
	  "condition": { "api": ["CheckRemoteDebuggerPresent", "NtSetInformationThread"] } },
	{ "name": "link functions at run time", "namespace": "linking/runtime-linking",
	  "condition": { "all": [ { "api": ["LoadLibrary", "LdrLoadDll"] }, { "api": ["GetProcAddress", "LdrGetProcedureAddress"] } ] } },
	{ "name": "create or change a service", "namespace": "host-interaction/service/create", "attack": ["T1543.003"],
	  "condition": { "api": ["CreateService", "ChangeServiceConfig", "ChangeServiceConfig2"] } },
	{ "name": "start a service", "namespace": "host-interaction/service/start", "attack": ["T1569.002"],
	  "condition": { "api": "StartService" } },
	{ "name": "run at logon from a Run key", "namespace": "persistence/registry/run", "attack": ["T1547.001"],
	  "condition": { "all": [ { "api": ["RegSetValue", "RegSetKeyValue", "NtSetValueKey", "SHSetValue"] },
	                          { "regex": "\\\\CurrentVersion\\\\Run(Once)?(\\\\|$)" } ] } },
	{ "name": "write to the registry", "namespace": "host-interaction/registry/write", "attack": ["T1112"],
	  "condition": { "api": ["RegSetValue", "RegSetKeyValue", "RegCreateKey", "NtSetValueKey", "SHSetValue"] } },
	{ "name": "delete from the registry", "namespace": "host-interaction/registry/delete", "attack": ["T1112"],
	  "condition": { "api": ["RegDeleteKey", "RegDeleteValue", "RegDeleteTree", "SHDeleteKey", "NtDeleteKey"] } },
	{ "name": "query the registry", "namespace": "host-interaction/registry/read", "attack": ["T1012"],
	  "condition": { "api": ["RegQueryValue", "RegGetValue", "RegEnumKey", "RegEnumValue", "NtQueryValueKey", "SHGetValue"] } },
	{ "name": "enumerate files", "namespace": "host-interaction/file-system/list", "attack": ["T1083"],
	  "condition": { "api": ["FindFirstFile", "NtQueryDirectoryFile"] } },
	{ "name": "delete files", "namespace": "host-interaction/file-system/delete",
	  "condition": { "api": ["DeleteFile", "NtDeleteFile", "SHFileOperation"] } },
	{ "name": "connect to a network host (sockets)", "namespace": "communication/socket/connect",
	  "condition": { "api": ["connect", "WSAConnect", "WSAConnectByName"] } },
	{ "name": "listen for network connections (sockets)", "namespace": "communication/socket/listen",
	  "condition": { "api": ["listen", "accept", "WSAAccept", "AcceptEx"] } },
	{ "name": "send HTTP requests", "namespace": "communication/http/client", "attack": ["T1071.001"],
	  "condition": { "api": ["InternetOpenUrl", "HttpSendRequest", "HttpOpenRequest", "WinHttpSendRequest", "WinHttpOpenRequest"] } },
	{ "name": "download a file from a URL", "namespace": "communication/http/download", "attack": ["T1105"],
	  "condition": { "api": ["URLDownloadToFile", "URLDownloadToCacheFile"] } },
	{ "name": "resolve host names (DNS)", "namespace": "communication/dns",
	  "condition": { "api": ["DnsQuery_A", "DnsQuery_W", "DnsQuery_UTF8", "DnsQueryEx", "getaddrinfo", "GetAddrInfo", "gethostbyname"] } },
	{ "name": "use named pipes", "namespace": "communication/named-pipe",
	  "condition": { "api": ["CreateNamedPipe", "ConnectNamedPipe", "CallNamedPipe"] } },
	{ "name": "encrypt or decrypt data (CryptoAPI, CNG)", "namespace": "data-manipulation/encryption",
	  "condition": { "api": ["CryptEncrypt", "CryptDecrypt", "BCryptEncrypt", "BCryptDecrypt"] } },
	{ "name": "hash data (CryptoAPI, CNG)", "namespace": "data-manipulation/hashing",
	  "condition": { "api": ["CryptHashData", "CryptCreateHash", "BCryptHashData", "BCryptCreateHash", "BCryptHash"] } },
	{ "name": "decompress data", "namespace": "data-manipulation/compression",
	  "condition": { "api": ["RtlDecompressBuffer", "RtlDecompressFragment", "cabinet.Decompress"] } },
	{ "name": "read the clipboard", "namespace": "host-interaction/clipboard", "attack": ["T1115"],
	  "condition": { "all": [ { "api": "OpenClipboard" }, { "api": "GetClipboardData" } ] } },
	{ "name": "install a Windows hook", "namespace": "host-interaction/gui/hook",
	  "condition": { "api": "SetWindowsHookEx" } },
	{ "name": "adjust the privileges of a token", "namespace": "host-interaction/process/token", "attack": ["T1134"],
	  "condition": { "api": ["AdjustTokenPrivileges", "NtAdjustPrivilegesToken", "RtlAdjustPrivilege"] } },
	{ "name": "enable the debug privilege", "namespace": "host-interaction/process/token", "attack": ["T1134"],
	  "condition": { "all": [ { "api": ["AdjustTokenPrivileges", "NtAdjustPrivilegesToken", "LookupPrivilegeValue"] },
	                          { "string": "SeDebugPrivilege" } ] } },
	{ "name": "impersonate another security context", "namespace": "host-interaction/process/token", "attack": ["T1134.001"],
	  "condition": { "api": ["ImpersonateLoggedOnUser", "ImpersonateNamedPipeClient", "SetThreadToken"] } },
	{ "name": "send control codes to a device driver", "namespace": "host-interaction/driver/ioctl",
	  "condition": { "api": ["DeviceIoControl", "NtDeviceIoControlFile"] } },
	{ "name": "create a device object", "namespace": "host-interaction/driver/device",
	  "condition": { "api": ["IoCreateDevice", "IoCreateDeviceSecure", "WdfDeviceCreate"] } },
	{ "name": "query WMI", "namespace": "host-interaction/wmi", "attack": ["T1047"],
	  "condition": { "all": [ { "api": ["CoCreateInstance", "CoCreateInstanceEx"] }, { "string": "root\\cimv2" } ] } },
	{ "name": "schedule a task", "namespace": "persistence/scheduled-task", "attack": ["T1053.005"],
	  "condition": { "any": [ { "string": "schtasks" }, { "api": "NetScheduleJobAdd" } ] } },
	{ "name": "shut down or restart the system", "namespace": "host-interaction/os/shutdown", "attack": ["T1529"],
	  "condition": { "api": ["ExitWindowsEx", "InitiateSystemShutdown", "InitiateShutdown", "NtShutdownSystem"] } },
	{ "name": "packed with UPX", "namespace": "anti-analysis/packer/upx", "attack": ["T1027.002"],
	  "condition": { "section": ["UPX0", "UPX1", "UPX2"] } },
	{ "name": "packed (high entropy and few imports)", "namespace": "anti-analysis/packer", "attack": ["T1027.002"],
	  "condition": { "all": [ { "characteristic": "high-entropy-section" }, { "characteristic": "few-imports" } ] } },
	{ "name": "has sections that are both writable and executable", "namespace": "executable/sections",
	  "condition": { "characteristic": "writable-code" } },
	{ "name": "has TLS callbacks", "namespace": "executable/tls",
	  "condition": { "characteristic": "tls-callbacks" } },
	{ "name": "has data appended (an overlay)", "namespace": "executable/overlay",
	  "condition": { "characteristic": "overlay" } },
	{ "name": "runs on .NET", "namespace": "runtime/dotnet",
	  "condition": { "characteristic": "dotnet" } },
	{ "name": "is a kernel driver", "namespace": "runtime/kernel",
	  "condition": { "characteristic": "driver" } }
] })json";
}

CapabilityRules::CapabilityRules() : m_Impl(std::make_unique<Impl>()) {}
CapabilityRules::~CapabilityRules() = default;
CapabilityRules::CapabilityRules(CapabilityRules&&) noexcept = default;
CapabilityRules& CapabilityRules::operator=(CapabilityRules&&) noexcept = default;

CapabilityRules const& CapabilityRules::BuiltIn() {
	static const CapabilityRules rules = [] {
		CapabilityRules r;
		std::vector<std::string> errors;
		r.AddJson(BuiltInRules, errors, "built-in");
		assert(errors.empty());
		return r;
	}();
	return rules;
}

size_t CapabilityRules::Size() const {
	return m_Impl->Rules.size();
}

void CapabilityRules::Add(CapabilityRules const& other) {
	m_Impl->Rules.insert(m_Impl->Rules.end(), other.m_Impl->Rules.begin(), other.m_Impl->Rules.end());
}

bool CapabilityRules::AddJson(std::string_view text, std::vector<std::string>& errors, std::string const& source) {
	auto prefix = source.empty() ? std::string() : source + ": ";
	JsonValue json;
	std::string error;
	if (!JsonValue::Parse(text, json, &error)) {
		errors.push_back(prefix + "not JSON: " + error);
		return false;
	}
	auto const& list = json.IsArray() ? json.AsArray() : json["rules"].AsArray();
	if (list.empty()) {
		errors.push_back(prefix + "no rules (an array of rules, or an object with a \"rules\" array)");
		return false;
	}
	bool ok = true;
	size_t index = 0;
	for (auto const& r : list) {
		index++;
		Rule rule;
		rule.Name = r["name"].AsString();
		rule.Namespace = r["namespace"].AsString();
		rule.Description = r["description"].AsString();
		for (auto const& a : r["attack"].AsArray())
			if (a.IsString())
				rule.Attack.push_back(a.AsString());
		auto where = std::format("{}rule {}{}", prefix, index, rule.Name.empty() ? "" : " (" + rule.Name + ")");
		if (rule.Name.empty()) {
			errors.push_back(where + ": no \"name\"");
			ok = false;
			continue;
		}
		if (!ParseNode(r["condition"], rule.Condition, error)) {
			errors.push_back(where + ": " + error);
			ok = false;
			continue;
		}
		m_Impl->Rules.push_back(std::move(rule));
	}
	return ok;
}

bool CapabilityRules::AddPath(std::wstring const& path, std::vector<std::string>& errors) {
	auto addFile = [&](fs::path const& file) {
		std::ifstream in(file, std::ios::binary);
		if (!in) {
			errors.push_back(Utf8(file.wstring()) + ": cannot be opened");
			return false;
		}
		std::stringstream text;
		text << in.rdbuf();
		return AddJson(text.str(), errors, Utf8(file.wstring()));
	};
	std::error_code ec;
	if (fs::is_regular_file(path, ec))
		return addFile(path);
	if (!fs::is_directory(path, ec)) {
		errors.push_back(Utf8(path) + ": not found");
		return false;
	}
	std::vector<fs::path> files;
	for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
		if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), L".json") == 0)
			files.push_back(it->path());
	std::sort(files.begin(), files.end());
	if (files.empty()) {
		errors.push_back(Utf8(path) + ": no rule files (.json)");
		return false;
	}
	bool ok = true;
	for (auto const& f : files)
		ok &= addFile(f);
	return ok;
}

std::vector<CapabilityMatch> CapabilityRules::Match(PEFile const& pe) const {
	std::vector<CapabilityMatch> matches;
	if (!pe || !pe.GetNTHeader() || m_Impl->Rules.empty())
		return matches;
	auto features = Gather(pe);
	for (auto const& rule : m_Impl->Rules) {
		std::vector<std::string> evidence;
		if (rule.Evaluate(rule.Condition, features, evidence))
			matches.push_back({ rule.Name, rule.Namespace, rule.Description, rule.Attack, std::move(evidence) });
	}
	return matches;
}
