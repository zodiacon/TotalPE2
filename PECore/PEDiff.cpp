#include "pch.h"
#include "PEDiff.h"
#include "PEFile.h"
#include "VersionInfo.h"
#include <algorithm>
#include <format>
#include <ranges>
#include <unordered_map>

namespace {
	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	std::wstring Lower(std::wstring s) {
		if (!s.empty())
			::CharLowerBuffW(s.data(), (DWORD)s.size());
		return s;
	}

	// an import by ordinal has no name, and the ordinal flag in its thunk
	bool IsOrdinalImport(PEImportFunction const& fn, bool is64) {
		if (!fn.FuncName.empty() || fn.ImpByName.Name[0] != 0)
			return false;
		return is64 ? (fn.unThunk.Thunk64.u1.Ordinal & IMAGE_ORDINAL_FLAG64) != 0 : (fn.unThunk.Thunk32.u1.Ordinal & IMAGE_ORDINAL_FLAG32) != 0;
	}

	uint16_t ImportOrdinal(PEImportFunction const& fn) {
		return (uint16_t)(fn.unThunk.Thunk32.u1.Ordinal & 0xFFFF);
	}

	std::wstring Hex(uint64_t value) {
		return std::format(L"0x{:X}", value);
	}

	std::wstring TimeStamp(uint32_t secs) {
		if (secs == 0)
			return L"0";
		tm t{};
		__time64_t time = secs;
		if (_gmtime64_s(&t, &time))
			return Hex(secs);
		return std::format(L"0x{:08X} ({:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC)", secs, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	}

	std::wstring Version(uint32_t ms, uint32_t ls) {
		return std::format(L"{}.{}.{}.{}", HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls));
	}

	std::wstring ResourceTypeName(PEResFlat const& res) {
		if (!res.TypeStr.empty())
			return res.TypeStr;
		static const std::unordered_map<WORD, PCWSTR> names = {
			{ 1, L"Cursor" }, { 2, L"Bitmap" }, { 3, L"Icon" }, { 4, L"Menu" }, { 5, L"Dialog" }, { 6, L"String Table" },
			{ 7, L"Font Directory" }, { 8, L"Font" }, { 9, L"Accelerators" }, { 10, L"RC Data" }, { 11, L"Message Table" },
			{ 12, L"Group Cursor" }, { 14, L"Group Icon" }, { 16, L"Version" }, { 17, L"Dialog Include" }, { 19, L"Plug && Play" },
			{ 20, L"VxD" }, { 21, L"Animated Cursor" }, { 22, L"Animated Icon" }, { 23, L"HTML" }, { 24, L"Manifest" },
		};
		auto it = names.find(res.TypeID);
		return it == names.end() ? std::format(L"#{}", res.TypeID) : it->second;
	}

	// A field of an item; fields that are not compared are only shown (the address of an export moves in every build)
	struct Field {
		std::wstring Name, Value;
		bool Compare{ true };
	};

	// An item of one of the files, by the key it is matched with
	struct Keyed {
		std::wstring Key;		// matched case sensitively; made unique if it repeats
		std::wstring Name;		// what is shown (the key if empty)
		std::vector<Field> Fields;
		std::span<const std::byte> Data;	// compared too, if both files have it
		bool HasData{ false };
		DiffLocation Where;
	};

	std::wstring FieldsText(std::vector<Field> const& fields) {
		std::wstring text;
		for (auto const& f : fields) {
			if (!text.empty())
				text += L", ";
			text += f.Name.empty() ? f.Value : f.Name + L" " + f.Value;
		}
		return text;
	}

	// "12 of 512 bytes differ", or "" if the data is the same
	std::wstring CompareData(std::span<const std::byte> a, std::span<const std::byte> b) {
		if (a.size() != b.size())
			return std::format(L"size {} -> {} bytes", a.size(), b.size());
		size_t differ = 0;
		for (size_t i = 0; i < a.size(); i++)
			differ += a[i] != b[i];
		return differ ? std::format(L"{} of {} bytes differ", differ, a.size()) : L"";
	}

	// keys that repeat get a number: the second ".text" is ".text (2)"
	void MakeUnique(std::vector<Keyed>& items) {
		std::unordered_map<std::wstring, int> seen;
		for (auto& item : items) {
			if (item.Name.empty())
				item.Name = item.Key;
			if (int n = ++seen[item.Key]; n > 1) {
				item.Key += std::format(L" ({})", n);
				item.Name += std::format(L" ({})", n);
			}
		}
	}

	// The items of this file in their order (the same, changed and removed ones), then the added ones in the order of the other file
	void Compare(PEDiff& diff, DiffCategory category, std::vector<Keyed> left, std::vector<Keyed> right) {
		MakeUnique(left);
		MakeUnique(right);
		std::unordered_map<std::wstring, size_t> rightIndex;
		for (size_t i = 0; i < right.size(); i++)
			rightIndex.emplace(right[i].Key, i);
		std::vector<bool> matched(right.size());

		for (auto& l : left) {
			DiffItem item{ .Category = category, .Name = l.Name, .Left = FieldsText(l.Fields), .Where = std::move(l.Where) };
			auto it = rightIndex.find(l.Key);
			if (it == rightIndex.end()) {
				item.Status = DiffStatus::Removed;
				diff.Items.push_back(std::move(item));
				continue;
			}
			auto const& r = right[it->second];
			matched[it->second] = true;
			item.Right = FieldsText(r.Fields);
			// what changed: the fields by name, then the data (an item that is a single value needs no details: the values say it)
			std::vector<std::wstring> changes;
			bool changed = false;
			auto change = [&](std::wstring const& what) {
				changed = true;
				if (!what.empty())
					changes.push_back(what);
			};
			for (auto const& f : l.Fields) {
				if (!f.Compare)
					continue;
				auto other = std::ranges::find(r.Fields, f.Name, &Field::Name);
				if (other == r.Fields.end() || other->Value != f.Value)
					change(Lower(f.Name));
			}
			for (auto const& f : r.Fields)
				if (f.Compare && std::ranges::find(l.Fields, f.Name, &Field::Name) == l.Fields.end())
					change(Lower(f.Name));
			if (l.HasData && r.HasData)
				if (auto data = CompareData(l.Data, r.Data); !data.empty())
					change(L"content: " + data);
			item.Status = changed ? DiffStatus::Changed : DiffStatus::Same;
			for (auto const& c : changes)
				item.Details += (item.Details.empty() ? L"" : L"; ") + c;
			diff.Items.push_back(std::move(item));
		}
		for (size_t i = 0; i < right.size(); i++)
			if (!matched[i])
				diff.Items.push_back({ .Category = category, .Name = right[i].Name, .Status = DiffStatus::Added, .Right = FieldsText(right[i].Fields) });
	}

	std::vector<Keyed> HeaderFields(PEFile const& pe) {
		std::vector<Keyed> items;
		auto add = [&](PCWSTR name, std::wstring value) {
			items.push_back({ .Key = name, .Fields = { { L"", std::move(value) } } });
		};
		auto info = pe.GetFileInfo();
		auto nt = pe.GetNTHeader();
		if (!info || !nt)
			return items;
		auto const& fh = info->IsPE64 ? nt->NTHdr64.FileHeader : nt->NTHdr32.FileHeader;
		auto machine = MapFileHdrMachine.find(fh.Machine);
		add(L"Machine", machine == MapFileHdrMachine.end() ? Hex(fh.Machine) : std::wstring(machine->second));
		add(L"Number of Sections", std::to_wstring(fh.NumberOfSections));
		add(L"Time/Date Stamp", TimeStamp(fh.TimeDateStamp));
		add(L"Characteristics", std::format(L"0x{:04X}", fh.Characteristics));
		auto optional = [&](auto const& oh) {
			add(L"Magic", oh.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? L"PE32+" : L"PE32");
			add(L"Linker Version", std::format(L"{}.{}", oh.MajorLinkerVersion, oh.MinorLinkerVersion));
			add(L"Size of Code", Hex(oh.SizeOfCode));
			add(L"Size of Initialized Data", Hex(oh.SizeOfInitializedData));
			add(L"Size of Uninitialized Data", Hex(oh.SizeOfUninitializedData));
			add(L"Entry Point", Hex(oh.AddressOfEntryPoint));
			add(L"Image Base", Hex(oh.ImageBase));
			add(L"Section Alignment", Hex(oh.SectionAlignment));
			add(L"File Alignment", Hex(oh.FileAlignment));
			add(L"OS Version", std::format(L"{}.{}", oh.MajorOperatingSystemVersion, oh.MinorOperatingSystemVersion));
			add(L"Image Version", std::format(L"{}.{}", oh.MajorImageVersion, oh.MinorImageVersion));
			add(L"Subsystem Version", std::format(L"{}.{}", oh.MajorSubsystemVersion, oh.MinorSubsystemVersion));
			add(L"Size of Image", Hex(oh.SizeOfImage));
			add(L"Size of Headers", Hex(oh.SizeOfHeaders));
			add(L"Checksum", std::format(L"0x{:08X}", oh.CheckSum));
			auto subsystem = MapOptHdrSubsystem.find(oh.Subsystem);
			add(L"Subsystem", subsystem == MapOptHdrSubsystem.end() ? std::to_wstring(oh.Subsystem) : std::wstring(subsystem->second));
			add(L"DLL Characteristics", std::format(L"0x{:04X}", oh.DllCharacteristics));
			add(L"Stack Reserve", Hex(oh.SizeOfStackReserve));
			add(L"Stack Commit", Hex(oh.SizeOfStackCommit));
			add(L"Heap Reserve", Hex(oh.SizeOfHeapReserve));
			add(L"Heap Commit", Hex(oh.SizeOfHeapCommit));
		};
		if (info->IsPE64)
			optional(nt->NTHdr64.OptionalHeader);
		else
			optional(nt->NTHdr32.OptionalHeader);
		return items;
	}

	std::vector<Keyed> Directories(PEFile const& pe) {
		static PCWSTR const names[] = {
			L"Export", L"Import", L"Resource", L"Exception", L"Security", L"Base Relocation", L"Debug", L"Architecture",
			L"Global Pointer", L"TLS", L"Load Config", L"Bound Import", L"IAT", L"Delay Import", L"COM Descriptor", L"Reserved",
		};
		std::vector<Keyed> items;
		auto dirs = pe.GetDataDirs();
		if (!dirs)
			return items;
		for (size_t i = 0; i < dirs->size() && i < _countof(names); i++) {
			auto const& d = (*dirs)[i].DataDir;
			if (d.VirtualAddress == 0 && d.Size == 0)
				continue;
			items.push_back({ .Key = names[i], .Fields = { { L"Address", Hex(d.VirtualAddress) }, { L"Size", Hex(d.Size) } } });
		}
		return items;
	}

	std::vector<Keyed> Sections(PEFile const& pe) {
		std::vector<Keyed> items;
		auto sections = pe.GetSecHeaders();
		if (!sections)
			return items;
		int index = 0;
		for (auto const& s : *sections) {
			auto const& h = s.SecHdr;
			Keyed item{ .Key = Widen(s.SectionName), .Fields = {
				{ L"RVA", Hex(h.VirtualAddress) }, { L"Virtual Size", Hex(h.Misc.VirtualSize) }, { L"Raw Size", Hex(h.SizeOfRawData) },
				{ L"Characteristics", std::format(L"0x{:08X}", h.Characteristics) } } };
			if (h.SizeOfRawData && h.PointerToRawData < pe.GetFileSize()) {
				item.Data = pe.GetSpan(h.PointerToRawData, std::min(h.SizeOfRawData, pe.GetFileSize() - h.PointerToRawData));
				item.HasData = true;
			}
			item.Where = { .Place = DiffPlace::Section, .Name = item.Key, .Value = h.VirtualAddress, .Index = index };
			items.push_back(std::move(item));
			index++;
		}
		return items;
	}

	void Imports(PEFile const& pe, std::vector<Keyed>& modules, std::vector<Keyed>& functions) {
		bool is64 = pe.GetFileInfo() && pe.GetFileInfo()->IsPE64;
		if (auto imports = pe.GetImport()) {
			int m = 0;
			for (auto const& mod : *imports) {
				auto module = Widen(mod.ModuleName);
				modules.push_back({ .Key = Lower(module), .Name = module, .Fields = { { L"Functions", std::to_wstring(mod.ImportFunc.size()) } },
					.Where = { .Place = DiffPlace::ImportModule, .Name = module, .Index = m } });
				for (auto const& fn : mod.ImportFunc) {
					bool ordinal = IsOrdinalImport(fn, is64);
					auto name = ordinal ? std::format(L"#{}", ImportOrdinal(fn)) : Widen(fn.FuncName);
					functions.push_back({ .Key = Lower(module) + L"!" + name, .Name = module + L"!" + name,
						.Where = { .Place = DiffPlace::Import, .Name = name, .Value = ordinal ? ImportOrdinal(fn) : 0u, .Index = m, .Owner = module } });
				}
				m++;
			}
		}
		// the delay-load imports are matched by themselves: a module that becomes delay-loaded is removed and added
		if (auto delay = pe.GetDelayImport()) {
			for (auto const& mod : *delay) {
				auto module = Widen(mod.ModuleName) + L" (delay-load)";
				modules.push_back({ .Key = Lower(module), .Name = module, .Fields = { { L"Functions", std::to_wstring(mod.DelayImpFunc.size()) } } });
				for (auto const& fn : mod.DelayImpFunc) {
					auto name = fn.FuncName.empty() ? std::wstring(L"?") : Widen(fn.FuncName);
					functions.push_back({ .Key = Lower(module) + L"!" + name, .Name = module + L"!" + name });
				}
			}
		}
	}

	std::vector<Keyed> Exports(PEFile const& pe) {
		std::vector<Keyed> items;
		auto exports = pe.GetExport();
		if (!exports)
			return items;
		for (auto const& e : exports->Funcs) {
			Keyed item{ .Key = e.FuncName.empty() ? std::format(L"#{}", e.Ordinal) : Widen(e.FuncName) };
			item.Fields.push_back({ L"Ordinal", std::to_wstring(e.Ordinal) });
			if (!e.ForwarderName.empty())
				item.Fields.push_back({ L"Forwarded to", Widen(e.ForwarderName) });
			else
				item.Fields.push_back({ L"RVA", Hex(e.FuncRVA), false });
			item.Where = { .Place = DiffPlace::Export, .Name = item.Key, .Value = e.Ordinal };
			items.push_back(std::move(item));
		}
		return items;
	}

	std::vector<Keyed> Resources(PEFile const& pe) {
		std::vector<Keyed> items;
		int index = 0;
		for (auto const& res : pe.GetFlatResources()) {
			auto name = res.NameID == 0 ? res.NameStr : std::format(L"#{}", res.NameID);
			auto lang = !res.LangStr.empty() ? res.LangStr : LanguageName(res.LangID);
			if (lang.empty())
				lang = std::format(L"0x{:04X}", res.LangID);
			Keyed item{ .Key = std::format(L"{}\\{} ({})", ResourceTypeName(res), name, lang),
				.Fields = { { L"Size", std::to_wstring(res.Data.size()) } }, .Data = res.Data, .HasData = true };
			item.Where = { .Place = DiffPlace::Resource, .Name = item.Key, .Index = index };
			items.push_back(std::move(item));
			index++;
		}
		return items;
	}

	std::vector<Keyed> Version(PEFile const& pe) {
		std::vector<Keyed> items;
		auto const& resources = pe.GetFlatResources();
		auto it = std::ranges::find(resources, (WORD)16, &PEResFlat::TypeID);
		if (it == resources.end())
			return items;
		auto info = ParseVersionInfo(it->Data);
		if (info.HasFixedInfo) {
			items.push_back({ .Key = L"File Version (fixed)", .Fields = { { L"", Version(info.Fixed.dwFileVersionMS, info.Fixed.dwFileVersionLS) } } });
			items.push_back({ .Key = L"Product Version (fixed)", .Fields = { { L"", Version(info.Fixed.dwProductVersionMS, info.Fixed.dwProductVersionLS) } } });
		}
		if (!info.Tables.empty())
			for (auto const& [name, value] : info.Tables.front().Strings)
				items.push_back({ .Key = name, .Fields = { { L"", value } } });
		return items;
	}

	std::vector<Keyed> DebugInfo(PEFile const& pe) {
		std::vector<Keyed> items;
		if (auto debug = pe.GetDebug()) {
			for (auto const& d : *debug) {
				if (d.Directory.Type != IMAGE_DEBUG_TYPE_CODEVIEW)
					continue;
				items.push_back({ .Key = L"PDB", .Fields = { { L"", Widen(d.PdbPath) } } });
				// RSDS, the GUID and the age: the identity of the PDB
				auto offset = d.Directory.PointerToRawData;
				if (d.Directory.SizeOfData >= 24 && pe.Read<uint32_t>(offset) == 0x53445352) {
					auto guid = pe.Read<GUID>(offset + 4);
					items.push_back({ .Key = L"PDB GUID and Age", .Fields = { { L"", std::format(L"{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}, {}",
						guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4],
						guid.Data4[5], guid.Data4[6], guid.Data4[7], pe.Read<uint32_t>(offset + 20)) } } });
				}
			}
		}
		if (auto security = pe.GetSecurity(); security && !security->empty())
			items.push_back({ .Key = L"Authenticode Signature", .Fields = { { L"Size", std::to_wstring(security->front().WinCert.dwLength) } } });
		if (auto rich = pe.GetRichHeader(); rich && !rich->Entries.empty())
			items.push_back({ .Key = L"Rich Header", .Fields = { { L"Entries", std::to_wstring(rich->Entries.size()) } } });
		return items;
	}
}

PEDiff ComparePEFiles(PEFile const& left, PEFile const& right) {
	PEDiff diff;
	auto leftData = left.GetSpan(0, left.GetFileSize()), rightData = right.GetSpan(0, right.GetFileSize());
	diff.Identical = leftData.size() == rightData.size() && std::equal(leftData.begin(), leftData.end(), rightData.begin());

	auto content = CompareData(leftData, rightData);
	diff.Items.push_back({ .Category = DiffCategory::File, .Name = L"Size", .Status = leftData.size() == rightData.size() ? DiffStatus::Same : DiffStatus::Changed,
		.Left = std::to_wstring(leftData.size()), .Right = std::to_wstring(rightData.size()) });
	diff.Items.push_back({ .Category = DiffCategory::File, .Name = L"Content", .Status = diff.Identical ? DiffStatus::Same : DiffStatus::Changed,
		.Details = diff.Identical ? L"The files are identical" : content });

	Compare(diff, DiffCategory::Header, HeaderFields(left), HeaderFields(right));
	Compare(diff, DiffCategory::Directory, Directories(left), Directories(right));
	Compare(diff, DiffCategory::Section, Sections(left), Sections(right));
	std::vector<Keyed> leftModules, leftFunctions, rightModules, rightFunctions;
	Imports(left, leftModules, leftFunctions);
	Imports(right, rightModules, rightFunctions);
	Compare(diff, DiffCategory::Import, std::move(leftModules), std::move(rightModules));
	Compare(diff, DiffCategory::Import, std::move(leftFunctions), std::move(rightFunctions));
	Compare(diff, DiffCategory::Export, Exports(left), Exports(right));
	Compare(diff, DiffCategory::Resource, Resources(left), Resources(right));
	Compare(diff, DiffCategory::Version, Version(left), Version(right));
	Compare(diff, DiffCategory::Debug, DebugInfo(left), DebugInfo(right));
	return diff;
}

size_t PEDiff::Count(DiffStatus status) const {
	return std::ranges::count(Items, status, &DiffItem::Status);
}

const wchar_t* PEDiff::StatusName(DiffStatus status) {
	switch (status) {
		case DiffStatus::Same: return L"Same";
		case DiffStatus::Changed: return L"Changed";
		case DiffStatus::Added: return L"Added";
		case DiffStatus::Removed: return L"Removed";
	}
	return L"";
}

const wchar_t* PEDiff::CategoryName(DiffCategory category) {
	switch (category) {
		case DiffCategory::File: return L"File";
		case DiffCategory::Header: return L"Header";
		case DiffCategory::Directory: return L"Data Directory";
		case DiffCategory::Section: return L"Section";
		case DiffCategory::Import: return L"Import";
		case DiffCategory::Export: return L"Export";
		case DiffCategory::Resource: return L"Resource";
		case DiffCategory::Version: return L"Version";
		case DiffCategory::Debug: return L"Debug";
	}
	return L"";
}
