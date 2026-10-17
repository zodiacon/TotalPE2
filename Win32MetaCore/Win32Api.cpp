#include "pch.h"
#include "Win32Api.h"
#include "CombinedMetadata.h"
#include <mutex>

using Kind = GhidraDecompiler::TypeKind;

struct Win32Api::Data {
	CombinedMetadata Md;	// Windows.Win32.winmd, and Windows.Wdk.winmd if it is there
	std::unordered_map<std::string, std::vector<uint32_t>> Functions;	// MethodDef rows, by the name they are exported by
	std::unordered_map<std::string, std::vector<uint32_t>> Types;		// TypeDef rows, by their names
	std::unordered_map<uint32_t, std::vector<std::pair<std::string, uint64_t>>> Constants;	// of a type (HRESULT...), by its row
	bool ConstantsRead{ false };
};

namespace {
	std::string Narrow(std::wstring const& text) {
		std::string result;
		result.reserve(text.size());
		for (auto ch : text)
			result += (char)ch;		// (the names of the metadata are ASCII)
		return result;
	}

	std::string Lower(std::string text) {
		for (auto& ch : text)
			ch = (char)tolower((unsigned char)ch);
		return text;
	}

	// a DLL's name without its extension, in lowercase: "KERNEL32.dll" -> "kernel32"
	std::string ModuleKey(std::string const& module) {
		auto key = Lower(module);
		if (key.ends_with(".dll"))
			key.resize(key.size() - 4);
		return key;
	}

	uint32_t AlignUp(uint32_t value, uint32_t alignment) {
		return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
	}

	// a namespace as the entries show it: "System.Threading" (of "Windows.Win32.System.Threading"), "Wdk.Foundation" (of
	// "Windows.Wdk.Foundation")
	std::string ShortNamespace(std::string ns) {
		if (ns.starts_with("Windows.Win32."))
			ns = ns.substr(14);
		else if (ns.starts_with("Windows."))
			ns = ns.substr(8);
		return ns;
	}
}

namespace {
	std::wstring s_MetadataFolder;
}

void Win32Api::SetMetadataFolder(std::wstring folder) {
	if (!folder.empty() && !folder.ends_with(L'\\'))
		folder += L'\\';
	s_MetadataFolder = std::move(folder);
}

std::wstring Win32Api::MetadataPath() {
	if (!s_MetadataFolder.empty())
		return s_MetadataFolder + L"Windows.Win32.winmd";
	WCHAR path[MAX_PATH];
	::GetModuleFileNameW(nullptr, path, _countof(path));
	std::wstring folder = path;
	return folder.substr(0, folder.find_last_of(L'\\') + 1) + L"Windows.Win32.winmd";
}

std::wstring Win32Api::WdkMetadataPath() {
	auto path = MetadataPath();
	return path.substr(0, path.find_last_of(L'\\') + 1) + L"Windows.Wdk.winmd";
}

Win32Api::Data* Win32Api::Shared() {
	static std::mutex lock;
	static std::unique_ptr<Data> data;
	static bool tried = false;
	std::lock_guard guard(lock);
	if (tried)
		return data.get();
	tried = true;
	auto path = MetadataPath();
	if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return nullptr;
	auto loaded = std::make_unique<Data>();
	if (!loaded->Md.Add(path))
		return nullptr;
	// the kernel's (the WDK's), if it is there: after Windows.Win32's (whose types it uses)
	if (auto wdk = WdkMetadataPath(); ::GetFileAttributesW(wdk.c_str()) != INVALID_FILE_ATTRIBUTES)
		loaded->Md.Add(wdk);
	auto& md = loaded->Md;
	// the functions (those of DLLs: P/Invoke), by the names the DLLs export them by
	for (uint32_t m = 1; m <= md.Methods().size(); m++)
		if (auto native = md.PInvoke(m))
			loaded->Functions[Narrow(native->EntryPoint.empty() ? md.Methods()[m - 1].Name : native->EntryPoint)].push_back(m);
	for (uint32_t t = 1; t <= md.Types().size(); t++)
		loaded->Types[Narrow(md.Types()[t - 1].Name)].push_back(t);
	data = std::move(loaded);
	return data.get();
}

std::unique_ptr<Win32Api>* Win32Api::Instances() {
	static std::unique_ptr<Win32Api> apis[3];
	return apis;
}

namespace {
	std::vector<std::string> s_Preferred;
	// the names code uses, of those that a value has (the others are older, or of other uses)
	char const* const s_Common[] = {
		"MB_ICONERROR", "MB_ICONWARNING", "MB_ICONINFORMATION", "SW_SHOWNORMAL", "SW_SHOWMAXIMIZED", "ERROR_SUCCESS",
		"STATUS_SUCCESS", "WAIT_OBJECT_0", "IDOK", "HWND_TOP", "GENERIC_ALL",
	};
}

std::vector<std::string> const& Win32Api::PreferredNames() {
	return s_Preferred;
}

int Win32Api::NameRank(std::string const& name) {
	if (std::ranges::find(s_Preferred, name) != s_Preferred.end())
		return 0;
	for (auto common : s_Common)
		if (name == common)
			return 1;
	return 2;
}

// by value, and of a value's names the preferred ones first (the decompiler shows the first)
void Win32Api::OrderNames(std::vector<std::pair<std::string, uint64_t>>& values) {
	std::stable_sort(values.begin(), values.end(), [](auto const& a, auto const& b) {
		return a.second != b.second ? a.second < b.second : NameRank(a.first) < NameRank(b.first);
	});
}

void Win32Api::SetPreferredNames(std::vector<std::string> names) {
	s_Preferred = std::move(names);
	for (int i = 0; i < 3; i++)
		if (auto& api = Instances()[i])
			std::erase_if(api->m_Described, [](auto const& entry) { return entry.second.Kind == Kind::Enum; });
}

std::optional<Win32Api::Constant> Win32Api::ConstantOf(std::string const& name) {
	if (m_ConstantTypes.empty()) {
		// the constants of the enums, and those of HRESULT and NTSTATUS (they are read when the types are described)
		auto& md = m_Data.Md;
		for (auto const& e : Entries())
			if (e.Kind == Entry::Kinds::Enum)
				if (auto info = md.EnumInfo((ClrTableId::TypeDef << 24) | e.Row))
					for (auto const& v : info->Values)
						m_ConstantTypes.emplace(Narrow(v.Name), IdBit | e.Row);
		GhidraDecompiler::Type type;
		for (auto typeName : { "HRESULT", "NTSTATUS" })
			if (auto id = TypeByName(typeName); id && Describe(id, type) && type.Kind == Kind::Enum)
				for (auto const& v : type.Values)
					m_ConstantTypes.emplace(v.first, id);
	}
	auto it = m_ConstantTypes.find(name);
	GhidraDecompiler::Type type;
	if (it == m_ConstantTypes.end() || !Describe(it->second, type))
		return std::nullopt;
	auto value = std::ranges::find(type.Values, name, &std::pair<std::string, uint64_t>::first);
	if (value == type.Values.end())
		return std::nullopt;
	Constant constant{ type.Name, value->second };
	for (auto const& [n, v] : type.Values)
		if (v == constant.Value)
			constant.Names.push_back(n);
	return constant;
}

Win32Api* Win32Api::Get(Machine machine) {
	auto& api = Instances()[(int)machine];
	if (!api)
		if (auto data = Shared())
			api.reset(new Win32Api(*data, machine));
	return api.get();
}

Win32Api::Win32Api(Data& data, Machine machine) : m_Data(data), m_Machine(machine), m_Pointer(machine == Machine::X86 ? 4 : 8) {
}

// Of the machine: a type or function without [SupportedArchitecture], or with this machine in it
bool Win32Api::HasAttribute(uint32_t token, std::wstring_view name) {
	for (auto type : m_Data.Md.CustomAttributeTypes(token)) {
		auto it = m_AttributeTypeNames.find(type);
		if (it == m_AttributeTypeNames.end())
			it = m_AttributeTypeNames.emplace(type, m_Data.Md.TypeName(type)).first;
		if (it->second.ends_with(name))
			return true;
	}
	return false;
}

std::wstring const& Win32Api::TypeKindOf(uint32_t row) {
	static std::wstring const interface_ = L"interface", class_ = L"class";
	auto const& types = m_Data.Md.Types();
	if (row == 0 || row > types.size())
		return class_;
	auto const& t = types[row - 1];
	if (t.Flags & 0x20)
		return interface_;
	if (!t.Extends)
		return class_;
	auto it = m_KindsOfBases.find(t.Extends);
	if (it == m_KindsOfBases.end())
		it = m_KindsOfBases.emplace(t.Extends, m_Data.Md.TypeKind(row)).first;
	return it->second;
}

bool Win32Api::OfMachine(uint32_t token) {
	// (most have no [SupportedArchitecture]: they are of all, without decoding their attributes)
	if (!HasAttribute(token, L"SupportedArchitectureAttribute"))
		return true;
	auto& md = m_Data.Md;
	for (auto const& attribute : md.CustomAttributes(token)) {
		auto name = md.TypeName(attribute.AttributeType);
		if (!name.ends_with(L"SupportedArchitectureAttribute"))
			continue;
		auto text = md.AttributeCSharp(attribute);
		auto wanted = m_Machine == Machine::X86 ? L"X86" : m_Machine == Machine::X64 ? L"X64" : L"Arm64";
		// the value, as a number if the enum was not found (X86 = 1, X64 = 2, Arm64 = 4)
		if (text.find(wanted) != std::wstring::npos)
			return true;
		auto open = text.find(L'(');
		if (open != std::wstring::npos) {
			auto digits = text.find_first_of(L"0123456789", open);
			if (digits != std::wstring::npos) {
				auto value = _wtoi(text.c_str() + digits);
				int bit = m_Machine == Machine::X86 ? 1 : m_Machine == Machine::X64 ? 2 : 4;
				return (value & bit) != 0;
			}
		}
		return false;
	}
	return true;
}

// The definition of a type for the machine (CONTEXT is defined for each)
uint32_t Win32Api::ForMachine(uint32_t row) {
	auto& md = m_Data.Md;
	auto const& type = md.Types()[row - 1];
	auto it = m_Data.Types.find(Narrow(type.Name));
	if (it == m_Data.Types.end() || it->second.size() < 2 || OfMachine((ClrTableId::TypeDef << 24) | row))
		return row;
	for (auto other : it->second) {
		auto const& t = md.Types()[other - 1];
		if (other != row && t.Namespace == type.Namespace && t.Enclosing == 0 && type.Enclosing == 0 && OfMachine((ClrTableId::TypeDef << 24) | other))
			return other;
	}
	return row;
}

// A type of the metadata (a TypeDef row) from a TypeDef or TypeRef token; 0 if it is of another assembly
uint32_t Win32Api::RowOf(uint32_t token) {
	auto [md, row] = m_Data.Md.ResolveType(token);
	if (md != &m_Data.Md || row == 0)
		return 0;
	return ForMachine(row);
}

std::string Win32Api::Name(uint32_t row) {
	auto& md = m_Data.Md;
	auto const& type = md.Types()[row - 1];
	auto name = Narrow(type.Name);
	// a nested type (an anonymous union): named with the type it is in ("_Anonymous_e__Union" of SYSTEM_INFO:
	// SYSTEM_INFO_Anonymous)
	if (type.Enclosing) {
		for (auto suffix : { "_e__Union", "_e__Struct" })
			if (name.ends_with(suffix))
				name.resize(name.size() - strlen(suffix));
		name = Name(type.Enclosing) + "_" + (name.starts_with("_") ? name.substr(1) : name);
	}
	return name;
}

uint32_t Win32Api::Made(std::string const& key, GhidraDecompiler::Type type) {
	if (auto it = m_MadeKeys.find(key); it != m_MadeKeys.end())
		return it->second;
	auto id = IdBit | MadeBit | (uint32_t)m_Made.size();
	m_Made.push_back(std::move(type));
	m_MadeKeys[key] = id;
	return id;
}

uint32_t Win32Api::Primitive(Kind kind, uint32_t size, bool isSigned, char const* name) {
	GhidraDecompiler::Type type{ kind, name ? name : "", size, isSigned };
	return Made(std::format("p{}:{}:{}:{}", (int)kind, size, isSigned, name ? name : ""), type);
}

uint32_t Win32Api::Pointer(uint32_t target) {
	GhidraDecompiler::Type type{ Kind::Pointer, "", m_Pointer };
	type.Target = target;
	return Made(std::format("*{}", target), type);
}

// The decompiler's id of a type of a signature
uint32_t Win32Api::TypeId(ClrSigType const& type) {
	switch (type.Element) {
		case 0x01: return 0;	// void
		case 0x02: return Primitive(Kind::Boolean, 1, false);
		case 0x03: return Primitive(Kind::Char, 2, false);
		case 0x04: return Primitive(Kind::Integer, 1, true);
		case 0x05: return Primitive(Kind::Integer, 1, false);
		case 0x06: return Primitive(Kind::Integer, 2, true);
		case 0x07: return Primitive(Kind::Integer, 2, false);
		case 0x08: return Primitive(Kind::Integer, 4, true);
		case 0x09: return Primitive(Kind::Integer, 4, false);
		case 0x0A: return Primitive(Kind::Integer, 8, true);
		case 0x0B: return Primitive(Kind::Integer, 8, false);
		case 0x0C: return Primitive(Kind::Float, 4, true);
		case 0x0D: return Primitive(Kind::Float, 8, true);
		case 0x18: return Primitive(Kind::Integer, m_Pointer, true);
		case 0x19: return Primitive(Kind::Integer, m_Pointer, false);
		case 0x0F: case 0x10: case 0x1D:
			return Pointer(type.Inner.empty() ? 0 : TypeId(type.Inner[0]));
		case 0x1C:
			return Pointer(0);	// object
		case 0x14:
		{
			if (type.Inner.empty() || type.Count == 0)
				return Pointer(0);
			GhidraDecompiler::Type array{ Kind::Array };
			array.Target = TypeId(type.Inner[0]);
			array.Count = type.Count;
			array.Size = Layout(array.Target).first * type.Count;
			return Made(std::format("[{}]{}", type.Count, array.Target), array);
		}
		case 0x11: case 0x12:
		{
			auto row = RowOf(type.Token);
			if (!row) {
				// of another assembly: a GUID, or bytes of a pointer
				auto name = m_Data.Md.TypeName(type.Token);
				if (name.ends_with(L"System.Guid"))
					return GuidType();
				return Pointer(0);
			}
			// an interface (COM) is used through a pointer
			auto kind = TypeKindOf(row);
			if (kind == L"interface")
				return Pointer(IdBit | row);
			return IdBit | row;
		}
	}
	return Primitive(Kind::Unknown, m_Pointer, false);
}

bool Win32Api::Describe(uint32_t id, GhidraDecompiler::Type& type) {
	if (!IsId(id))
		return false;
	if (id & MadeBit) {
		auto index = id & ~(IdBit | MadeBit);
		if (index >= m_Made.size())
			return false;
		type = m_Made[index];
		return true;
	}
	if (auto it = m_Described.find(id); it != m_Described.end()) {
		type = it->second;
		return true;
	}
	auto row = id & ~IdBit;
	if (row == 0 || row > m_Data.Md.Types().size() || !DescribeRow(row, type))
		return false;
	m_Described[id] = type;
	return true;
}

bool Win32Api::DescribeRow(uint32_t row, GhidraDecompiler::Type& type) {
	auto& md = m_Data.Md;
	auto token = (ClrTableId::TypeDef << 24) | row;
	auto kind = TypeKindOf(row);
	auto name = Name(row);
	type = {};
	type.Name = name;

	if (kind == L"enum") {
		auto info = md.EnumInfo(token);
		if (!info)
			return false;
		static const uint32_t sizes[] = { 1, 1, 2, 2, 4, 4, 8, 8 };	// int8... uint64 (0x04 to 0x0B)
		type.Kind = Kind::Enum;
		type.Size = info->Underlying >= 0x04 && info->Underlying <= 0x0B ? sizes[info->Underlying - 0x04] : 4;
		type.Signed = info->Underlying == 0x04 || info->Underlying == 0x06 || info->Underlying == 0x08 || info->Underlying == 0x0A;
		for (auto const& v : info->Values)
			type.Values.emplace_back(Narrow(v.Name), (uint64_t)v.Value);
		OrderNames(type.Values);
		type.Exact = !info->Flags;
		return true;
	}
	if (kind == L"delegate") {
		// a callback: a pointer to a function of the delegate's Invoke
		for (auto m : md.MethodsOf(row)) {
			if (md.Methods()[m - 1].Name != L"Invoke")
				continue;
			ClrSigType result;
			std::vector<ClrSigType> params;
			if (!md.MethodSigTypes(m, result, params))
				return false;
			GhidraDecompiler::Type function{ Kind::Function };
			function.Target = TypeId(result);
			for (auto& p : params)
				function.Parameters.push_back(TypeId(p));
			auto fid = Made("f" + name, function);
			type.Kind = Kind::Pointer;
			type.Size = m_Pointer;
			type.Target = fid;
			type.Name.clear();
			return true;
		}
		return false;
	}
	if (kind == L"interface") {
		// what it is to C: a struct that starts with the pointer to its functions
		type.Kind = Kind::Struct;
		type.Size = m_Pointer;
		type.Fields = { { "lpVtbl", 0, Pointer(Vtable(row)) } };
		m_Alignment[IdBit | row] = m_Pointer;
		return true;
	}
	if (kind != L"struct")
		return false;

	std::vector<uint32_t> fields;
	for (auto f : md.FieldsOf(row))
		if ((md.Fields()[f - 1].Flags & 0x10) == 0)
			fields.push_back(f);

	// a typedef (HANDLE, PWSTR, BOOL): of its value
	bool typedefOf = HasAttribute(token, L"NativeTypedefAttribute");
	if (typedefOf && fields.size() == 1) {
		ClrSigType value;
		if (!md.FieldSigType(fields[0], value))
			return false;
		// HRESULT, NTSTATUS: their values have names (the constants of the type)
		if (name == "HRESULT" || name == "NTSTATUS") {
			if (!m_Data.ConstantsRead) {
				m_Data.ConstantsRead = true;
				std::set<uint32_t> wanted;
				for (auto n : { "HRESULT", "NTSTATUS" })
					if (auto it = m_Data.Types.find(n); it != m_Data.Types.end())
						wanted.insert(it->second.begin(), it->second.end());
				for (auto apis : m_Data.Types["Apis"])
					for (auto f : md.FieldsOf(apis)) {
						ClrSigType t;
						if ((md.Fields()[f - 1].Flags & 0x40) == 0 || !md.FieldSigType(f, t) || t.Element != 0x11)
							continue;
						auto [owner, typeRow] = md.ResolveType(t.Token);
						if (owner != &md || !wanted.contains(typeRow))
							continue;
						if (auto v = md.ConstantInteger((ClrTableId::Field << 24) | f))
							m_Data.Constants[typeRow].emplace_back(Narrow(md.Fields()[f - 1].Name), (uint64_t)(uint32_t)*v);
					}
			}
			if (auto it = m_Data.Constants.find(row); it != m_Data.Constants.end() && !it->second.empty()) {
				// of the names of a value, the one code uses (the decompiler shows the first): S_OK rather than SEC_E_OK
				auto rank = [](std::string const& n) {
					bool usual = n.starts_with("S_") || n.starts_with("E_") || n.starts_with("STATUS_");
					return std::pair<int, size_t>(usual ? 0 : 1, n.size());
				};
				std::stable_sort(it->second.begin(), it->second.end(), [&](auto const& a, auto const& b) {
					return a.second != b.second ? a.second < b.second : rank(a.first) < rank(b.first);
				});
				OrderNames(it->second);	// (the user's choices first)
				type.Kind = Kind::Enum;
				type.Size = 4;
				type.Signed = true;
				type.Values = it->second;
				type.Exact = true;
				return true;
			}
		}
		type.Kind = Kind::Typedef;
		type.Target = TypeId(value);
		return true;
	}

	// a struct or union: its fields where the metadata puts them, or one after the other (each aligned, up to the packing)
	auto [packing, classSize] = md.ClassLayout(row);
	uint32_t pack = packing ? packing : 8;
	bool explicitLayout = (md.Types()[row - 1].Flags & 0x18) == 0x10;
	bool isUnion = explicitLayout;
	uint32_t offset = 0, end = 0, alignment = 1;
	m_Laying.insert(row);
	for (auto f : fields) {
		ClrSigType t;
		if (!md.FieldSigType(f, t)) {
			m_Laying.erase(row);
			return false;
		}
		auto fid = TypeId(t);
		auto [size, align] = Layout(fid);
		align = std::min(std::max(align, 1u), pack);
		uint32_t at;
		if (explicitLayout) {
			at = md.FieldOffset(f).value_or(0);
			isUnion &= at == 0;
		}
		else
			at = AlignUp(offset, align);
		type.Fields.push_back({ Narrow(md.Fields()[f - 1].Name), at, fid });
		offset = at + size;
		end = std::max(end, offset);
		alignment = std::max(alignment, align);
	}
	m_Laying.erase(row);
	type.Kind = isUnion && !fields.empty() ? Kind::Union : Kind::Struct;
	type.Size = std::max(AlignUp(end, alignment), classSize);
	if (type.Size == 0)
		type.Size = 1;
	m_Alignment[IdBit | row] = alignment;
	return true;
}

uint32_t Win32Api::GuidType() {
	GhidraDecompiler::Type guid{ Kind::Struct, "GUID", 16 };
	auto bytes = TypeId({ 0x14, 0, 8, { { 0x05 } } });
	guid.Fields = { { "Data1", 0, Primitive(Kind::Integer, 4, false) }, { "Data2", 4, Primitive(Kind::Integer, 2, false) },
		{ "Data3", 6, Primitive(Kind::Integer, 2, false) }, { "Data4", 8, bytes } };
	auto id = Made("GUID", guid);
	m_Alignment[id] = 4;
	return id;
}

uint32_t Win32Api::Vtable(uint32_t row) {
	auto& md = m_Data.Md;
	auto name = Name(row) + "Vtbl";
	if (auto it = m_MadeKeys.find("vtbl:" + name); it != m_MadeKeys.end())
		return it->second;
	std::vector<uint32_t> methods;
	std::set<uint32_t> seen;
	auto collect = [&](auto&& self, uint32_t r) -> void {
		if (!seen.insert(r).second)
			return;
		for (auto token : md.Interfaces(r))
			if (auto base = RowOf(token))
				self(self, base);
		for (auto m : md.MethodsOf(r))
			methods.push_back(m);
	};
	collect(collect, row);
	GhidraDecompiler::Type vtable{ Kind::Struct, name, (uint32_t)methods.size() * m_Pointer };
	std::set<std::string> names;
	for (size_t i = 0; i < methods.size(); i++) {
		auto m = methods[i];
		uint32_t function = 0;
		ClrSigType result;
		std::vector<ClrSigType> params;
		if (md.MethodSigTypes(m, result, params)) {
			GhidraDecompiler::Type f{ Kind::Function };
			f.Target = TypeId(result);
			f.Parameters.push_back(Pointer(IdBit | row));
			for (auto& p : params)
				f.Parameters.push_back(TypeId(p));
			function = Made(std::format("f{}{}", name, i), f);
		}
		auto method = Narrow(md.Methods()[m - 1].Name);
		if (!names.insert(method).second)
			method += std::format("_{}", i);	// (overloads)
		vtable.Fields.push_back({ method, (uint32_t)i * m_Pointer, Pointer(function) });
	}
	auto id = Made("vtbl:" + name, vtable);
	m_Alignment[id] = m_Pointer;
	return id;
}

bool Win32Api::InterfaceMethods(std::string const& name, std::vector<Method>& methods) {
	methods.clear();
	auto entry = Find(name);
	if (!entry || entry->Kind != Entry::Kinds::Interface)
		return false;
	auto& md = m_Data.Md;
	// (as the vtable has them: the bases' first; its functions' types are those it made)
	auto vtable = Vtable(entry->Row);
	std::vector<uint32_t> rows;
	std::set<uint32_t> seen;
	auto collect = [&](auto&& self, uint32_t r) -> void {
		if (!seen.insert(r).second)
			return;
		for (auto token : md.Interfaces(r))
			if (auto base = RowOf(token))
				self(self, base);
		for (auto m : md.MethodsOf(r))
			rows.push_back(m);
	};
	collect(collect, entry->Row);
	GhidraDecompiler::Type type;
	if (!Describe(vtable, type) || type.Fields.size() != rows.size())
		return false;
	for (size_t i = 0; i < rows.size(); i++) {
		Method method{ type.Fields[i].Name };
		if (auto made = m_MadeKeys.find(std::format("f{}Vtbl{}", Name(entry->Row), i)); made != m_MadeKeys.end()) {
			method.Type = made->second;
			ClrSigType result;
			std::vector<ClrSigType> params;
			method.Parameters.push_back("This");
			if (md.MethodSigTypes(rows[i], result, params))
				for (size_t k = 0; k < params.size(); k++)
					method.Parameters.push_back(Narrow(md.ParamName(rows[i], (uint32_t)k + 1)));
		}
		methods.push_back(std::move(method));
	}
	return !methods.empty();
}

std::unordered_map<std::string, Win32Api::NamedGuid> const& Win32Api::Guids() {
	if (m_GuidsRead)
		return m_Guids;
	m_GuidsRead = true;
	auto& md = m_Data.Md;
	// [Guid(uint, ushort, ushort, byte x 8)]: after the prolog, the bytes as they are in memory
	auto guidOf = [&](uint32_t token, std::string& bytes) {
		if (!HasAttribute(token, L"GuidAttribute"))
			return false;
		for (auto const& attribute : md.CustomAttributes(token))
			if (attribute.Value.size() >= 18 && md.TypeName(attribute.AttributeType).ends_with(L"GuidAttribute")) {
				bytes.assign((char const*)attribute.Value.data() + 2, 16);
				return bytes != std::string(16, '\0');
			}
		return false;
	};
	std::string bytes;
	// the interfaces first (an IID names its interface), then the classes, then the constants
	for (int pass = 0; pass < 2; pass++)
		for (uint32_t t = 1; t <= md.Types().size(); t++) {
			auto kind = TypeKindOf(t);
			if ((pass == 0) != (kind == L"interface") || (pass == 1 && kind != L"struct"))
				continue;
			if (guidOf((ClrTableId::TypeDef << 24) | t, bytes))
				m_Guids.try_emplace(bytes, NamedGuid{ (pass == 0 ? "IID_" : "CLSID_") + Narrow(md.Types()[t - 1].Name), pass == 0 ? IdBit | t : 0 });
		}
	for (auto apis : m_Data.Types["Apis"])
		for (auto f : md.FieldsOf(apis))
			if (guidOf((ClrTableId::Field << 24) | f, bytes))
				m_Guids.try_emplace(bytes, NamedGuid{ Narrow(md.Fields()[f - 1].Name) });
	return m_Guids;
}

std::pair<uint32_t, uint32_t> Win32Api::Layout(uint32_t id, int depth) {
	if (id == 0 || depth > 32)
		return { 0, 1 };
	if (IsId(id) && !(id & MadeBit) && m_Laying.contains(id & ~IdBit))
		return { 0, 1 };	// (a struct within itself: not a struct C has)
	GhidraDecompiler::Type type;
	if (!Describe(id, type))
		return { m_Pointer, m_Pointer };
	switch (type.Kind) {
		case Kind::Pointer: return { m_Pointer, m_Pointer };
		case Kind::Typedef: return Layout(type.Target, depth + 1);
		case Kind::Array:
		{
			auto [size, align] = Layout(type.Target, depth + 1);
			return { size * type.Count, align };
		}
		case Kind::Struct: case Kind::Union:
		{
			auto it = m_Alignment.find(id);
			return { type.Size, it == m_Alignment.end() ? 1 : it->second };
		}
		case Kind::Function: case Kind::Void: return { 0, 1 };
		default:
			return { type.Size, std::max(type.Size, 1u) };
	}
}

bool Win32Api::Function(std::string const& name, std::string const& module, uint32_t& type, std::vector<std::string>& names, bool onlyOf) {
	auto it = m_Data.Functions.find(name);
	if (it == m_Data.Functions.end())
		return false;
	auto& md = m_Data.Md;
	// of the DLL (an API set's is another's: then the first that is of the machine)
	uint32_t method = 0;
	auto key = ModuleKey(module);
	for (auto m : it->second) {
		if (!OfMachine((ClrTableId::MethodDef << 24) | m))
			continue;
		auto native = md.PInvoke(m);
		bool ofModule = native && ModuleKey(Narrow(native->Module)) == key;
		if ((!method && !onlyOf) || ofModule)
			method = m;
	}
	if (!method)
		return false;
	ClrSigType result;
	std::vector<ClrSigType> params;
	if (!md.MethodSigTypes(method, result, params))
		return false;
	GhidraDecompiler::Type function{ Kind::Function };
	function.Target = TypeId(result);
	names.clear();
	for (size_t i = 0; i < params.size(); i++) {
		function.Parameters.push_back(TypeId(params[i]));
		names.push_back(Narrow(md.ParamName(method, (uint32_t)i + 1)));
	}
	type = Made(std::format("m{}", method), function);
	return true;
}

uint32_t Win32Api::TypeByName(std::string const& name) {
	auto it = m_Data.Types.find(name);
	if (it == m_Data.Types.end())
		return 0;
	for (auto row : it->second) {
		auto& t = m_Data.Md.Types()[row - 1];
		auto kind = TypeKindOf(row);
		if (t.Enclosing == 0 && kind != L"class" && OfMachine((ClrTableId::TypeDef << 24) | row))
			return kind == L"interface" ? IdBit | row : IdBit | row;
	}
	return 0;
}

//
// What the metadata has, to look at
//

PCWSTR Win32Api::KindName(Entry::Kinds kind) {
	static PCWSTR const names[] = { L"Function", L"Enum", L"Struct", L"Union", L"Typedef", L"Callback", L"Interface", L"Constant" };
	return names[(int)kind];
}

std::vector<Win32Api::Entry> const& Win32Api::Entries() {
	if (!m_Entries.empty())
		return m_Entries;
	auto& md = m_Data.Md;
	for (auto const& [name, methods] : m_Data.Functions)
		for (auto m : methods) {
			if (!OfMachine((ClrTableId::MethodDef << 24) | m))
				continue;
			auto native = md.PInvoke(m);
			auto module = native ? Narrow(native->Module) : std::string();
			auto owner = md.Methods()[m - 1].Type;
			auto ns = owner ? Narrow(md.Types()[owner - 1].Namespace) : std::string();
			// one that is exported by its ordinal only ("#660"): by the metadata's name, with the ordinal
			if (name.starts_with("#"))
				m_Entries.push_back({ Narrow(md.Methods()[m - 1].Name), Entry::Kinds::Function, module + " " + name, ns, m });
			else
				m_Entries.push_back({ name, Entry::Kinds::Function, module, ns, m });
		}
	for (uint32_t row = 1; row <= md.Types().size(); row++) {
		auto const& t = md.Types()[row - 1];
		if (t.Enclosing)
			continue;
		auto kind = TypeKindOf(row);
		Entry::Kinds entry;
		if (kind == L"enum")
			entry = Entry::Kinds::Enum;
		else if (kind == L"delegate")
			entry = Entry::Kinds::Callback;
		else if (kind == L"interface")
			entry = Entry::Kinds::Interface;
		else if (kind == L"struct") {
			bool typedefOf = HasAttribute((ClrTableId::TypeDef << 24) | row, L"NativeTypedefAttribute");
			entry = typedefOf ? Entry::Kinds::Typedef : (t.Flags & 0x18) == 0x10 ? Entry::Kinds::Union : Entry::Kinds::Struct;
		}
		else
			continue;	// the classes of the functions and constants, attributes
		if (ForMachine(row) != row)
			continue;	// (a type defined for another machine)
		auto ns = Narrow(t.Namespace);
		m_Entries.push_back({ Narrow(t.Name), entry, ShortNamespace(ns), ns, row });
	}
	// the constants: the static fields of the classes of the functions and constants (after the functions and types: of
	// those of a name, Find has the function or type)
	if (auto apis = m_Data.Types.find("Apis"); apis != m_Data.Types.end())
		for (auto row : apis->second) {
			auto ns = Narrow(md.Types()[row - 1].Namespace);
			auto where = ShortNamespace(ns);
			for (auto f : md.FieldsOf(row))
				if ((md.Fields()[f - 1].Flags & 0x10) && OfMachine((ClrTableId::Field << 24) | f))
					m_Entries.push_back({ Narrow(md.Fields()[f - 1].Name), Entry::Kinds::Constant, where, ns, f });
		}
	// by name, not case sensitive (as _stricmp compares: in lowercase); of the same name, in the order they were added. The
	// names in lowercase sorted, then the entries moved once.
	{
		std::vector<std::string> keys;
		keys.reserve(m_Entries.size());
		for (auto const& e : m_Entries) {
			std::string key(e.Name);
			for (auto& c : key)
				c = (char)tolower((unsigned char)c);
			keys.push_back(std::move(key));
		}
		std::vector<uint32_t> order(m_Entries.size());
		for (uint32_t i = 0; i < order.size(); i++)
			order[i] = i;
		std::ranges::stable_sort(order, [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });
		std::vector<Entry> sorted;
		sorted.reserve(m_Entries.size());
		for (auto i : order)
			sorted.push_back(std::move(m_Entries[i]));
		m_Entries = std::move(sorted);
	}
	for (size_t i = 0; i < m_Entries.size(); i++)
		m_EntryIndex.try_emplace(m_Entries[i].Name, i);
	return m_Entries;
}

Win32Api::Entry const* Win32Api::Find(std::string const& name) {
	Entries();
	auto it = m_EntryIndex.find(name);
	return it == m_EntryIndex.end() ? nullptr : &m_Entries[it->second];
}

Win32Api::Namespace const& Win32Api::Namespaces() {
	if (m_NamespacesBuilt)
		return m_Namespaces;
	m_NamespacesBuilt = true;
	auto const& entries = Entries();
	// (the entries are by name: so is each namespace's)
	for (size_t i = 0; i < entries.size(); i++) {
		auto node = &m_Namespaces;
		std::string_view rest = entries[i].Namespace;
		while (!rest.empty()) {
			auto dot = rest.find('.');
			auto part = rest.substr(0, dot);
			auto child = std::ranges::find(node->Children, part, &Namespace::Name);
			if (child == node->Children.end()) {
				auto full = node->FullName.empty() ? std::string(part) : node->FullName + "." + std::string(part);
				node->Children.push_back({ std::string(part), std::move(full) });
				child = node->Children.end() - 1;
			}
			node = &*child;
			rest = dot == std::string_view::npos ? std::string_view() : rest.substr(dot + 1);
		}
		node->Entries.push_back(i);
	}
	auto sort = [](auto&& self, Namespace& ns) -> void {
		std::ranges::sort(ns.Children, [](auto const& a, auto const& b) { return _stricmp(a.Name.c_str(), b.Name.c_str()) < 0; });
		for (auto& child : ns.Children)
			self(self, child);
	};
	sort(sort, m_Namespaces);
	return m_Namespaces;
}

std::vector<size_t> const& Win32Api::UsedBy(size_t entry) {
	static std::vector<size_t> const none;
	if (m_UsedBy.empty())
		BuildUsedBy();
	return entry < m_UsedBy.size() ? m_UsedBy[entry] : none;
}

// For each type, who uses it: from the signatures of the functions and methods and the types of the fields, the types they
// name (through pointers and arrays; a nested type is its outermost one's)
void Win32Api::BuildUsedBy() {
	auto const& entries = Entries();
	auto& md = m_Data.Md;
	auto const& types = md.Types();
	m_UsedBy.assign(entries.size(), {});
	std::unordered_map<uint32_t, size_t> typeEntries;	// by TypeDef row
	for (size_t i = 0; i < entries.size(); i++)
		if (entries[i].Kind != Entry::Kinds::Function && entries[i].Kind != Entry::Kinds::Constant)
			typeEntries.try_emplace(entries[i].Row, i);
	std::unordered_map<uint32_t, std::vector<uint32_t>> nested;
	for (uint32_t row = 1; row <= types.size(); row++)
		if (types[row - 1].Enclosing)
			nested[types[row - 1].Enclosing].push_back(row);

	auto useType = [&](size_t user, uint32_t row) {
		for (int up = 0; up < 16 && row && types[row - 1].Enclosing; up++)
			row = types[row - 1].Enclosing;
		if (auto it = typeEntries.find(row); it != typeEntries.end() && it->second != user)
			m_UsedBy[it->second].push_back(user);
	};
	auto use = [&](auto&& self, size_t user, ClrSigType const& type) -> void {
		if (type.Element == 0x11 || type.Element == 0x12) {
			if (auto row = RowOf(type.Token))
				useType(user, row);
		}
		for (auto const& inner : type.Inner)
			self(self, user, inner);
	};
	auto useMethod = [&](size_t user, uint32_t method) {
		ClrSigType result;
		std::vector<ClrSigType> params;
		if (!md.MethodSigTypes(method, result, params))
			return;
		use(use, user, result);
		for (auto const& p : params)
			use(use, user, p);
	};
	auto useFields = [&](auto&& self, size_t user, uint32_t row) -> void {
		for (auto f : md.FieldsOf(row)) {
			ClrSigType type;
			if ((md.Fields()[f - 1].Flags & 0x10) == 0 && md.FieldSigType(f, type))
				use(use, user, type);
		}
		if (auto it = nested.find(row); it != nested.end())
			for (auto inner : it->second)
				self(self, user, inner);
	};

	for (size_t i = 0; i < entries.size(); i++) {
		auto const& e = entries[i];
		switch (e.Kind) {
			case Entry::Kinds::Function:
				useMethod(i, e.Row);
				break;
			case Entry::Kinds::Callback: case Entry::Kinds::Interface:
				for (auto m : md.MethodsOf(e.Row))
					useMethod(i, m);
				for (auto token : md.Interfaces(e.Row))
					if (auto base = RowOf(token))
						useType(i, base);
				break;
			case Entry::Kinds::Struct: case Entry::Kinds::Union: case Entry::Kinds::Typedef:
				useFields(useFields, i, e.Row);
				break;
			case Entry::Kinds::Constant:
			{
				ClrSigType type;
				if (md.FieldSigType(e.Row, type))
					use(use, i, type);
				break;
			}
			default:
				break;
		}
	}
	// (the entries are by name: so are the users, by their indices)
	for (auto& users : m_UsedBy) {
		std::ranges::sort(users);
		users.erase(std::unique(users.begin(), users.end()), users.end());
	}
}

std::vector<Win32Api::NamedValue> Win32Api::FindValue(uint64_t value) {
	if (!m_ValuesBuilt) {
		m_ValuesBuilt = true;
		auto const& entries = Entries();
		auto& md = m_Data.Md;
		for (size_t i = 0; i < entries.size(); i++) {
			auto const& e = entries[i];
			if (e.Kind == Entry::Kinds::Constant) {
				if (auto v = md.ConstantInteger((ClrTableId::Field << 24) | e.Row))
					m_Values.push_back({ *v, { {}, i } });	// (a constant: the entry's name)
			}
			else if (e.Kind == Entry::Kinds::Enum) {
				if (auto info = md.EnumInfo((ClrTableId::TypeDef << 24) | e.Row))
					for (auto const& member : info->Values)
						m_Values.push_back({ member.Value, { Narrow(member.Name), i } });
			}
		}
		std::ranges::sort(m_Values, {}, &std::pair<int64_t, NamedValue>::first);
	}
	// the value, and what it is in fewer bits (signed and not)
	std::vector<int64_t> keys{ (int64_t)value };
	auto signedValue = (int64_t)value;
	for (int bits : { 32, 16, 8 }) {
		auto max = (int64_t)((1ULL << bits) - 1);
		auto min = -(int64_t)(1ULL << (bits - 1));
		if (signedValue < min || signedValue > max)
			continue;
		auto low = (uint64_t)value & (uint64_t)max;
		keys.push_back((int64_t)low);
		keys.push_back(low >> (bits - 1) ? (int64_t)low - (int64_t)(1ULL << bits) : (int64_t)low);
	}
	std::ranges::sort(keys);
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

	auto const& entries = Entries();
	std::vector<NamedValue> found;
	for (auto key : keys) {
		auto [first, last] = std::ranges::equal_range(m_Values, key, {}, &std::pair<int64_t, NamedValue>::first);
		for (auto it = first; it != last; ++it) {
			auto named = it->second;
			if (named.Name.empty())
				named.Name = entries[named.Entry].Name;
			found.push_back(std::move(named));
		}
	}
	std::ranges::sort(found, [](auto const& a, auto const& b) {
		auto c = _stricmp(a.Name.c_str(), b.Name.c_str());
		return c != 0 ? c < 0 : a.Entry < b.Entry;
	});
	found.erase(std::unique(found.begin(), found.end(), [](auto const& a, auto const& b) {
		return a.Name == b.Name && a.Entry == b.Entry;
	}), found.end());
	return found;
}

std::optional<Win32Api::NamedValue> Win32Api::FindGuid(GUID const& guid) {
	auto const& guids = Guids();
	auto it = guids.find(std::string((char const*)&guid, sizeof(guid)));
	if (it == guids.end())
		return std::nullopt;
	// IID_IUnknown: of IUnknown; CLSID_FileOpenDialog: of FileOpenDialog; a constant: itself
	auto const& name = it->second.Name;
	Entry const* entry = nullptr;
	for (auto prefix : { "IID_", "CLSID_" })
		if (!entry && name.starts_with(prefix))
			entry = Find(name.substr(strlen(prefix)));
	if (!entry)
		entry = Find(name);
	if (!entry)
		return std::nullopt;
	return NamedValue{ name, (size_t)(entry - Entries().data()) };
}

std::string Win32Api::TypeText(uint32_t id) {
	if (id == 0)
		return "void";
	GhidraDecompiler::Type type;
	if (!Describe(id, type))
		return "?";
	auto number = [&] {
		if (type.Kind == Kind::Float)
			return std::string(type.Size == 4 ? "float" : "double");
		if (type.Kind == Kind::Boolean)
			return std::string("bool");
		if (type.Kind == Kind::Char)
			return std::string(type.Size == 2 ? "wchar_t" : "char");
		if (type.Kind == Kind::Unknown)
			return std::format("undef{}", type.Size);
		static char const* const names[] = { "char", "short", "int", "longlong" };
		int index = type.Size == 1 ? 0 : type.Size == 2 ? 1 : type.Size == 4 ? 2 : 3;
		return std::string(type.Signed ? "" : "u") + names[index];
	};
	switch (type.Kind) {
		case Kind::Pointer:
		{
			// a callback is named by its delegate
			if (!(id & MadeBit))
				return Name(id & ~IdBit);
			auto target = TypeText(type.Target);
			return target + (target.ends_with("*") ? "*" : " *");
		}
		case Kind::Array:
			return std::format("{}[{}]", TypeText(type.Target), type.Count);
		case Kind::Function:
			return "function";
		case Kind::Struct: case Kind::Union: case Kind::Enum: case Kind::Typedef:
			return type.Name.empty() ? number() : type.Name;
		default:
			return number();
	}
}

std::string Win32Api::Referred(uint32_t id) {
	for (int depth = 0; id && depth < 16; depth++) {
		if (!(id & MadeBit)) {
			// (a nested type: the type it is in, which is listed)
			auto row = id & ~IdBit;
			auto const& types = m_Data.Md.Types();
			for (int up = 0; up < 16 && row && row <= types.size() && types[row - 1].Enclosing; up++)
				row = types[row - 1].Enclosing;
			return row && row <= types.size() ? Narrow(types[row - 1].Name) : std::string();
		}
		GhidraDecompiler::Type type;
		if (!Describe(id, type) || (type.Kind != Kind::Pointer && type.Kind != Kind::Array))
			return {};
		id = type.Target;
	}
	return {};
}

std::wstring Win32Api::DocumentationUrl(Entry const& entry) {
	auto& md = m_Data.Md;
	auto table = entry.Kind == Entry::Kinds::Function ? ClrTableId::MethodDef : entry.Kind == Entry::Kinds::Constant ? ClrTableId::Field : ClrTableId::TypeDef;
	auto token = (table << 24) | entry.Row;
	for (auto const& attribute : md.CustomAttributes(token)) {
		if (!md.TypeName(attribute.AttributeType).ends_with(L"DocumentationAttribute"))
			continue;
		// [Documentation("https://...")]
		auto text = md.AttributeCSharp(attribute);
		auto open = text.find(L'"');
		auto close = open == std::wstring::npos ? open : text.find(L'"', open + 1);
		if (close != std::wstring::npos && text.compare(open + 1, 8, L"https://") == 0)
			return text.substr(open + 1, close - open - 1);
	}
	return {};
}

std::string Win32Api::ConstantDeclaration(Entry const& entry, std::vector<std::string>* lines) {
	auto& md = m_Data.Md;
	auto token = (ClrTableId::Field << 24) | entry.Row;
	std::string text;
	auto line = [&](std::string const& s, std::string const& type = {}) {
		text += s + "\r\n";
		if (lines)
			lines->push_back(type);
	};
	line("// " + entry.Namespace);
	ClrSigType sig;
	auto id = md.FieldSigType(entry.Row, sig) ? TypeId(sig) : 0;
	auto typeText = sig.Element == 0x0E ? std::string("wchar_t const *") : TypeText(id);
	auto referred = Referred(id);
	// a number: as wide as its type (-1 of a DWORD: 0xFFFFFFFF), in hex, with its value in decimal if it is not small
	if (auto value = md.ConstantInteger(token)) {
		auto size = Layout(id).first;
		auto bits = (uint64_t)*value;
		if (size > 0 && size < 8)
			bits &= (1ULL << (size * 8)) - 1;
		GhidraDecompiler::Type type;
		bool isSigned = Describe(id, type) && type.Signed;
		auto decimal = isSigned ? std::format("{}", *value) : std::format("{}", bits);
		if (bits < 10)
			line(std::format("#define {} {}\t// {}", entry.Name, bits, typeText), referred);
		else
			line(std::format("#define {} 0x{:X}\t// {} ({})", entry.Name, bits, typeText, decimal), referred);
		return text;
	}
	// a string, a floating point number: as C# has it
	if (auto value = Narrow(md.ConstantCSharp(token)); !value.empty()) {
		line(std::format("#define {} {}{}\t// {}", entry.Name, value.starts_with("\"") ? "L" : "", value, typeText), referred);
		return text;
	}
	// a GUID ([Guid]) or a struct's value as the metadata has it ([Constant("{...}, 2")]: a PROPERTYKEY)
	for (auto const& attribute : md.CustomAttributes(token)) {
		auto name = md.TypeName(attribute.AttributeType);
		if (name.ends_with(L"GuidAttribute") && attribute.Value.size() >= 18) {
			auto b = attribute.Value.data() + 2;
			auto data1 = *(uint32_t const*)b;
			auto data2 = *(uint16_t const*)(b + 4);
			auto data3 = *(uint16_t const*)(b + 6);
			line(std::format("// {{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}", data1, data2, data3,
				b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]));
			line(std::format("DEFINE_GUID({}, 0x{:08X}, 0x{:04X}, 0x{:04X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X});",
				entry.Name, data1, data2, data3, b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]), referred);
			return text;
		}
		if (name.ends_with(L"ConstantAttribute")) {
			auto value = Narrow(md.AttributeCSharp(attribute));
			auto open = value.find('"');
			auto close = value.rfind('"');
			if (open != std::string::npos && close > open) {
				line(std::format("const {} {} = {{ {} }};", typeText, entry.Name, value.substr(open + 1, close - open - 1)), referred);
				return text;
			}
		}
	}
	line(std::format("extern const {} {};", typeText, entry.Name), referred);
	return text;
}

//
// What the attributes of the metadata say
//

std::string Win32Api::Attribute::operator[](std::string const& name) const {
	for (auto const& [n, v] : Arguments)
		if (n == name)
			return v;
	return {};
}

std::vector<Win32Api::Attribute> Win32Api::AttributesOf(uint32_t token) {
	auto& md = m_Data.Md;
	std::vector<Attribute> result;
	for (auto const& a : md.CustomAttributes(token)) {
		auto type = Narrow(md.TypeName(a.AttributeType));
		auto name = type.substr(type.find_last_of(".]") + 1);
		if (name.ends_with("Attribute"))
			name.resize(name.size() - 9);
		Attribute attribute{ name };
		int position = 0;
		for (auto const& argument : a.Arguments) {
			auto value = Narrow(argument.CSharp);
			// "\"text\"": text; "-1L", "5u": the number
			if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
				value = value.substr(1, value.size() - 2);
			else if (!value.empty() && (isdigit((unsigned char)value[0]) || value[0] == '-'))
				while (!value.empty() && strchr("uUlL", value.back()))
					value.pop_back();
			attribute.Arguments.emplace_back(argument.Name.empty() ? std::to_string(position++) : Narrow(argument.Name), value);
		}
		result.push_back(std::move(attribute));
	}
	return result;
}

namespace {
	// "windows5.1.2600": "Windows XP+"
	std::string WindowsVersion(std::string const& platform) {
		if (!platform.starts_with("windows"))
			return platform;
		int major = 0, minor = 0, build = 0;
		sscanf_s(platform.c_str() + 7, "%d.%d.%d", &major, &minor, &build);
		std::string name;
		if (major == 5)
			name = minor == 0 ? "Windows 2000" : minor == 1 ? "Windows XP" : "Windows Server 2003";
		else if (major == 6)
			name = minor == 0 ? "Windows Vista" : minor == 1 ? "Windows 7" : minor == 2 ? "Windows 8" : "Windows 8.1";
		else if (major == 8)
			name = minor == 0 ? "Windows 8" : "Windows 8.1";
		else if (major == 10)
			name = build >= 22000 ? std::format("Windows 11 (build {})", build) : build ? std::format("Windows 10 (build {})", build) : "Windows 10";
		else
			name = "Windows " + platform.substr(7);
		return name + "+";
	}

	std::string Joined(std::vector<std::string> const& parts) {
		std::string text;
		for (auto const& part : parts)
			text += (text.empty() ? "" : ", ") + part;
		return text;
	}
}

std::vector<std::string> Win32Api::Notes(std::vector<Attribute> const& attributes, uint32_t method) {
	// a parameter by its index (0 is the first): its name
	auto parameter = [&](std::string const& index) {
		if (index.empty() || !method)
			return index;
		auto name = Narrow(m_Data.Md.ParamName(method, (uint32_t)atoi(index.c_str()) + 1));
		return name.empty() ? index : name;
	};
	std::vector<std::string> notes;
	for (auto const& a : attributes) {
		auto const& n = a.Name;
		if (n == "SupportedOSPlatform")
			notes.push_back(WindowsVersion(a["0"]));
		else if (n == "Obsolete")
			notes.push_back(a["0"].empty() ? "obsolete" : "obsolete: " + a["0"]);
		else if (n == "DoesNotReturn")
			notes.push_back("does not return");
		else if (n == "Const")
			notes.push_back("const");
		else if (n == "MemorySize" && !a["BytesParamIndex"].empty())
			notes.push_back("size in bytes: " + parameter(a["BytesParamIndex"]));
		else if (n == "NativeArrayInfo") {
			if (!a["CountParamIndex"].empty())
				notes.push_back("count: " + parameter(a["CountParamIndex"]));
			else if (!a["CountFieldName"].empty())
				notes.push_back("count: " + a["CountFieldName"]);
			else if (!a["CountConst"].empty())
				notes.push_back("count: " + a["CountConst"]);
		}
		else if (n == "ComOutPtr")
			notes.push_back("COM object out");
		else if (n == "RetVal")
			notes.push_back("retval");
		else if (n == "Reserved")
			notes.push_back("reserved");
		else if (n == "NullNullTerminated")
			notes.push_back("double-null-terminated");
		else if (n == "NotNullTerminated")
			notes.push_back("not null-terminated");
		else if (n == "FreeWith" || n == "RAIIFree")
			notes.push_back("free with " + a["0"]);
		else if (n == "DoNotRelease")
			notes.push_back("do not release");
		else if (n == "Retained")
			notes.push_back("retained");
		else if (n == "AssociatedEnum")
			notes.push_back("values: " + a["0"]);
		else if (n == "StructSizeField")
			notes.push_back("set " + a["0"] + " to its size");
		else if (n == "InvalidHandleValue")
			notes.push_back("invalid: " + a["0"]);
		else if (n == "FlexibleArray")
			notes.push_back("flexible array");
	}
	// (the invalid values of a handle: in one)
	auto invalid = [](std::string const& note) { return note.starts_with("invalid: "); };
	if (std::ranges::count_if(notes, invalid) > 1) {
		std::string values;
		for (auto const& note : notes)
			if (invalid(note))
				values += (values.empty() ? "" : " or ") + note.substr(9);
		std::erase_if(notes, invalid);
		notes.push_back("invalid: " + values);
	}
	return notes;
}

std::string Win32Api::HeaderOf(std::wstring const& url) {
	// ".../windows/win32/api/fileapi/nf-fileapi-createfilew", ".../windows-hardware/drivers/ddi/wdm/nf-wdm-..."
	auto text = Narrow(url);
	for (auto marker : { "/api/", "/ddi/" }) {
		auto at = text.find(marker);
		if (at == std::string::npos)
			continue;
		at += strlen(marker);
		auto end = text.find('/', at);
		if (end == std::string::npos || end == at)
			continue;
		auto header = text.substr(at, end - at);
		if (header.find_first_of("#?.") != std::string::npos)
			continue;
		for (auto& c : header)
			c = (char)tolower((unsigned char)c);
		return header + ".h";
	}
	return {};
}

std::string Win32Api::Declaration(Entry const& entry, std::vector<std::string>* lines) {
	if (entry.Kind == Entry::Kinds::Constant) {
		if (lines)
			lines->clear();
		return ConstantDeclaration(entry, lines);
	}
	auto& md = m_Data.Md;
	std::string text;
	// "HANDLE hFile", "SECURITY_ATTRIBUTES *lpAttributes"
	auto declare = [](std::string const& type, std::string const& name) {
		return type + (type.ends_with("*") ? "" : " ") + name;
	};
	auto line = [&](std::string const& s, std::string const& type = {}) {
		text += s + "\r\n";
		if (lines)
			lines->push_back(type);
	};
	if (lines)
		lines->clear();
	// what a line refers to: its type, else the enum its values are of (a uint of the values of an enum)
	auto referredOr = [](std::string referred, std::vector<Attribute> const& attributes) {
		if (referred.empty())
			for (auto const& a : attributes)
				if (a.Name == "AssociatedEnum")
					return a["0"];
		return referred;
	};
	// a function (or a method of an interface, or a callback's): "Type Name(\n\tType param, // in ..."
	auto signature = [&](uint32_t method, std::string const& name, std::string const& prefix, std::string const& indent) {
		ClrSigType result;
		std::vector<ClrSigType> params;
		if (!md.MethodSigTypes(method, result, params)) {
			line(indent + prefix + name + "(?);");
			return;
		}
		auto ret = TypeId(result);
		auto returnAttributes = AttributesOf(md.ParamToken(method, 0));
		auto returnNotes = Joined(Notes(returnAttributes, method));
		auto returned = TypeText(ret);
		if (std::ranges::any_of(returnAttributes, [](auto const& a) { return a.Name == "Const"; }) && returned.ends_with("*"))
			returned = "const " + returned;
		auto head = indent + prefix + returned + " " + name + "(";
		auto returnNote = returnNotes.empty() ? std::string() : "\t// returns: " + returnNotes;
		if (params.empty()) {
			line(head + "void);" + returnNote, referredOr(Referred(ret), returnAttributes));
			return;
		}
		line(head + returnNote, referredOr(Referred(ret), returnAttributes));
		for (size_t i = 0; i < params.size(); i++) {
			auto id = TypeId(params[i]);
			auto flags = md.ParamFlags(method, (uint32_t)i + 1);
			std::vector<std::string> notes;
			if (flags & 1)
				notes.push_back("in");
			if (flags & 2)
				notes.push_back("out");
			if (flags & 0x10)
				notes.push_back("optional");
			auto attributes = AttributesOf(md.ParamToken(method, (uint32_t)i + 1));
			auto typeText = TypeText(id);
			// (a pointer to what is not changed: as C writes it)
			bool isConst = std::ranges::any_of(attributes, [](auto const& a) { return a.Name == "Const"; });
			auto referred = Referred(id);
			if (isConst && typeText.ends_with("*"))
				typeText = "const " + typeText;
			// (a string that is not changed: PCWSTR, as the SDK has it; it is PWSTR's, if the metadata has no PCWSTR)
			else if (isConst && (typeText == "PWSTR" || typeText == "PSTR")) {
				typeText = "PC" + typeText.substr(1);
				if (Find(typeText))
					referred = typeText;
			}
			for (auto& note : Notes(attributes, method))
				if (note != "const" || (!typeText.starts_with("const ") && !typeText.starts_with("PC")))
					notes.push_back(std::move(note));
			auto pname = Narrow(md.ParamName(method, (uint32_t)i + 1));
			line(std::format("{}\t{}{}{}", indent, declare(typeText, pname.empty() ? std::format("param{}", i + 1) : pname),
				i + 1 < params.size() ? "," : ");", notes.empty() ? "" : "\t// " + Joined(notes)), referredOr(referred, attributes));
		}
	};
	// where it is ("// KERNEL32.dll", "// Windows.Win32.Security"), with its header; then what its attributes say of it
	auto header = HeaderOf(DocumentationUrl(entry));
	auto where = std::format("// {}{}", entry.Kind == Entry::Kinds::Function ? entry.Where : entry.Namespace,
		header.empty() ? "" : " (" + header + ")");
	auto token = ((entry.Kind == Entry::Kinds::Function ? ClrTableId::MethodDef : ClrTableId::TypeDef) << 24) | entry.Row;
	auto notes = Notes(AttributesOf(token));
	if (entry.Kind == Entry::Kinds::Function)
		if (auto native = md.PInvoke(entry.Row); native && (native->Flags & 0x40))
			notes.insert(notes.begin(), "sets the last error");
	line(where);
	if (!notes.empty())
		line("// " + Joined(notes));

	switch (entry.Kind) {
		case Entry::Kinds::Function:
			signature(entry.Row, entry.Name, "", "");
			break;

		case Entry::Kinds::Callback:
			for (auto m : md.MethodsOf(entry.Row))
				if (md.Methods()[m - 1].Name == L"Invoke") {
					signature(m, "(*" + entry.Name + ")", "typedef ", "");
					break;
				}
			break;

		case Entry::Kinds::Interface:
		{
			std::string bases;
			for (auto token : md.Interfaces(entry.Row))
				bases += (bases.empty() ? " : " : ", ") + Narrow(md.TypeName(token).substr(md.TypeName(token).rfind(L'.') + 1));
			line("interface " + entry.Name + bases);
			line("{");
			for (auto m : md.MethodsOf(entry.Row)) {
				signature(m, Narrow(md.Methods()[m - 1].Name), "", "\t");
			}
			line("};");
			break;
		}

		default:
		{
			GhidraDecompiler::Type type;
			auto id = IdBit | entry.Row;
			if (!Describe(id, type)) {
				line("// (not described)");
				break;
			}
			if (type.Kind == Kind::Enum) {
				line(std::format("enum {} : {}{}", entry.Name, type.Signed ? "" : "u",
					type.Size == 1 ? "char" : type.Size == 2 ? "short" : type.Size == 8 ? "longlong" : "int"));
				line(type.Exact ? "{" : "{\t// flags");
				uint64_t mask = type.Size >= 8 ? ~0ULL : (1ULL << (type.Size * 8)) - 1;
				for (auto const& [name, value] : type.Values)
					line(std::format("\t{} = 0x{:X},", name, value & mask));
				line("};");
			}
			else if (type.Kind == Kind::Typedef)
				line("typedef " + TypeText(type.Target) + " " + entry.Name + ";", Referred(type.Target));
			else if (type.Kind == Kind::Struct || type.Kind == Kind::Union) {
				line(std::format("{} {}\t// 0x{:X} bytes", type.Kind == Kind::Union ? "union" : "struct", entry.Name, type.Size));
				line("{");
				// (the fields as the metadata has them: their attributes)
				std::vector<uint32_t> fieldRows;
				for (auto f : md.FieldsOf(entry.Row))
					if ((md.Fields()[f - 1].Flags & 0x10) == 0)
						fieldRows.push_back(f);
				for (size_t i = 0; i < type.Fields.size(); i++) {
					auto const& f = type.Fields[i];
					// an array's count goes after the name
					GhidraDecompiler::Type ft;
					auto typeText = TypeText(f.Type);
					std::string suffix;
					if (Describe(f.Type, ft) && ft.Kind == Kind::Array) {
						typeText = TypeText(ft.Target);
						suffix = std::format("[{}]", ft.Count);
					}
					auto attributes = i < fieldRows.size() ? AttributesOf((ClrTableId::Field << 24) | fieldRows[i]) : std::vector<Attribute>();
					auto fieldNotes = Notes(attributes);
					line(std::format("\t/* 0x{:03X} */ {}{};{}", f.Offset, declare(typeText, f.Name), suffix,
						fieldNotes.empty() ? "" : "\t// " + Joined(fieldNotes)), referredOr(Referred(f.Type), attributes));
					// the bit fields it holds: "Depth : 16 (bits 0-15)"
					for (auto const& a : attributes)
						if (a.Name == "NativeBitfield") {
							auto offset = atoi(a["1"].c_str()), length = atoi(a["2"].c_str());
							line(std::format("\t\t//   {} : {} (bit{} {})", a["0"], length, length > 1 ? "s" : "",
								length > 1 ? std::format("{}-{}", offset, offset + length - 1) : std::to_string(offset)));
						}
				}
				line("};");
			}
			else if (type.Kind == Kind::Enum || type.Kind == Kind::Integer)
				line("typedef " + TypeText(id) + " " + entry.Name + ";");
			break;
		}
	}
	return text;
}

//
// Layouts
//

std::optional<Win32Api::StructLayout> Win32Api::LayoutOf(Entry const& entry) {
	if (entry.Kind != Entry::Kinds::Struct && entry.Kind != Entry::Kinds::Union)
		return std::nullopt;
	auto id = IdBit | entry.Row;
	GhidraDecompiler::Type type;
	if (!Describe(id, type) || (type.Kind != Kind::Struct && type.Kind != Kind::Union))
		return std::nullopt;
	StructLayout layout;
	layout.Size = type.Size;
	layout.Alignment = Layout(id).second;
	AddLayout(id, type, 0, 0, {}, layout.Items);
	return layout;
}

void Win32Api::AddLayout(uint32_t id, GhidraDecompiler::Type const& type, uint32_t base, int depth, std::string const& path,
	std::vector<LayoutItem>& items) {
	auto& md = m_Data.Md;
	bool isUnion = type.Kind == Kind::Union;
	// the fields as the metadata has them (of a type of the metadata, not one made): their attributes
	std::vector<uint32_t> fieldRows;
	if (IsId(id) && !(id & MadeBit))
		for (auto f : md.FieldsOf(id & ~IdBit))
			if ((md.Fields()[f - 1].Flags & 0x10) == 0)
				fieldRows.push_back(f);
	// what a type is, through its typedefs (LARGE_INTEGER: a union)
	auto resolve = [&](uint32_t typeId, GhidraDecompiler::Type& resolved) {
		for (int i = 0; i < 16 && Describe(typeId, resolved); i++) {
			if (resolved.Kind != Kind::Typedef)
				return typeId;
			typeId = resolved.Target;
		}
		return 0u;
	};
	uint32_t end = 0;	// (of the fields so far: what is after it, if not a field, is padding)
	auto padding = [&](uint32_t from, uint32_t to) {
		if (to > from)
			items.push_back({ LayoutItem::Kinds::Padding, depth, path + std::format("(padding at 0x{:X})", base + from), "",
				"", base + from, to - from });
	};
	for (size_t i = 0; i < type.Fields.size(); i++) {
		auto const& f = type.Fields[i];
		auto size = Layout(f.Type).first;
		if (!isUnion)
			padding(end, f.Offset);
		GhidraDecompiler::Type ft;
		auto typeText = TypeText(f.Type);
		auto name = f.Name;
		uint32_t element = 0;
		uint32_t count = 0;
		if (Describe(f.Type, ft) && ft.Kind == Kind::Array) {
			typeText = TypeText(ft.Target);
			name += std::format("[{}]", ft.Count);
			element = ft.Target;
			count = ft.Count;
		}
		auto at = base + f.Offset;
		// a type nested in another (unnamed in C: "union { ... } Anonymous"): by what it is, not the name made for it
		auto typeId = element ? element : f.Type;
		if (IsId(typeId) && !(typeId & MadeBit)) {
			auto row = typeId & ~IdBit;
			GhidraDecompiler::Type nested;
			if (row && row <= md.Types().size() && md.Types()[row - 1].Enclosing && Describe(typeId, nested))
				typeText = nested.Kind == Kind::Union ? "union" : "struct";
		}
		items.push_back({ LayoutItem::Kinds::Field, depth, path + f.Name, typeText, name, at, size, Referred(f.Type) });
		// its bit fields
		if (i < fieldRows.size())
			for (auto const& a : AttributesOf((ClrTableId::Field << 24) | fieldRows[i]))
				if (a.Name == "NativeBitfield") {
					auto offset = atoi(a["1"].c_str()), length = atoi(a["2"].c_str());
					items.push_back({ LayoutItem::Kinds::Bitfield, depth + 1, path + f.Name + "." + a["0"], "", a["0"], at, 0, {},
						length > 1 ? std::format("bits {}-{}", offset, offset + length - 1) : std::format("bit {}", offset) });
				}
		// a struct or union in it: its fields (an array's: of its first element)
		GhidraDecompiler::Type inner;
		if (depth < 8) {
			if (auto innerId = resolve(element ? element : f.Type, inner); innerId && (inner.Kind == Kind::Struct || inner.Kind == Kind::Union)) {
				auto innerPath = path + f.Name + (element ? "[0]." : ".");
				if (element && count > 1)
					items.back().Note = std::format("{} of 0x{:X} bytes", count, Layout(element).first);
				AddLayout(innerId, inner, at, depth + 1, innerPath, items);
			}
		}
		if (!isUnion)
			end = std::max(end, f.Offset + size);
		else
			end = std::max(end, size);
	}
	padding(end, type.Size);
}

//
// Values of enums, vtables
//

std::string Win32Api::DecodeValue(Entry const& entry, uint64_t value) {
	if (entry.Kind != Entry::Kinds::Enum && entry.Kind != Entry::Kinds::Typedef)
		return {};
	GhidraDecompiler::Type type;
	if (!Describe(IdBit | entry.Row, type) || type.Kind != Kind::Enum)
		return {};
	uint64_t mask = type.Size >= 8 ? ~0ULL : (1ULL << (type.Size * 8)) - 1;
	value &= mask;
	// a name of the value itself (the first: the one code uses)
	for (auto const& [name, v] : type.Values)
		if ((v & mask) == value)
			return name;
	if (type.Exact || value == 0)
		return std::format("0x{:X} (none of its names)", value);
	// of flags: the names whose bits it has, those of more bits first (each adds bits the others do not have)
	std::vector<std::pair<std::string, uint64_t>> candidates;
	for (auto const& [name, v] : type.Values)
		if ((v & mask) != 0 && ((v & mask) & ~value) == 0)
			candidates.emplace_back(name, v & mask);
	std::ranges::stable_sort(candidates, [](auto const& a, auto const& b) {
		auto bitsA = std::popcount(a.second), bitsB = std::popcount(b.second);
		return bitsA != bitsB ? bitsA > bitsB : a.second < b.second;
	});
	uint64_t covered = 0;
	std::vector<std::pair<std::string, uint64_t>> chosen;
	for (auto const& candidate : candidates)
		if (candidate.second & ~covered) {
			chosen.push_back(candidate);
			covered |= candidate.second;
		}
	std::ranges::sort(chosen, {}, &std::pair<std::string, uint64_t>::second);
	std::string text;
	for (auto const& [name, v] : chosen)
		text += (text.empty() ? "" : " | ") + name;
	if (auto rest = value & ~covered)
		text += (text.empty() ? "" : " | ") + std::format("0x{:X}", rest);
	return text;
}

std::vector<Win32Api::VtableSlot> Win32Api::VtableOf(Entry const& entry) {
	if (entry.Kind != Entry::Kinds::Interface)
		return {};
	auto& md = m_Data.Md;
	std::vector<VtableSlot> slots;
	std::set<uint32_t> seen;
	auto declare = [](std::string const& type, std::string const& name) {
		return type + (type.ends_with("*") ? "" : " ") + name;
	};
	// (as the vtable has them: the bases' first)
	auto collect = [&](auto&& self, uint32_t row) -> void {
		if (!seen.insert(row).second)
			return;
		for (auto token : md.Interfaces(row))
			if (auto base = RowOf(token))
				self(self, base);
		auto owner = Name(row);
		for (auto m : md.MethodsOf(row)) {
			VtableSlot slot{ owner, Narrow(md.Methods()[m - 1].Name) };
			ClrSigType result;
			std::vector<ClrSigType> params;
			if (md.MethodSigTypes(m, result, params)) {
				auto ret = TypeId(result);
				std::string list;
				for (size_t i = 0; i < params.size(); i++) {
					auto id = TypeId(params[i]);
					auto name = Narrow(md.ParamName(m, (uint32_t)i + 1));
					list += (i ? ", " : "") + declare(TypeText(id), name.empty() ? std::format("param{}", i + 1) : name);
					if (slot.Referred.empty())
						slot.Referred = Referred(id);
				}
				slot.Signature = std::format("{} {}({})", TypeText(ret), slot.Method, list.empty() ? "void" : list);
				if (auto referred = Referred(ret); !referred.empty())
					slot.Referred = referred;
			}
			else
				slot.Signature = slot.Method + "(?)";
			slots.push_back(std::move(slot));
		}
	};
	collect(collect, entry.Row);
	return slots;
}

//
// Related entries
//

namespace {
	// a name without its suffix: of ANSI or Unicode (A, W), then "Ex" (or "EX") and a number, in that order
	// ("CreateWindowExW": "CreateWindow"; "CreateFile2": "CreateFile"; "OSVERSIONINFOEXW": "OSVERSIONINFO")
	std::string Stem(std::string name) {
		auto strip = [&](std::string_view suffix) {
			if (name.size() > suffix.size() + 2 && name.ends_with(suffix)) {
				name.resize(name.size() - suffix.size());
				return true;
			}
			return false;
		};
		if (!strip("A"))
			strip("W");
		while (name.size() > 3 && isdigit((unsigned char)name.back()))
			name.pop_back();
		if (!strip("Ex"))
			strip("EX");
		while (name.size() > 3 && isdigit((unsigned char)name.back()))
			name.pop_back();
		return name;
	}
}

std::vector<size_t> Win32Api::RelatedOf(size_t entry) {
	auto const& entries = Entries();
	if (entry >= entries.size())
		return {};
	using Kinds = Entry::Kinds;
	if (entries[entry].Kind == Kinds::Constant)
		return {};
	if (m_Stems.empty())
		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].Kind != Kinds::Constant)
				m_Stems[Stem(entries[i].Name)].push_back(i);
	auto isFunction = entries[entry].Kind == Kinds::Function;
	std::vector<size_t> related;
	if (auto it = m_Stems.find(Stem(entries[entry].Name)); it != m_Stems.end())
		for (auto i : it->second)
			if (entries[i].Name != entries[entry].Name && (entries[i].Kind == Kinds::Function) == isFunction &&
				std::ranges::none_of(related, [&](size_t r) { return entries[r].Name == entries[i].Name; }))
				related.push_back(i);
	return related;
}

//
// Declarations in other languages
//

namespace {
	// the primitive types of signatures (ELEMENT_TYPE): C#, Rust, Python (ctypes)
	char const* const* LanguagePrimitive(uint8_t element) {
		static char const* const types[][3] = {
			{ "bool", "bool", "ctypes.c_bool" },		// 0x02
			{ "char", "u16", "ctypes.c_wchar" },		// 0x03
			{ "sbyte", "i8", "ctypes.c_int8" },			// 0x04
			{ "byte", "u8", "ctypes.c_uint8" },			// 0x05
			{ "short", "i16", "ctypes.c_int16" },		// 0x06
			{ "ushort", "u16", "ctypes.c_uint16" },		// 0x07
			{ "int", "i32", "ctypes.c_int32" },			// 0x08
			{ "uint", "u32", "ctypes.c_uint32" },		// 0x09
			{ "long", "i64", "ctypes.c_int64" },		// 0x0A
			{ "ulong", "u64", "ctypes.c_uint64" },		// 0x0B
			{ "float", "f32", "ctypes.c_float" },		// 0x0C
			{ "double", "f64", "ctypes.c_double" },		// 0x0D
		};
		static char const* const native[][3] = {
			{ "nint", "isize", "ctypes.c_ssize_t" },	// 0x18
			{ "nuint", "usize", "ctypes.c_size_t" },	// 0x19
		};
		if (element >= 0x02 && element <= 0x0D)
			return types[element - 0x02];
		if (element == 0x18 || element == 0x19)
			return native[element - 0x18];
		return nullptr;
	}

	// a name that is a keyword of the language: as it can be a name
	std::string Escaped(std::string const& name, Win32Api::Language language) {
		static std::set<std::string> const csharp = { "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char",
			"checked", "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event",
			"explicit", "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int",
			"interface", "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override",
			"params", "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof",
			"stackalloc", "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong",
			"unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while" };
		static std::set<std::string> const rust = { "as", "break", "const", "continue", "crate", "else", "enum", "extern",
			"false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
			"static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", "async", "await", "dyn",
			"abstract", "become", "box", "do", "final", "macro", "override", "priv", "typeof", "unsized", "virtual", "yield", "try" };
		if (language == Win32Api::Language::CSharp && csharp.contains(name))
			return "@" + name;
		if (language == Win32Api::Language::Rust && (rust.contains(name)))
			return name == "self" || name == "Self" || name == "crate" || name == "super" ? name + "_" : "r#" + name;
		return name;
	}
}

std::string Win32Api::DeclarationIn(Entry const& entry, Language language) {
	auto& md = m_Data.Md;
	bool const cs = language == Language::CSharp, rs = language == Language::Rust, py = language == Language::Python;
	auto const comment = py ? std::string("# ") : std::string("// ");
	auto const index = (int)language;
	std::set<std::string> uses;			// the types it refers to (of the metadata, not nested in it)
	std::vector<uint32_t> nested;		// the types nested in it that it uses, to declare too
	std::set<uint32_t> nestedSeen;

	// a type of a signature (constant: of what a pointer points to)
	std::function<std::string(ClrSigType const&, bool)> typeOf = [&](ClrSigType const& t, bool isConst) -> std::string {
		if (auto names = LanguagePrimitive(t.Element))
			return names[index];
		switch (t.Element) {
			case 0x01:
				return cs ? "void" : rs ? "c_void" : "None";
			case 0x0F: case 0x10:
			{
				auto const& inner = t.Inner.empty() ? ClrSigType{ 0x01 } : t.Inner[0];
				auto target = typeOf(inner, false);
				if (cs)
					return target + "*";
				if (rs)
					return (isConst ? "*const " : "*mut ") + target;
				return inner.Element == 0x01 ? "ctypes.c_void_p" : "ctypes.POINTER(" + target + ")";
			}
			case 0x1D: case 0x14:
			{
				// (an array, not of a struct's field: a pointer to its first)
				ClrSigType pointer{ 0x0F };
				if (!t.Inner.empty())
					pointer.Inner = { t.Inner[0] };
				return typeOf(pointer, isConst);
			}
			case 0x0E:
				return cs ? "string" : rs ? "*const u16" : "ctypes.c_wchar_p";
			case 0x11: case 0x12:
			{
				auto row = RowOf(t.Token);
				if (!row) {
					// of another assembly: a GUID, or what a pointer is
					if (md.TypeName(t.Token).ends_with(L"System.Guid"))
						return cs ? "Guid" : (uses.insert("GUID"), "GUID");
					return cs ? "nint" : rs ? "*mut c_void" : "ctypes.c_void_p";
				}
				auto name = Name(row);
				if (TypeKindOf(row) == L"interface") {
					// (a COM object: through a pointer)
					if (cs) {
						uses.insert(name);
						return name;
					}
					return rs ? "*mut c_void" : "ctypes.c_void_p";
				}
				if (md.Types()[row - 1].Enclosing) {
					if (nestedSeen.insert(row).second)
						nested.push_back(row);
				}
				else
					uses.insert(name);
				return name;
			}
		}
		return cs ? "nint" : rs ? "isize" : "ctypes.c_ssize_t";
	};

	// a method's result and parameters: "HANDLE", { "PCWSTR lpFileName", ... } in the language's way
	struct Signature {
		std::string Result;
		std::vector<std::pair<std::string, std::string>> Parameters;	// type, name
		bool Void{ false };
	};
	auto signatureOf = [&](uint32_t method) {
		Signature s;
		ClrSigType result;
		std::vector<ClrSigType> params;
		if (!md.MethodSigTypes(method, result, params))
			return s;
		s.Void = result.Element == 0x01;
		s.Result = typeOf(result, false);
		for (size_t i = 0; i < params.size(); i++) {
			auto attributes = AttributesOf(md.ParamToken(method, (uint32_t)i + 1));
			bool isConst = std::ranges::any_of(attributes, [](auto const& a) { return a.Name == "Const"; });
			auto name = Narrow(md.ParamName(method, (uint32_t)i + 1));
			s.Parameters.emplace_back(typeOf(params[i], isConst), Escaped(name.empty() ? std::format("param{}", i + 1) : name, language));
		}
		return s;
	};
	auto parameterList = [&](Signature const& s, bool withThis = false) {
		std::string list;
		if (withThis)
			list = cs ? "" : rs ? "this: *mut c_void" : "ctypes.c_void_p";
		for (auto const& [type, name] : s.Parameters) {
			if (!list.empty())
				list += ", ";
			list += cs ? type + " " + name : rs ? name + ": " + type : type;
		}
		return list;
	};
	// a Rust function type: "unsafe extern "system" fn(...) -> T"
	auto rustFunction = [&](Signature const& s, bool withThis) {
		return std::format("unsafe extern \"system\" fn({}){}", parameterList(s, withThis), s.Void ? "" : " -> " + s.Result);
	};

	// a struct or union (its fields; those nested in it are declared too)
	auto structOf = [&](uint32_t row) {
		auto name = Name(row);
		auto const& t = md.Types()[row - 1];
		bool explicitLayout = (t.Flags & 0x18) == 0x10;
		GhidraDecompiler::Type described;
		bool isUnion = Describe(IdBit | row, described) && described.Kind == Kind::Union;
		auto [packing, classSize] = md.ClassLayout(row);
		std::string text;
		if (cs)
			text += std::format("[StructLayout(LayoutKind.{}{})]\r\npublic unsafe struct {}\r\n{{\r\n", explicitLayout ? "Explicit" : "Sequential",
				packing ? std::format(", Pack = {}", packing) : "", name);
		else if (rs)
			text += std::format("#[repr(C{})]\r\n#[derive(Clone, Copy)]\r\npub {} {} {{\r\n", packing ? std::format(", packed({})", packing) : "",
				isUnion ? "union" : "struct", name);
		else
			text += std::format("class {}(ctypes.{}):\r\n{}    _fields_ = [\r\n", name, isUnion ? "Union" : "Structure",
				packing ? std::format("    _pack_ = {}\r\n", packing) : "");
		for (auto f : md.FieldsOf(row)) {
			if (md.Fields()[f - 1].Flags & 0x10)
				continue;
			ClrSigType ft;
			if (!md.FieldSigType(f, ft))
				continue;
			auto fieldName = Escaped(Narrow(md.Fields()[f - 1].Name), language);
			auto attributes = AttributesOf((ClrTableId::Field << 24) | f);
			bool isConst = std::ranges::any_of(attributes, [](auto const& a) { return a.Name == "Const"; });
			// an array: of its count
			std::string declaration;
			if (ft.Element == 0x14 && !ft.Inner.empty() && ft.Count) {
				auto element = typeOf(ft.Inner[0], false);
				if (cs)
					declaration = LanguagePrimitive(ft.Inner[0].Element) ? std::format("public fixed {} {}[{}];", element, fieldName, ft.Count) :
						std::format("[MarshalAs(UnmanagedType.ByValArray, SizeConst = {})] public {}[] {};", ft.Count, element, fieldName);
				else if (rs)
					declaration = std::format("pub {}: [{}; {}],", fieldName, element, ft.Count);
				else
					declaration = std::format("(\"{}\", {} * {}),", fieldName, element, ft.Count);
			}
			else {
				auto type = typeOf(ft, isConst);
				declaration = cs ? std::format("public {} {};", type, fieldName) : rs ? std::format("pub {}: {},", fieldName, type) :
					std::format("(\"{}\", {}),", fieldName, type);
			}
			if (cs && explicitLayout)
				declaration = std::format("[FieldOffset({})] ", md.FieldOffset(f).value_or(0)) + declaration;
			text += (py ? "        " : "    ") + declaration + "\r\n";
		}
		text += cs || rs ? "}\r\n" : "    ]\r\n";
		return text;
	};

	std::string body;
	auto token = ((entry.Kind == Entry::Kinds::Function ? ClrTableId::MethodDef : entry.Kind == Entry::Kinds::Constant ?
		ClrTableId::Field : ClrTableId::TypeDef) << 24) | entry.Row;
	switch (entry.Kind) {
		case Entry::Kinds::Function:
		{
			auto native = md.PInvoke(entry.Row);
			auto module = native ? Narrow(native->Module) : std::string();
			auto exported = native && !native->EntryPoint.empty() ? Narrow(native->EntryPoint) : entry.Name;
			bool lastError = native && (native->Flags & 0x40);
			auto s = signatureOf(entry.Row);
			auto library = module;
			for (auto& c : library)
				c = (char)tolower((unsigned char)c);
			if (library.ends_with(".dll"))
				library.resize(library.size() - 4);
			if (cs)
				body = std::format("[DllImport(\"{}\", ExactSpelling = true{}{})]\r\npublic static extern unsafe {} {}({});\r\n", module,
					exported != entry.Name ? std::format(", EntryPoint = \"{}\"", exported) : "", lastError ? ", SetLastError = true" : "",
					s.Result, entry.Name, parameterList(s));
			else if (rs)
				body = std::format("#[link(name = \"{}\")]\r\nextern \"system\" {{\r\n    {}pub fn {}({}){};\r\n}}\r\n", library,
					exported != entry.Name ? std::format("#[link_name = \"{}\"]\r\n    ", exported) : "", entry.Name, parameterList(s),
					s.Void ? "" : " -> " + s.Result);
			else
				body = std::format("{} = ctypes.WinDLL(\"{}\"{}){}\r\n{}.argtypes = [{}]\r\n{}.restype = {}\r\n", entry.Name, library,
					lastError ? ", use_last_error=True" : "", exported.starts_with("#") ? std::format("[{}]", exported.substr(1)) : "." + exported,
					entry.Name, parameterList(s), entry.Name, s.Result);
			break;
		}

		case Entry::Kinds::Struct: case Entry::Kinds::Union:
			body = structOf(entry.Row);
			break;

		case Entry::Kinds::Enum:
		{
			GhidraDecompiler::Type type;
			if (!Describe(IdBit | entry.Row, type) || type.Kind != Kind::Enum)
				break;
			auto size = type.Size;
			int sizeIndex = size == 1 ? 0 : size == 2 ? 1 : size == 8 ? 3 : 2;
			static char const* const underlying[2][4][3] = {
				{ { "byte", "u8", "ctypes.c_uint8" }, { "ushort", "u16", "ctypes.c_uint16" }, { "uint", "u32", "ctypes.c_uint32" }, { "ulong", "u64", "ctypes.c_uint64" } },
				{ { "sbyte", "i8", "ctypes.c_int8" }, { "short", "i16", "ctypes.c_int16" }, { "int", "i32", "ctypes.c_int32" }, { "long", "i64", "ctypes.c_int64" } },
			};
			auto base = underlying[type.Signed ? 1 : 0][sizeIndex][index];
			uint64_t mask = size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
			auto literal = [&](uint64_t value) {
				value &= mask;
				// (a negative value of a signed one: as a number of its own, not of bits)
				if (type.Signed && (value >> (size * 8 - 1)) & 1) {
					auto negative = (int64_t)(value | ~mask);
					return cs ? std::format("unchecked(({}){})", base, negative) : std::format("{}", negative);
				}
				return std::format("0x{:X}", value);
			};
			if (cs) {
				body = std::format("{}public enum {} : {}\r\n{{\r\n", type.Exact ? "" : "[Flags]\r\n", entry.Name, base);
				for (auto const& [name, value] : type.Values)
					body += std::format("    {} = {},\r\n", name, literal(value));
				body += "}\r\n";
			}
			else if (rs) {
				body = std::format("pub type {} = {};\r\n", entry.Name, base);
				for (auto const& [name, value] : type.Values)
					body += std::format("pub const {}: {} = {}{};\r\n", name, entry.Name, literal(value), literal(value).starts_with("-") ? "" : base);
			}
			else {
				body = std::format("{} = {}\r\n", entry.Name, base);
				for (auto const& [name, value] : type.Values)
					body += std::format("{} = {}\r\n", name, literal(value));
			}
			break;
		}

		case Entry::Kinds::Typedef:
		{
			// (a struct of one field, its value)
			auto fields = md.FieldsOf(entry.Row);
			ClrSigType value;
			if (fields.empty() || !md.FieldSigType(fields[0], value))
				break;
			auto type = typeOf(value, false);
			body = cs ? std::format("public unsafe struct {}\r\n{{\r\n    public {} Value;\r\n}}\r\n", entry.Name, type) :
				rs ? std::format("pub type {} = {};\r\n", entry.Name, type) : std::format("{} = {}\r\n", entry.Name, type);
			break;
		}

		case Entry::Kinds::Callback:
			for (auto m : md.MethodsOf(entry.Row))
				if (md.Methods()[m - 1].Name == L"Invoke") {
					auto s = signatureOf(m);
					if (cs)
						body = std::format("[UnmanagedFunctionPointer(CallingConvention.Winapi)]\r\npublic unsafe delegate {} {}({});\r\n", s.Result,
							entry.Name, parameterList(s));
					else if (rs)
						body = std::format("pub type {} = Option<{}>;\r\n", entry.Name, rustFunction(s, false));
					else
						body = std::format("{} = ctypes.WINFUNCTYPE({}{}{})\r\n", entry.Name, s.Result, s.Parameters.empty() ? "" : ", ", parameterList(s));
					break;
				}
			break;

		case Entry::Kinds::Interface:
		{
			// its methods, and its bases' (the vtable's order); its IID
			std::string guid;
			for (auto const& a : md.CustomAttributes(token))
				if (a.Value.size() >= 18 && md.TypeName(a.AttributeType).ends_with(L"GuidAttribute")) {
					auto b = a.Value.data() + 2;
					guid = std::format("{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", *(uint32_t const*)b,
						*(uint16_t const*)(b + 4), *(uint16_t const*)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
				}
			std::vector<uint32_t> bases;
			for (auto t : md.Interfaces(entry.Row))
				if (auto base = RowOf(t))
					bases.push_back(base);
			if (cs) {
				// (ComImport: the methods of its bases but IUnknown's, then its own)
				std::vector<uint32_t> rows;
				std::set<uint32_t> seen;
				auto collect = [&](auto&& self, uint32_t row) -> void {
					if (!seen.insert(row).second)
						return;
					for (auto t : md.Interfaces(row))
						if (auto base = RowOf(t))
							self(self, base);
					if (Name(row) != "IUnknown")
						rows.push_back(row);
				};
				collect(collect, entry.Row);
				body = std::format("[ComImport, Guid(\"{}\"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]\r\npublic unsafe interface {}\r\n{{\r\n",
					guid, entry.Name);
				for (auto row : rows)
					for (auto m : md.MethodsOf(row)) {
						auto s = signatureOf(m);
						body += std::format("    [PreserveSig] {} {}({});\r\n", s.Result, Narrow(md.Methods()[m - 1].Name), parameterList(s));
					}
				body += "}\r\n";
			}
			else if (rs) {
				// its vtable: its base's first
				body = std::format("#[repr(C)]\r\npub struct {}_Vtbl {{\r\n", entry.Name);
				if (!bases.empty()) {
					auto baseName = Name(bases[0]);
					body += std::format("    pub base__: {}_Vtbl,\r\n", baseName);
					uses.insert(baseName + "_Vtbl");
				}
				for (auto m : md.MethodsOf(entry.Row))
					body += std::format("    pub {}: {},\r\n", Narrow(md.Methods()[m - 1].Name), rustFunction(signatureOf(m), true));
				body += "}\r\n";
			}
			else {
				// its vtable, all of it (its bases' methods first)
				std::vector<uint32_t> methods;
				std::set<uint32_t> seen;
				auto collect = [&](auto&& self, uint32_t row) -> void {
					if (!seen.insert(row).second)
						return;
					for (auto t : md.Interfaces(row))
						if (auto base = RowOf(t))
							self(self, base);
					for (auto m : md.MethodsOf(row))
						methods.push_back(m);
				};
				collect(collect, entry.Row);
				body = std::format("class {}Vtbl(ctypes.Structure):\r\n    _fields_ = [\r\n", entry.Name);
				for (auto m : methods) {
					auto s = signatureOf(m);
					body += std::format("        (\"{}\", ctypes.WINFUNCTYPE({}, {})),\r\n", Narrow(md.Methods()[m - 1].Name), s.Result,
						parameterList(s, true));
				}
				body += "    ]\r\n";
			}
			if (!guid.empty() && !cs)
				body += comment + "IID_" + entry.Name + ": {" + guid + "}\r\n";
			break;
		}

		case Entry::Kinds::Constant:
		{
			ClrSigType ft;
			md.FieldSigType(entry.Row, ft);
			// its type: a typedef's or an enum's (HRESULT): of its value
			auto valueType = ft;
			for (int depth = 0; depth < 4 && (valueType.Element == 0x11 || valueType.Element == 0x12); depth++) {
				auto row = RowOf(valueType.Token);
				if (!row)
					break;
				if (TypeKindOf(row) == L"enum") {
					if (auto info = md.EnumInfo((ClrTableId::TypeDef << 24) | row))
						valueType = ClrSigType{ info->Underlying };
					break;
				}
				auto fields = md.FieldsOf(row);
				ClrSigType inner;
				if (fields.size() != 1 || !md.FieldSigType(fields[0], inner))
					break;
				valueType = inner;
			}
			if (auto value = md.ConstantInteger(token)) {
				auto names = LanguagePrimitive(valueType.Element);
				auto type = names ? names[index] : (cs ? "int" : rs ? "i32" : "ctypes.c_int32");
				bool isSigned = valueType.Element == 0x04 || valueType.Element == 0x06 || valueType.Element == 0x08 || valueType.Element == 0x0A ||
					valueType.Element == 0x18;
				int bits = valueType.Element == 0x04 || valueType.Element == 0x05 ? 8 : valueType.Element == 0x06 || valueType.Element == 0x07 ? 16 :
					valueType.Element == 0x0A || valueType.Element == 0x0B || valueType.Element == 0x18 || valueType.Element == 0x19 ? 64 : 32;
				auto mask = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
				auto v = (uint64_t)*value & mask;
				auto text = isSigned && *value < 0 ? std::format("{}", *value) : v < 10 ? std::format("{}", v) : std::format("0x{:X}", v);
				if (cs)
					body = std::format("public const {} {} = {};\r\n", type, entry.Name, isSigned && *value < 0 && v > 0x7FFFFFFF ?
						std::format("unchecked(({})0x{:X})", type, v) : text);
				else if (rs)
					body = std::format("pub const {}: {} = {};\r\n", entry.Name, type, text);
				else
					body = std::format("{} = {}\r\n", entry.Name, py && isSigned && *value < 0 ? std::format("{}", *value) : text);
				break;
			}
			if (auto value = Narrow(md.ConstantCSharp(token)); !value.empty()) {
				body = cs ? std::format("public const string {} = {};\r\n", entry.Name, value) :
					rs ? std::format("pub const {}: &str = {};\r\n", entry.Name, value) : std::format("{} = {}\r\n", entry.Name, value);
				break;
			}
			// a GUID
			for (auto const& a : md.CustomAttributes(token))
				if (a.Value.size() >= 18 && md.TypeName(a.AttributeType).ends_with(L"GuidAttribute")) {
					auto b = a.Value.data() + 2;
					auto guid = std::format("{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", *(uint32_t const*)b,
						*(uint16_t const*)(b + 4), *(uint16_t const*)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
					auto hex = guid;
					std::erase(hex, '-');
					if (rs)
						uses.insert("GUID");
					body = cs ? std::format("public static readonly Guid {} = new Guid(\"{}\");\r\n", entry.Name, guid) :
						rs ? std::format("pub const {}: GUID = GUID::from_u128(0x{});\r\n", entry.Name, hex) :
						std::format("{} = \"{{{}}}\"\r\n", entry.Name, guid);
					break;
				}
			if (body.empty())
				body = comment + entry.Name + ": a value of " + Narrow(md.TypeName(ft.Token)) + " the metadata has as text: not a constant of the language\r\n";
			break;
		}
	}
	if (body.empty())
		return {};

	// the types nested in it (that they use too), before it
	std::string nestedText;
	for (size_t i = 0; i < nested.size(); i++) {
		auto kind = TypeKindOf(nested[i]);
		if (kind == L"struct")
			nestedText = structOf(nested[i]) + "\r\n" + nestedText;
	}
	static char const* const names[] = { "C#", "Rust", "Python (ctypes)" };
	std::string header = comment + entry.Name + " in " + names[index] + ", of the Win32 metadata (" + entry.Namespace + ")\r\n";
	if (cs)
		header += "using System.Runtime.InteropServices;\r\n";
	else if (py)
		header += "import ctypes\r\n";
	else if ((nestedText + body).find("c_void") != std::string::npos)
		header += "use core::ffi::c_void;\r\n";
	uses.erase(entry.Name);
	std::string used;
	for (auto const& name : uses)
		used += (used.empty() ? "" : ", ") + name;
	return header + "\r\n" + nestedText + body + (used.empty() ? "" : "\r\n" + comment + "it uses: " + used + "\r\n");
}
