#include "pch.h"
#include "WinRtServer.h"
#include "ImageData.h"
#include "ClrMetadata.h"
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>

namespace {
	bool Exports(PEFile const& pe, char const* name) {
		if (auto exports = pe.GetExport())
			for (auto const& f : exports->Funcs)
				if (f.FuncName == name && f.ForwarderName.empty())
					return true;
		return false;
	}

	bool Imports(PEFile const& pe, char const* name) {
		if (auto imports = pe.GetImport())
			for (auto const& mod : *imports)
				for (auto const& fn : mod.ImportFunc)
					if (fn.FuncName == name)
						return true;
		return false;
	}

	std::wstring Lower(std::wstring text) {
		for (auto& c : text)
			c = towlower(c);
		return text;
	}

	// The registry's runtime classes (read once): their names and what it has of them, and the servers' EXEs
	struct RegisteredClass {
		std::wstring Name;
		int ActivationType{ -1 }, Threading{ -1 }, TrustLevel{ -1 };
		std::wstring Dll;		// expanded, lower case
		std::wstring Server;	// the name of an out-of-process one's server
	};
	struct Registry {
		std::vector<RegisteredClass> Classes;
		std::map<std::wstring, std::wstring> ServerExes;	// server name -> its EXE (expanded, lower case, no arguments)
	};

	std::wstring Expand(std::wstring const& text) {
		WCHAR expanded[MAX_PATH * 2];
		auto n = ::ExpandEnvironmentStringsW(text.c_str(), expanded, _countof(expanded));
		return n && n <= _countof(expanded) ? std::wstring(expanded) : text;
	}

	// the path of an EXE of a command line ("C:\x\y.exe" -Embedding, C:\x\y.exe -k Group)
	std::wstring ExeOf(std::wstring command) {
		if (!command.empty() && command[0] == L'"') {
			auto end = command.find(L'"', 1);
			return command.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
		}
		auto lower = Lower(command);
		auto exe = lower.find(L".exe");
		return exe == std::wstring::npos ? command : command.substr(0, exe + 4);
	}

	Registry const& ReadRegistry() {
		static Registry registry;
		static std::once_flag once;
		std::call_once(once, [] {
			auto dword = [](HKEY key, PCWSTR name) {
				DWORD value, size = sizeof(value);
				return ::RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS ? (int)value : -1;
			};
			auto text = [](HKEY key, PCWSTR name) {
				WCHAR value[1024];
				DWORD size = sizeof(value);
				return ::RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, value, &size) == ERROR_SUCCESS ?
					std::wstring(value) : std::wstring();
			};
			HKEY root;
			if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId", 0, KEY_READ, &root) == ERROR_SUCCESS) {
				WCHAR name[512];
				for (DWORD i = 0; ; i++) {
					DWORD length = _countof(name);
					if (::RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
						break;
					HKEY key;
					if (::RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS)
						continue;
					RegisteredClass c{ name, dword(key, L"ActivationType"), dword(key, L"Threading"), dword(key, L"TrustLevel") };
					if (auto dll = text(key, L"DllPath"); !dll.empty())
						c.Dll = Lower(Expand(dll));
					c.Server = text(key, L"Server");
					::RegCloseKey(key);
					registry.Classes.push_back(std::move(c));
				}
				::RegCloseKey(root);
			}
			if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsRuntime\\Server", 0, KEY_READ, &root) == ERROR_SUCCESS) {
				WCHAR name[512];
				for (DWORD i = 0; ; i++) {
					DWORD length = _countof(name);
					if (::RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
						break;
					HKEY key;
					if (::RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS)
						continue;
					if (auto exe = text(key, L"ExePath"); !exe.empty())
						registry.ServerExes[name] = Lower(ExeOf(Expand(exe)));
					::RegCloseKey(key);
				}
				::RegCloseKey(root);
			}
		});
		return registry;
	}

	// The string a function gives back, of one that only does that: lea rax, [rip + s]; ret (x64), mov eax, s; ret (x86),
	// adrp x0, s; add x0, x0, #s; ret (ARM64). 0 if it is not one.
	DWORD ReturnedAddress(ImageData const& image, WORD machine, DWORD function) {
		uint8_t code[16];
		if (!image.InCode(function) || !image.Read(function, code, sizeof(code)))
			return 0;
		if (machine == IMAGE_FILE_MACHINE_AMD64 && code[0] == 0x48 && code[1] == 0x8D && code[2] == 0x05 && code[7] == 0xC3)
			return function + 7 + *(int32_t const*)(code + 3);
		if (machine == IMAGE_FILE_MACHINE_I386 && code[0] == 0xB8 && code[5] == 0xC3)
			return image.RvaOf(*(uint32_t const*)(code + 1));
		if (machine == IMAGE_FILE_MACHINE_ARM64) {
			auto adrp = *(uint32_t const*)code, add = *(uint32_t const*)(code + 4), ret = *(uint32_t const*)(code + 8);
			if ((adrp & 0x9F00001F) != 0x90000000 || (add & 0xFFC003FF) != 0x91000000 || ret != 0xD65F03C0)
				return 0;
			int64_t page = (int64_t)(((adrp >> 5) & 0x7FFFF) << 2 | ((adrp >> 29) & 3)) << 12;
			if (page & (1LL << 32))
				page -= 1LL << 33;
			return (DWORD)(((int64_t)function & ~0xFFFLL) + page + ((add >> 10) & 0xFFF));
		}
		return 0;
	}

	// a runtime class's name in the image: "Namespace.Name" (letters, digits, '_', '.'), of one dot at least
	std::wstring ClassName(ImageData const& image, DWORD rva) {
		wchar_t text[257]{};
		if (!rva || !image.InData(rva) && !image.InCode(rva))
			return {};
		for (size_t i = 0; i < 256; i++) {
			if (!image.Read(rva + DWORD(i * 2), text + i, 2))
				return {};
			if (!text[i])
				break;
			if (!iswalnum(text[i]) && text[i] != L'_' && text[i] != L'.')
				return {};
		}
		std::wstring name = text;
		return name.size() > 2 && name.size() < 256 && name.find(L'.') != std::wstring::npos && name.front() != L'.' && name.back() != L'.' ?
			name : std::wstring();
	}
}

bool IsWinRtServer(PEFile const& pe) {
	return Exports(pe, "DllGetActivationFactory") || Imports(pe, "RoRegisterActivationFactories");
}

std::vector<WinRtClass> FindWinRtClasses(PEFile const& pe, std::wstring const& path) {
	std::vector<WinRtClass> classes;
	auto sections = pe.GetSecHeaders();
	if (!sections || !pe.GetFileInfo())
		return classes;
	ImageData image(pe);
	auto machine = pe.GetFileInfo()->IsPE64 ? pe.GetNTHeader()->NTHdr64.FileHeader.Machine : pe.GetNTHeader()->NTHdr32.FileHeader.Machine;
	std::map<std::wstring, size_t> byName;
	auto add = [&](std::wstring const& name) -> WinRtClass& {
		auto [it, added] = byName.try_emplace(name, classes.size());
		if (added)
			classes.push_back({ name });
		return classes[it->second];
	};

	// WRL's creator map, the entries of runtime classes (CreatorMap): factoryCreator, getRuntimeName, getTrustLevel (code),
	// factoryCache (writable data), serverName (null, or a string). (x86 Windows' DLLs have their constant data in their code
	// section: of 32-bit code, all sections are looked in.)
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		bool code = h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE);
		if (image.Pe64 ? code || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) : !code && !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto size = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		std::vector<uint8_t> data(size);
		if (!size || !image.Read(h.VirtualAddress, data.data(), size))
			continue;
		auto field = [&](size_t at, int index) -> DWORD {
			auto offset = at + index * (size_t)image.Pointer;
			if (offset + image.Pointer > size)
				return 0;
			uint64_t value = 0;
			memcpy(&value, data.data() + offset, image.Pointer);
			return image.RvaOf(value);
		};
		for (size_t at = 0; at + 4 * image.Pointer <= size; at += image.Pointer) {
			auto creator = field(at, 0), getName = field(at, 1), getTrust = field(at, 2), cache = field(at, 3);
			if (!creator || !getName || !getTrust || !cache || !image.InCode(creator) || !image.InCode(getName) || !image.InCode(getTrust) ||
				!image.InData(cache) || !image.Writable(cache) || getName == creator || getName == getTrust)
				continue;
			auto name = ClassName(image, ReturnedAddress(image, machine, getName));
			if (name.empty())
				continue;
			auto& c = add(name);
			if (c.Entry)
				continue;
			c.Source = L"creator map (WRL)";
			c.Entry = h.VirtualAddress + (DWORD)at;
			c.Creator = creator;
			c.NameFunction = getName;
			c.TrustFunction = getTrust;
		}
	}

	// the registry's classes whose server is the file: its DLL, or an EXE server's
	auto file = Lower(path);
	auto const& registry = ReadRegistry();
	for (auto const& r : registry.Classes) {
		std::wstring server;
		if (!r.Dll.empty() && r.Dll == file)
			server = r.Dll;
		else if (!r.Server.empty())
			if (auto exe = registry.ServerExes.find(r.Server); exe != registry.ServerExes.end() && exe->second == file)
				server = r.Server + L": " + exe->second;
		if (server.empty())
			continue;
		auto& c = add(r.Name);
		c.Source = c.Source.empty() ? L"registry" : c.Source + L", registry";
		c.ActivationType = r.ActivationType;
		c.Threading = r.Threading;
		c.TrustLevel = r.TrustLevel;
		c.Server = server;
	}
	std::ranges::sort(classes, {}, &WinRtClass::Name);
	return classes;
}

namespace {
	// The metadata files of Windows (System32\WinMetadata), parsed as they are needed and kept
	struct MetadataFile {
		PEFile Pe;
		ClrMetadata Metadata;
		bool Ok{ false };
	};

	MetadataFile const* Metadata(std::wstring const& name) {
		static std::mutex lock;
		static std::map<std::wstring, std::unique_ptr<MetadataFile>> files;
		std::lock_guard guard(lock);
		auto [it, added] = files.try_emplace(Lower(name));
		if (added) {
			WCHAR dir[MAX_PATH];
			::GetSystemDirectoryW(dir, MAX_PATH);
			auto path = std::wstring(dir) + L"\\WinMetadata\\" + name + L".winmd";
			auto file = std::make_unique<MetadataFile>();
			file->Ok = ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && file->Pe.Open(path) && file->Metadata.Parse(file->Pe);
			it->second = std::move(file);
		}
		return it->second->Ok ? it->second.get() : nullptr;
	}

	// a type's name (or a signature) without the assemblies ("[Windows.Foundation]Windows.Foundation.IClosable"), the kinds
	// of types, the arity of generic ones ("IVector`1<string>") and "instance": "Ns.Name", "IVector<string> get_Items()"
	std::wstring PlainName(std::wstring name) {
		static std::wregex const assembly(LR"(\[[^\]]*\])"), kind(LR"(\b(class|valuetype|instance) )"), arity(LR"(`\d+)");
		name = std::regex_replace(name, assembly, L"");
		name = std::regex_replace(name, kind, L"");
		return std::regex_replace(name, arity, L"");
	}

	// The type of a full name ("Windows.Globalization.Calendar") in the metadata file of its namespace (the longest of
	// its prefixes that there is a file of): the file and the TypeDef row; null if there is none
	std::pair<MetadataFile const*, uint32_t> FindType(std::wstring const& full, std::wstring* file = nullptr) {
		auto dot = full.rfind(L'.');
		if (dot == std::wstring::npos)
			return {};
		auto ns = full.substr(0, dot), name = full.substr(dot + 1);
		for (auto prefix = ns; !prefix.empty(); ) {
			if (auto md = Metadata(prefix)) {
				auto const& types = md->Metadata.Types();
				for (uint32_t i = 0; i < types.size(); i++)
					if (types[i].Name == name && types[i].Namespace == ns) {
						if (file)
							*file = prefix + L".winmd";
						return { md, i + 1 };
					}
			}
			auto last = prefix.rfind(L'.');
			prefix = last == std::wstring::npos ? std::wstring() : prefix.substr(0, last);
		}
		return {};
	}

	// "{...}" of a type's [Guid(uint, ushort, ushort, byte...)]
	std::wstring GuidOf(ClrMetadata const& md, uint32_t row) {
		for (auto const& a : md.CustomAttributes(0x02000000 | row)) {
			if (md.TypeName(a.AttributeType).ends_with(L"GuidAttribute") && a.Value.size() >= 2 + 16) {
				auto b = a.Value.data() + 2;
				return std::format(L"{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}", *(uint32_t const*)b,
					*(uint16_t const*)(b + 4), *(uint16_t const*)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
			}
		}
		return {};
	}

	WinRtInterface InterfaceOf(std::wstring const& name, std::wstring const& role) {
		WinRtInterface result{ name, role };
		// (a generic one's methods are of its type arguments: they are not listed)
		if (name.find(L'<') != std::wstring::npos || name.find(L'`') != std::wstring::npos)
			return result;
		auto [md, row] = FindType(name);
		if (!md)
			return result;
		result.Guid = GuidOf(md->Metadata, row);
		uint32_t slot = 6;		// (after IInspectable's: QueryInterface, AddRef, Release, GetIids, GetRuntimeClassName, GetTrustLevel)
		for (auto method : md->Metadata.MethodsOf(row)) {
			auto const& m = md->Metadata.Methods()[method - 1];
			result.Methods.push_back({ m.Name, PlainName(md->Metadata.MethodSignature(method)), slot++ });
		}
		return result;
	}
}

WinRtClassMetadata FindWinRtClassMetadata(std::wstring const& name) {
	WinRtClassMetadata result;
	auto [md, row] = FindType(name, &result.File);
	if (!md)
		return result;
	auto const& m = md->Metadata;
	// [Activatable(version)]: IActivationFactory; [Activatable(typeof(IFactory), version)], [Static(typeof(IStatics), version)],
	// [Composable(typeof(IFactory), type, version)]: the interfaces the factory implements
	for (auto const& a : m.CustomAttributes(0x02000000 | row)) {
		auto type = m.TypeName(a.AttributeType);
		auto role = type.ends_with(L".ActivatableAttribute") ? L"factory" : type.ends_with(L".StaticAttribute") ? L"statics" :
			type.ends_with(L".ComposableAttribute") ? L"composable factory" : nullptr;
		if (!role)
			continue;
		auto argument = std::ranges::find_if(a.Arguments, [](auto const& arg) { return arg.Il.starts_with(L"type("); });
		if (argument == a.Arguments.end()) {
			if (role == std::wstring_view(L"factory"))
				result.Activatable = true;
			continue;
		}
		auto interfaceName = PlainName(argument->Il.substr(5, argument->Il.size() - 6));
		result.Interfaces.push_back(InterfaceOf(interfaceName, role));
	}
	// the interfaces of its instances
	for (auto token : m.Interfaces(row))
		result.Interfaces.push_back(InterfaceOf(PlainName(m.TypeName(token)), L"instance"));
	return result;
}
