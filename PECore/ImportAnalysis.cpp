#include "pch.h"
#include "ImportAnalysis.h"
#include "ApiSetMap.h"
#include "Md5.h"
#include "ImphashOrdinals.h"
#include <map>
#include <mutex>

namespace {
	std::string Lower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	// libraries whose ordinals the import hash replaces with names
	bool HasOrdinalTable(std::string const& dll) {
		return dll == "ws2_32.dll" || dll == "wsock32.dll" || dll == "oleaut32.dll";
	}
}

bool IsOrdinalImport(PEImportFunction const& fn, bool is64) {
	if (!fn.FuncName.empty() || fn.ImpByName.Name[0] != 0)
		return false;
	return is64 ? (fn.unThunk.Thunk64.u1.Ordinal & IMAGE_ORDINAL_FLAG64) != 0 : (fn.unThunk.Thunk32.u1.Ordinal & IMAGE_ORDINAL_FLAG32) != 0;
}

uint16_t ImportOrdinal(PEImportFunction const& fn) {
	// the ordinal is in the low 16 bits of the thunk, which are the first bytes in both layouts
	return fn.FuncName.empty() && fn.ImpByName.Name[0] == 0 ? (uint16_t)(fn.unThunk.Thunk32.u1.Ordinal & 0xFFFF) : (uint16_t)0;
}

std::string ResolveOrdinalName(std::string const& dllName, uint16_t ordinal, bool importerIs32Bit) {
	// export tables are read once per DLL
	static std::mutex lock;
	static std::map<std::string, std::map<uint16_t, std::string>> cache;

	auto dll = Lower(dllName);
	if (auto host = ApiSetMap::System().Resolve(dll))
		dll = *host;

	std::lock_guard guard(lock);
	auto key = dll + (importerIs32Bit ? "|32" : "|64");
	auto it = cache.find(key);
	if (it == cache.end()) {
		std::map<uint16_t, std::string> names;
		WCHAR dir[MAX_PATH];
		bool haveDir = importerIs32Bit ? ::GetSystemWow64DirectoryW(dir, MAX_PATH) != 0 : ::GetSystemDirectoryW(dir, MAX_PATH) != 0;
		if (haveDir) {
			PEFile pe;
			if (pe.Open(std::wstring(dir) + L"\\" + std::wstring(dll.begin(), dll.end()))) {
				if (auto exp = pe.GetExport())
					for (auto& f : exp->Funcs)
						if (!f.FuncName.empty() && f.Ordinal <= 0xFFFF)
							names[(uint16_t)f.Ordinal] = f.FuncName;
			}
		}
		it = cache.emplace(key, std::move(names)).first;
	}
	auto name = it->second.find(ordinal);
	return name == it->second.end() ? std::string() : name->second;
}

std::string ImphashInput(PEFile const& pe, OrdinalNameLookup const& lookup) {
	const bool is64 = pe.GetFileInfo()->IsPE64;
	std::string result;
	for (auto const& mod : *pe.GetImport()) {
		auto dll = Lower(mod.ModuleName);
		auto library = dll;
		// only these three extensions are removed
		if (auto dot = library.rfind('.'); dot != std::string::npos) {
			auto ext = library.substr(dot + 1);
			if (ext == "ocx" || ext == "sys" || ext == "dll")
				library.resize(dot);
		}

		for (auto const& fn : mod.ImportFunc) {
			std::string name;
			if (!IsOrdinalImport(fn, is64)) {
				name = fn.FuncName;
			}
			else {
				auto ordinal = ImportOrdinal(fn);
				if (HasOrdinalTable(dll)) {
					if (lookup)
						name = lookup(dll, ordinal);
					else if (auto known = ImphashOrdinalName(dll, ordinal))
						name = known;
				}
				if (name.empty())
					name = "ord" + std::to_string(ordinal);
			}
			if (name.empty())
				continue;
			if (!result.empty())
				result += ',';
			result += library + "." + Lower(name);
		}
	}
	return result;
}

std::string ComputeImphash(PEFile const& pe) {
	auto input = ImphashInput(pe);
	return input.empty() ? std::string() : Md5Hex(input);
}
