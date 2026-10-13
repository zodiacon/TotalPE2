#include "pch.h"
#include "Win32Api.h"
#include <mutex>

using Kind = GhidraDecompiler::TypeKind;

struct Win32Api::Data {
	PEFile Pe;
	ClrMetadata Md;
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
}

std::wstring Win32Api::MetadataPath() {
	WCHAR path[MAX_PATH];
	::GetModuleFileNameW(nullptr, path, _countof(path));
	std::wstring folder = path;
	return folder.substr(0, folder.find_last_of(L'\\') + 1) + L"Windows.Win32.winmd";
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
	if (!loaded->Pe.Open(path) || !loaded->Md.Parse(loaded->Pe) || !loaded->Md.HasMetadata())
		return nullptr;
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

Win32Api* Win32Api::Get(Machine machine) {
	static std::unique_ptr<Win32Api> apis[3];
	auto& api = apis[(int)machine];
	if (!api)
		if (auto data = Shared())
			api.reset(new Win32Api(*data, machine));
	return api.get();
}

Win32Api::Win32Api(Data& data, Machine machine) : m_Data(data), m_Machine(machine), m_Pointer(machine == Machine::X86 ? 4 : 8) {
}

// Of the machine: a type or function without [SupportedArchitecture], or with this machine in it
bool Win32Api::OfMachine(uint32_t token) {
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
				if (name.ends_with(L"System.Guid")) {
					GhidraDecompiler::Type guid{ Kind::Struct, "GUID", 16 };
					auto bytes = TypeId({ 0x14, 0, 8, { { 0x05 } } });
					guid.Fields = { { "Data1", 0, Primitive(Kind::Integer, 4, false) }, { "Data2", 4, Primitive(Kind::Integer, 2, false) },
						{ "Data3", 6, Primitive(Kind::Integer, 2, false) }, { "Data4", 8, bytes } };
					return Made("GUID", guid);
				}
				return Pointer(0);
			}
			// an interface (COM) is used through a pointer
			auto kind = m_Data.Md.TypeKind(row);
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
	auto kind = md.TypeKind(row);
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
		type.Fields = { { "lpVtbl", 0, Pointer(0) } };
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
	bool typedefOf = false;
	for (auto const& attribute : md.CustomAttributes(token))
		typedefOf |= md.TypeName(attribute.AttributeType).ends_with(L"NativeTypedefAttribute");
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
		auto kind = m_Data.Md.TypeKind(row);
		if (t.Enclosing == 0 && kind != L"class" && OfMachine((ClrTableId::TypeDef << 24) | row))
			return kind == L"interface" ? IdBit | row : IdBit | row;
	}
	return 0;
}

//
// What the metadata has, to look at
//

PCWSTR Win32Api::KindName(Entry::Kinds kind) {
	static PCWSTR const names[] = { L"Function", L"Enum", L"Struct", L"Union", L"Typedef", L"Callback", L"Interface" };
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
			// one that is exported by its ordinal only ("#660"): by the metadata's name, with the ordinal
			if (name.starts_with("#"))
				m_Entries.push_back({ Narrow(md.Methods()[m - 1].Name), Entry::Kinds::Function, module + " " + name, m });
			else
				m_Entries.push_back({ name, Entry::Kinds::Function, module, m });
		}
	for (uint32_t row = 1; row <= md.Types().size(); row++) {
		auto const& t = md.Types()[row - 1];
		if (t.Enclosing)
			continue;
		auto kind = md.TypeKind(row);
		Entry::Kinds entry;
		if (kind == L"enum")
			entry = Entry::Kinds::Enum;
		else if (kind == L"delegate")
			entry = Entry::Kinds::Callback;
		else if (kind == L"interface")
			entry = Entry::Kinds::Interface;
		else if (kind == L"struct") {
			bool typedefOf = false;
			for (auto const& attribute : md.CustomAttributes((ClrTableId::TypeDef << 24) | row))
				typedefOf |= md.TypeName(attribute.AttributeType).ends_with(L"NativeTypedefAttribute");
			entry = typedefOf ? Entry::Kinds::Typedef : (t.Flags & 0x18) == 0x10 ? Entry::Kinds::Union : Entry::Kinds::Struct;
		}
		else
			continue;	// the classes of the functions and constants, attributes
		if (ForMachine(row) != row)
			continue;	// (a type defined for another machine)
		auto where = Narrow(t.Namespace);
		if (where.starts_with("Windows.Win32."))
			where = where.substr(14);
		m_Entries.push_back({ Narrow(t.Name), entry, where, row });
	}
	std::stable_sort(m_Entries.begin(), m_Entries.end(), [](auto const& a, auto const& b) { return _stricmp(a.Name.c_str(), b.Name.c_str()) < 0; });
	for (size_t i = 0; i < m_Entries.size(); i++)
		m_EntryIndex.try_emplace(m_Entries[i].Name, i);
	return m_Entries;
}

Win32Api::Entry const* Win32Api::Find(std::string const& name) {
	Entries();
	auto it = m_EntryIndex.find(name);
	return it == m_EntryIndex.end() ? nullptr : &m_Entries[it->second];
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

std::string Win32Api::Declaration(Entry const& entry, std::vector<std::string>* lines) {
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
	// a function (or a method of an interface, or a callback's): "Type Name(\n\tType param, /* in */ ...);"
	auto signature = [&](uint32_t method, std::string const& name, std::string const& prefix, std::string const& indent) {
		ClrSigType result;
		std::vector<ClrSigType> params;
		if (!md.MethodSigTypes(method, result, params)) {
			line(indent + prefix + name + "(?);");
			return;
		}
		auto ret = TypeId(result);
		if (params.empty()) {
			line(indent + prefix + TypeText(ret) + " " + name + "(void);", Referred(ret));
			return;
		}
		line(indent + prefix + TypeText(ret) + " " + name + "(", Referred(ret));
		for (size_t i = 0; i < params.size(); i++) {
			auto id = TypeId(params[i]);
			auto flags = md.ParamFlags(method, (uint32_t)i + 1);
			std::string notes;
			if (flags & 1)
				notes += "in";
			if (flags & 2)
				notes += std::string(notes.empty() ? "" : ", ") + "out";
			if (flags & 0x10)
				notes += std::string(notes.empty() ? "" : ", ") + "optional";
			auto pname = Narrow(md.ParamName(method, (uint32_t)i + 1));
			line(std::format("{}\t{}{}{}", indent, declare(TypeText(id), pname.empty() ? std::format("param{}", i + 1) : pname),
				i + 1 < params.size() ? "," : ");", notes.empty() ? "" : "\t// " + notes), Referred(id));
		}
	};
	auto where = std::format("// {}{}", entry.Kind == Entry::Kinds::Function ? "" : "Windows.Win32.", entry.Where);

	switch (entry.Kind) {
		case Entry::Kinds::Function:
			line(where);
			signature(entry.Row, entry.Name, "", "");
			break;

		case Entry::Kinds::Callback:
			line(where);
			for (auto m : md.MethodsOf(entry.Row))
				if (md.Methods()[m - 1].Name == L"Invoke") {
					signature(m, "(*" + entry.Name + ")", "typedef ", "");
					break;
				}
			break;

		case Entry::Kinds::Interface:
		{
			line(where);
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
				line(where);
				line("// (not described)");
				break;
			}
			line(where);
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
				for (auto const& f : type.Fields) {
					// an array's count goes after the name
					GhidraDecompiler::Type ft;
					auto typeText = TypeText(f.Type);
					std::string suffix;
					if (Describe(f.Type, ft) && ft.Kind == Kind::Array) {
						typeText = TypeText(ft.Target);
						suffix = std::format("[{}]", ft.Count);
					}
					line(std::format("\t/* 0x{:03X} */ {}{};", f.Offset, declare(typeText, f.Name), suffix), Referred(f.Type));
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
