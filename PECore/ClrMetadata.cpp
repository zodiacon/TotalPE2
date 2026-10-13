#include "pch.h"
#include "ClrMetadata.h"
#include <cstring>
#include <mutex>

namespace {
	// bounds-checked little-endian reader over a byte span
	struct Reader {
		std::span<const std::byte> Data;
		size_t Pos{ 0 };
		bool Ok{ true };

		uint32_t Read(size_t size) {
			if (!Ok || size > 4 || Pos + size > Data.size()) {
				Ok = false;
				return 0;
			}
			uint32_t v = 0;
			for (size_t i = 0; i < size; i++)
				v |= (uint32_t)std::to_integer<uint8_t>(Data[Pos + i]) << (8 * i);
			Pos += size;
			return v;
		}
	};

	std::wstring Utf8ToWide(std::string_view s) {
		if (s.empty())
			return {};
		int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(len, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
		return w;
	}

	enum Coded {
		TypeDefOrRef, HasConstant, HasCustomAttribute, HasFieldMarshal, HasDeclSecurity, MemberRefParent,
		HasSemantics, MethodDefOrRef, MemberForwarded, Implementation, CustomAttributeType, ResolutionScope,
		TypeOrMethodDef, CodedCount
	};

	// tables addressed by each coded index type, in tag order (ECMA-335 II.24.2.6); -1 = unused tag
	const std::vector<int> codedTables[CodedCount] = {
		{ 0x02, 0x01, 0x1B },
		{ 0x04, 0x08, 0x17 },
		{ 0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B },
		{ 0x04, 0x08 },
		{ 0x02, 0x06, 0x20 },
		{ 0x02, 0x01, 0x1A, 0x06, 0x1B },
		{ 0x14, 0x17 },
		{ 0x06, 0x0A },
		{ 0x04, 0x06 },
		{ 0x26, 0x23, 0x27 },
		{ -1, -1, 0x06, 0x0A, -1 },
		{ 0x00, 0x1A, 0x23, 0x01 },
		{ 0x02, 0x06 },
	};
	const int codedBits[CodedCount] = { 2, 2, 5, 1, 2, 3, 1, 1, 1, 2, 3, 2, 1 };

	// the parts of the flags (ECMA-335 II.23.1)
	PCWSTR MemberAccess(uint32_t flags) {
		static const PCWSTR names[] = { L"privatescope", L"private", L"famandassem", L"assembly", L"family", L"famorassem", L"public", L"" };
		return names[flags & 7];
	}

	constexpr uint32_t TokenTable(uint32_t token) {
		return token >> 24;
	}
	constexpr uint32_t TokenRow(uint32_t token) {
		return token & 0xFFFFFF;
	}
}

// Reads the columns of a row, one after the other
struct ClrRowReader {
	ClrMetadata const& Md;
	Reader R;

	ClrRowReader(ClrMetadata const& md, int table, uint32_t row) : Md(md), R{ md.m_TableData, md.RowOffset(table, row) } {
		if (row == 0 || row > md.m_Rows[table])
			R.Ok = false;
	}
	uint32_t U16() { return R.Read(2); }
	uint32_t U32() { return R.Read(4); }
	uint32_t Str() { return R.Read(Md.m_StrIdx); }
	uint32_t Blob() { return R.Read(Md.m_BlobIdx); }
	uint32_t Guid() { return R.Read(Md.m_GuidIdx); }
	uint32_t Simple(int table) { return R.Read(Md.SimpleSize(table)); }
	// a coded index, as a token (0 if it is null)
	uint32_t Coded(int coded) { return Md.CodedToken(coded, R.Read(Md.CodedSize(coded))); }
	void Skip(size_t size) { R.Pos += size; }
};

// Decodes the signatures of the #Blob heap (ECMA-335 II.23.2) into text. The generic parameters of the type and method
// that a signature belongs to give their names to !0 and !!0.
struct ClrSignature {
	ClrMetadata const& Md;
	std::span<const std::byte> Data;
	std::vector<std::wstring> const* TypeParams{ nullptr };
	std::vector<std::wstring> const* MethodParams{ nullptr };
	size_t Pos{ 0 };
	bool Ok{ true };

	uint8_t Byte() {
		if (Pos >= Data.size()) {
			Ok = false;
			return 0;
		}
		return std::to_integer<uint8_t>(Data[Pos++]);
	}
	uint8_t Peek() const {
		return Pos < Data.size() ? std::to_integer<uint8_t>(Data[Pos]) : 0;
	}
	uint32_t Compressed() {
		auto b = Byte();
		if ((b & 0x80) == 0)
			return b;
		if ((b & 0xC0) == 0x80)
			return ((b & 0x3F) << 8) | Byte();
		uint32_t v = (b & 0x1F) << 24;
		v |= Byte() << 16;
		v |= Byte() << 8;
		return v | Byte();
	}
	int32_t Signed() {
		auto first = Peek();
		auto v = Compressed();
		// the sign is the lowest bit, the value is rotated (II.23.2)
		int bits = (first & 0x80) == 0 ? 7 : (first & 0xC0) == 0x80 ? 14 : 29;
		bool negative = v & 1;
		v >>= 1;
		return negative ? (int32_t)(v | (~0u << (bits - 1))) : (int32_t)v;
	}
	// TypeDefOrRefOrSpecEncoded
	uint32_t TypeToken() {
		auto v = Compressed();
		static const uint32_t tables[] = { ClrTableId::TypeDef, ClrTableId::TypeRef, ClrTableId::TypeSpec, 0 };
		return (tables[v & 3] << 24) | (v >> 2);
	}

	static std::wstring Param(std::vector<std::wstring> const* names, uint32_t n, PCWSTR prefix) {
		if (names && n < names->size() && !(*names)[n].empty())
			return (*names)[n];
		return std::format(L"{}{}", prefix, n);
	}

	std::wstring Type(int depth = 0) {
		if (depth > 64 || !Ok) {
			Ok = false;
			return L"?";
		}
		auto et = Byte();
		switch (et) {
			case 0x01: return L"void";
			case 0x02: return L"bool";
			case 0x03: return L"char";
			case 0x04: return L"int8";
			case 0x05: return L"uint8";
			case 0x06: return L"int16";
			case 0x07: return L"uint16";
			case 0x08: return L"int32";
			case 0x09: return L"uint32";
			case 0x0A: return L"int64";
			case 0x0B: return L"uint64";
			case 0x0C: return L"float32";
			case 0x0D: return L"float64";
			case 0x0E: return L"string";
			case 0x0F: return Type(depth + 1) + L"*";
			case 0x10: return Type(depth + 1) + L"&";
			case 0x11: return L"valuetype " + Md.TypeName(TypeToken());
			case 0x12: return L"class " + Md.TypeName(TypeToken());
			case 0x13: return Param(TypeParams, Compressed(), L"!");
			case 0x1E: return Param(MethodParams, Compressed(), L"!!");
			case 0x14:
			{
				auto type = Type(depth + 1);
				auto rank = Compressed();
				std::vector<uint32_t> sizes(std::min<uint32_t>(Compressed(), 32));
				for (auto& s : sizes)
					s = Compressed();
				std::vector<int32_t> bounds(std::min<uint32_t>(Compressed(), 32));
				for (auto& b : bounds)
					b = Signed();
				std::wstring dims;
				for (uint32_t i = 0; i < rank && i < 32; i++) {
					if (i)
						dims += L",";
					if (i < bounds.size() || i < sizes.size()) {
						int32_t low = i < bounds.size() ? bounds[i] : 0;
						dims += i < sizes.size() ? std::format(L"{}...{}", low, low + (int64_t)sizes[i] - 1) : std::format(L"{}...", low);
					}
				}
				return type + L"[" + dims + L"]";
			}
			case 0x15:
			{
				auto kind = Byte();
				auto type = (kind == 0x11 ? L"valuetype " : L"class ") + Md.TypeName(TypeToken());
				auto count = Compressed();
				std::wstring args;
				for (uint32_t i = 0; i < count && Ok && i < 64; i++) {
					if (i)
						args += L", ";
					args += Type(depth + 1);
				}
				return type + L"<" + args + L">";
			}
			case 0x16: return L"typedref";
			case 0x18: return L"native int";
			case 0x19: return L"native uint";
			case 0x1B:
			{
				std::wstring name;
				return L"method " + Method(name, nullptr, depth + 1, true);
			}
			case 0x1C: return L"object";
			case 0x1D: return Type(depth + 1) + L"[]";
			case 0x1F:
			case 0x20:
			{
				auto modifier = Md.TypeName(TypeToken());
				return Type(depth + 1) + (et == 0x1F ? L" modreq(" : L" modopt(") + modifier + L")";
			}
			case 0x45: return Type(depth + 1) + L" pinned";
			case 0x41: return L"...";
		}
		Ok = false;
		return std::format(L"(0x{:02X})", et);
	}

	// C#: a type as C# writes it ("int", "List<string>", "ref int"), with what kind of value it is
	ClrGenericContext Context;

	ClrTypeInfo CsType(int depth = 0) {
		using K = ClrValueKind;
		if (depth > 64 || !Ok) {
			Ok = false;
			return { L"?" };
		}
		auto et = Byte();
		auto simple = [](PCWSTR name, K kind) { return ClrTypeInfo{ name, kind }; };
		switch (et) {
			case 0x01: return simple(L"void", K::Void);
			case 0x02: return simple(L"bool", K::Bool);
			case 0x03: return simple(L"char", K::Char);
			case 0x04: return simple(L"sbyte", K::Integer);
			case 0x05: return simple(L"byte", K::Integer);
			case 0x06: return simple(L"short", K::Integer);
			case 0x07: return simple(L"ushort", K::Integer);
			case 0x08: return simple(L"int", K::Integer);
			case 0x09: return simple(L"uint", K::Integer);
			case 0x0A: return simple(L"long", K::Integer);
			case 0x0B: return simple(L"ulong", K::Integer);
			case 0x0C: return simple(L"float", K::Float);
			case 0x0D: return simple(L"double", K::Float);
			case 0x0E: return simple(L"string", K::String);
			case 0x0F: return { CsType(depth + 1).Name + L"*", K::Pointer };
			case 0x10: return { L"ref " + CsType(depth + 1).Name, K::ByRef };
			case 0x11: case 0x12:
			{
				auto token = TypeToken();
				auto type = Md.CSharpType(token, Context);
				// a value type of another assembly may be an enum
				if (et == 0x11 && type.Kind == K::Unknown && TokenTable(token) == ClrTableId::TypeRef && Md.EnumInfo(token))
					type.Kind = K::Enum;
				if (et == 0x12 && type.Kind != K::String)
					type.Kind = K::Object;
				else if (et == 0x11 && type.Kind != K::Enum && type.Kind != K::Bool && type.Kind != K::Char &&
					type.Kind != K::Integer && type.Kind != K::Float)
					type.Kind = K::Struct;
				return type;
			}
			case 0x13: case 0x1E:
			{
				auto n = Compressed();
				auto names = et == 0x13 ? Context.TypeNames : Context.MethodNames;
				auto kinds = et == 0x13 ? Context.TypeKinds : Context.MethodKinds;
				ClrTypeInfo type{ names && n < names->size() && !(*names)[n].empty() ? (*names)[n] :
					std::format(L"{}{}", et == 0x13 ? L"T" : L"M", n), K::Generic };
				if (kinds && n < kinds->size())
					type.Kind = (*kinds)[n];
				return type;
			}
			case 0x14:
			{
				auto type = CsType(depth + 1);
				auto rank = Compressed();
				for (auto count = std::min<uint32_t>(Compressed(), 32); count--; )
					Compressed();
				for (auto count = std::min<uint32_t>(Compressed(), 32); count--; )
					Signed();
				return { type.Name + L"[" + std::wstring(rank ? std::min<uint32_t>(rank, 32) - 1 : 0, L',') + L"]", K::Object };
			}
			case 0x15:
			{
				auto kind = Byte();
				auto token = TypeToken();
				auto count = Compressed();
				std::vector<ClrTypeInfo> args;
				for (uint32_t i = 0; i < count && Ok && i < 64; i++)
					args.push_back(CsType(depth + 1));
				auto base = Md.CSharpType(token, Context);
				if (base.Name == L"Nullable" && args.size() == 1)
					return { args[0].Name + L"?", K::Struct, token };
				std::wstring text = base.Name + L"<";
				for (size_t i = 0; i < args.size(); i++)
					text += (i ? L", " : L"") + args[i].Name;
				return { text + L">", kind == 0x11 ? K::Struct : K::Object, token };
			}
			case 0x16: return simple(L"TypedReference", K::Struct);
			case 0x18: return simple(L"IntPtr", K::Integer);
			case 0x19: return simple(L"UIntPtr", K::Integer);
			case 0x1B:
			{
				std::wstring name;
				Method(name, nullptr, depth + 1, true);	// what it points to is not written
				return simple(L"IntPtr", K::Pointer);
			}
			case 0x1C: return simple(L"object", K::Object);
			case 0x1D: return { CsType(depth + 1).Name + L"[]", K::Object };
			case 0x1F: case 0x20:
				TypeToken();	// a modifier (volatile, const): C# does not write it
				return CsType(depth + 1);
			case 0x45: return CsType(depth + 1);	// pinned
			case 0x41: return simple(L"__arglist", K::Unknown);
		}
		Ok = false;
		return { L"?" };
	}

	// C#: a method's signature
	ClrSignatureInfo CsMethod() {
		ClrSignatureInfo info;
		auto conv = Byte();
		info.HasThis = (conv & 0x20) != 0;
		info.Vararg = (conv & 0x0F) == 5;
		if (conv & 0x10)
			info.GenericCount = Compressed();
		auto count = Compressed();
		info.Return = CsType();
		for (uint32_t i = 0; i < count && Ok && i < 1024; i++) {
			if (Peek() == 0x41)
				Byte();	// the sentinel before the extra arguments of a vararg call
			info.Params.push_back(CsType());
		}
		return info;
	}

	// a method signature: "instance void Name<T>(int32 a, string)". The names of the parameters, if known, are
	// filled in. A function pointer type has a '*' where the name is.
	std::wstring Method(std::wstring const& name, std::vector<std::wstring> const* paramNames, int depth = 0, bool pointer = false) {
		auto conv = Byte();
		std::wstring text;
		if (conv & 0x20)
			text += L"instance ";
		if (conv & 0x40)
			text += L"explicit ";
		switch (conv & 0x0F) {
			case 1: text += L"unmanaged cdecl "; break;
			case 2: text += L"unmanaged stdcall "; break;
			case 3: text += L"unmanaged thiscall "; break;
			case 4: text += L"unmanaged fastcall "; break;
			case 5: text += L"vararg "; break;
		}
		if (conv & 0x10)
			Compressed();	// the number of generic parameters: the name has them
		auto count = Compressed();
		text += Type(depth + 1);
		text += L" ";
		text += pointer ? L"*" : name;
		text += L"(";
		for (uint32_t i = 0; i < count && Ok && i < 1024; i++) {
			if (i)
				text += L", ";
			if (Peek() == 0x41) {	// the sentinel before the extra arguments of a vararg call
				Byte();
				text += L"..., ";
			}
			text += Type(depth + 1);
			if (paramNames && i < paramNames->size() && !(*paramNames)[i].empty())
				text += L" " + (*paramNames)[i];
		}
		return text + L")";
	}
};

PCWSTR ClrMetadata::TableName(int id) {
	static const PCWSTR names[] = {
		L"Module", L"TypeRef", L"TypeDef", L"FieldPtr", L"Field", L"MethodPtr", L"MethodDef", L"ParamPtr", L"Param",
		L"InterfaceImpl", L"MemberRef", L"Constant", L"CustomAttribute", L"FieldMarshal", L"DeclSecurity",
		L"ClassLayout", L"FieldLayout", L"StandAloneSig", L"EventMap", L"EventPtr", L"Event", L"PropertyMap",
		L"PropertyPtr", L"Property", L"MethodSemantics", L"MethodImpl", L"ModuleRef", L"TypeSpec", L"ImplMap",
		L"FieldRVA", L"EncLog", L"EncMap", L"Assembly", L"AssemblyProcessor", L"AssemblyOS", L"AssemblyRef",
		L"AssemblyRefProcessor", L"AssemblyRefOS", L"File", L"ExportedType", L"ManifestResource", L"NestedClass",
		L"GenericParam", L"MethodSpec", L"GenericParamConstraint",
	};
	if (id >= 0x30 && id < 0x38)
		return L"(Portable PDB table)";
	return id >= 0 && id < (int)_countof(names) ? names[id] : L"(Unknown)";
}

bool ClrMetadata::Parse(PEFile const& pe) {
	auto dirs = pe.GetDataDirs();
	if (!dirs || dirs->size() <= IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR)
		return false;
	auto& dd = (*dirs)[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].DataDir;
	if (dd.VirtualAddress == 0 || dd.Size < sizeof(IMAGE_COR20_HEADER))
		return false;

	auto off = pe.GetOffsetFromRVA(dd.VirtualAddress);
	if (off == 0 || off + sizeof(IMAGE_COR20_HEADER) > pe.GetFileSize())
		return false;
	if (!pe.Read((uint32_t)off, sizeof(m_Header), &m_Header))
		return false;
	m_HasHeader = true;
	m_PE = &pe;

	auto& md = m_Header.MetaData;
	if (md.VirtualAddress == 0 || md.Size < 16)
		return true;
	auto mdOff = pe.GetOffsetFromRVA(md.VirtualAddress);
	if (mdOff == 0 || mdOff >= pe.GetFileSize())
		return true;
	auto size = (uint32_t)std::min<uint64_t>(md.Size, pe.GetFileSize() - mdOff);
	auto span = pe.GetSpan((uint32_t)mdOff, size);
	m_MetadataRva = md.VirtualAddress;

	Reader r{ span };
	if (r.Read(4) != 0x424A5342)	// "BSJB"
		return true;
	r.Read(2); r.Read(2); r.Read(4);	// major, minor, reserved
	auto verLen = r.Read(4);
	if (!r.Ok || r.Pos + verLen > span.size())
		return true;
	auto ver = reinterpret_cast<const char*>(span.data()) + r.Pos;
	m_Version = Utf8ToWide(std::string_view(ver, strnlen(ver, verLen)));
	r.Pos += verLen;
	r.Read(2);	// flags
	auto count = r.Read(2);
	if (!r.Ok)
		return true;
	m_HasMetadata = true;

	for (uint32_t i = 0; i < count; i++) {
		ClrStream s;
		s.Offset = r.Read(4);
		s.Size = r.Read(4);
		if (!r.Ok)
			break;
		while (r.Pos < span.size()) {
			char c = (char)span[r.Pos++];
			if (c == 0)
				break;
			s.Name += c;
		}
		r.Pos = (r.Pos + 3) & ~size_t(3);
		m_Streams.push_back(std::move(s));
	}

	// a heap that is not inside the metadata is ignored
	auto heap = [&](ClrStream const& s) {
		return (uint64_t)s.Offset + s.Size <= span.size() ? span.subspan(s.Offset, s.Size) : std::span<const std::byte>();
	};
	ClrStream const* tables = nullptr;
	for (auto& s : m_Streams) {
		if (s.Name == "#~" || s.Name == "#-")
			tables = &s;
		else if (s.Name == "#Strings")
			m_StringHeap = heap(s);
		else if (s.Name == "#Blob")
			m_BlobHeap = heap(s);
		else if (s.Name == "#US") {
			m_UserStrings = heap(s);
			m_UserStringsOffset = s.Offset;
		}
	}
	if (tables && (uint64_t)tables->Offset + tables->Size <= span.size())
		ParseTables(span, *tables);
	return true;
}

std::wstring ClrMetadata::String(uint32_t index) const {
	if (index >= m_StringHeap.size())
		return {};
	auto p = reinterpret_cast<const char*>(m_StringHeap.data()) + index;
	return Utf8ToWide(std::string_view(p, strnlen(p, m_StringHeap.size() - index)));
}

std::span<const std::byte> ClrMetadata::Blob(uint32_t index) const {
	ClrSignature s{ *this, m_BlobHeap };
	s.Pos = index;
	auto size = s.Compressed();
	if (!s.Ok || s.Pos + size > m_BlobHeap.size())
		return {};
	return m_BlobHeap.subspan(s.Pos, size);
}

uint32_t ClrMetadata::SimpleSize(int table) const {
	return m_Rows[table] > 0xFFFF ? 4u : 2u;
}

uint32_t ClrMetadata::CodedSize(int coded) const {
	uint32_t max = 0;
	for (auto t : codedTables[coded])
		if (t >= 0)
			max = std::max(max, m_Rows[t]);
	return max < (1u << (16 - codedBits[coded])) ? 2u : 4u;
}

uint32_t ClrMetadata::CodedToken(int coded, uint32_t value) const {
	auto bits = codedBits[coded];
	auto tag = value & ((1u << bits) - 1);
	auto row = value >> bits;
	if (row == 0 || tag >= codedTables[coded].size() || codedTables[coded][tag] < 0)
		return 0;
	return ((uint32_t)codedTables[coded][tag] << 24) | row;
}

size_t ClrMetadata::RowOffset(int table, uint32_t row) const {
	if (row == 0 || row > m_Rows[table] || m_Start[table] == 0)
		return m_TableData.size();	// nothing can be read there
	return m_Start[table] + (size_t)(row - 1) * m_RowSize[table];
}

uint32_t ClrMetadata::Indirect(int ptrTable, uint32_t row) const {
	if (m_Rows[ptrTable] == 0)
		return row;
	ClrRowReader r(*this, ptrTable, row);
	return r.Simple(ptrTable + 1);	// FieldPtr -> Field, MethodPtr -> MethodDef, ParamPtr -> Param
}

bool ClrMetadata::ParseTables(std::span<const std::byte> md, ClrStream const& stream) {
	Reader r{ md.subspan(stream.Offset, stream.Size) };
	r.Read(4);	// reserved
	r.Read(1); r.Read(1);	// major, minor
	auto heapSizes = r.Read(1);
	r.Read(1);
	auto vlo = r.Read(4), vhi = r.Read(4);
	uint64_t valid = vlo | ((uint64_t)vhi << 32);
	r.Read(4); r.Read(4);	// sorted
	if (!r.Ok)
		return false;

	auto& rows = m_Rows;
	for (int i = 0; i < 64; i++) {
		if (valid & (1ULL << i)) {
			rows[i] = r.Read(4);
			if (!r.Ok)
				return false;
			m_Tables.push_back({ i, rows[i] });
		}
	}
	if (heapSizes & 0x40)
		r.Read(4);	// extra data

	m_StrIdx = (heapSizes & 1) ? 4u : 2u;
	m_GuidIdx = (heapSizes & 2) ? 4u : 2u;
	m_BlobIdx = (heapSizes & 4) ? 4u : 2u;
	auto strIdx = m_StrIdx, guidIdx = m_GuidIdx, blobIdx = m_BlobIdx;
	auto simple = [&](int t) { return SimpleSize(t); };
	auto coded = [&](Coded c) { return CodedSize(c); };

	// row sizes (ECMA-335 II.22)
	auto& sz = m_RowSize;
	sz[0x00] = 2 + strIdx + 3 * guidIdx;
	sz[0x01] = coded(ResolutionScope) + 2 * strIdx;
	sz[0x02] = 4 + 2 * strIdx + coded(TypeDefOrRef) + simple(0x04) + simple(0x06);
	sz[0x03] = simple(0x04);
	sz[0x04] = 2 + strIdx + blobIdx;
	sz[0x05] = simple(0x06);
	sz[0x06] = 4 + 2 + 2 + strIdx + blobIdx + simple(0x08);
	sz[0x07] = simple(0x08);
	sz[0x08] = 2 + 2 + strIdx;
	sz[0x09] = simple(0x02) + coded(TypeDefOrRef);
	sz[0x0A] = coded(MemberRefParent) + strIdx + blobIdx;
	sz[0x0B] = 2 + coded(HasConstant) + blobIdx;
	sz[0x0C] = coded(HasCustomAttribute) + coded(CustomAttributeType) + blobIdx;
	sz[0x0D] = coded(HasFieldMarshal) + blobIdx;
	sz[0x0E] = 2 + coded(HasDeclSecurity) + blobIdx;
	sz[0x0F] = 2 + 4 + simple(0x02);
	sz[0x10] = 4 + simple(0x04);
	sz[0x11] = blobIdx;
	sz[0x12] = simple(0x02) + simple(0x14);
	sz[0x13] = simple(0x14);
	sz[0x14] = 2 + strIdx + coded(TypeDefOrRef);
	sz[0x15] = simple(0x02) + simple(0x17);
	sz[0x16] = simple(0x17);
	sz[0x17] = 2 + strIdx + blobIdx;
	sz[0x18] = 2 + simple(0x06) + coded(HasSemantics);
	sz[0x19] = simple(0x02) + 2 * coded(MethodDefOrRef);
	sz[0x1A] = strIdx;
	sz[0x1B] = blobIdx;
	sz[0x1C] = 2 + coded(MemberForwarded) + strIdx + simple(0x1A);
	sz[0x1D] = 4 + simple(0x04);
	sz[0x1E] = 8;
	sz[0x1F] = 4;
	sz[0x20] = 4 + 8 + 4 + blobIdx + 2 * strIdx;
	sz[0x21] = 4;
	sz[0x22] = 12;
	sz[0x23] = 8 + 4 + blobIdx + 2 * strIdx + blobIdx;
	sz[0x24] = 4 + simple(0x23);
	sz[0x25] = 12 + simple(0x23);
	sz[0x26] = 4 + strIdx + blobIdx;
	sz[0x27] = 4 + 4 + 2 * strIdx + coded(Implementation);
	sz[0x28] = 4 + 4 + strIdx + coded(Implementation);
	sz[0x29] = 2 * simple(0x02);
	sz[0x2A] = 2 + 2 + coded(TypeOrMethodDef) + strIdx;
	sz[0x2B] = coded(MethodDefOrRef) + blobIdx;
	sz[0x2C] = simple(0x2A) + coded(TypeDefOrRef);

	// tables above 0x2C (portable PDB) have no size here, so stop there. A table that does not fit is left out.
	m_TableData = r.Data;
	size_t pos = r.Pos;
	for (int i = 0; i < 0x2D; i++) {
		if (!(valid & (1ULL << i)))
			continue;
		auto bytes = (uint64_t)rows[i] * sz[i];
		if (pos + bytes > m_TableData.size()) {
			for (int j = i; j < 64; j++)
				rows[j] = 0;
			break;
		}
		m_Start[i] = pos;
		pos += (size_t)bytes;
	}

	if (rows[0x00]) {
		ClrRowReader m(*this, 0x00, 1);
		m.U16();
		m_ModuleName = String(m.Str());
	}

	// Assembly: HashAlgId(4), version(8), Flags(4), PublicKey, Name, Culture
	// AssemblyRef: version(8), Flags(4), PublicKeyOrToken, Name, Culture, HashValue
	auto readAsm = [&](int table, uint32_t row, bool isRef) {
		ClrAssemblyInfo a;
		ClrRowReader rr(*this, table, row);
		if (!isRef)
			rr.U32();
		a.Major = (uint16_t)rr.U16();
		a.Minor = (uint16_t)rr.U16();
		a.Build = (uint16_t)rr.U16();
		a.Revision = (uint16_t)rr.U16();
		a.Flags = rr.U32();
		rr.Blob();
		a.Name = String(rr.Str());
		a.Culture = String(rr.Str());
		return a;
	};

	if (rows[0x20]) {
		m_Assembly = readAsm(0x20, 1, false);
		m_HasAssembly = true;
	}
	for (uint32_t i = 1; i <= rows[0x23] && i <= 4096; i++)
		m_Refs.push_back(readAsm(0x23, i, true));

	// the types and their members: a type's fields and methods run up to those of the next type
	auto fieldList = rows[ClrTableId::FieldPtr] ? rows[ClrTableId::FieldPtr] : rows[ClrTableId::Field];
	auto methodList = rows[ClrTableId::MethodPtr] ? rows[ClrTableId::MethodPtr] : rows[ClrTableId::MethodDef];
	auto paramList = rows[ClrTableId::ParamPtr] ? rows[ClrTableId::ParamPtr] : rows[ClrTableId::Param];
	m_Types.resize(rows[ClrTableId::TypeDef]);
	for (uint32_t i = 0; i < rows[ClrTableId::TypeDef]; i++) {
		ClrRowReader t(*this, ClrTableId::TypeDef, i + 1);
		auto& type = m_Types[i];
		type.Flags = t.U32();
		type.Name = String(t.Str());
		type.Namespace = String(t.Str());
		type.Extends = t.Coded(TypeDefOrRef);
		type.FirstField = t.Simple(ClrTableId::Field);
		type.FirstMethod = t.Simple(ClrTableId::MethodDef);
	}
	for (size_t i = 0; i < m_Types.size(); i++) {
		auto& type = m_Types[i];
		type.EndField = i + 1 < m_Types.size() ? m_Types[i + 1].FirstField : fieldList + 1;
		type.EndMethod = i + 1 < m_Types.size() ? m_Types[i + 1].FirstMethod : methodList + 1;
		// damaged lists are empty
		type.FirstField = std::min(type.FirstField, fieldList + 1);
		type.EndField = std::clamp(type.EndField, type.FirstField, fieldList + 1);
		type.FirstMethod = std::min(type.FirstMethod, methodList + 1);
		type.EndMethod = std::clamp(type.EndMethod, type.FirstMethod, methodList + 1);
	}

	m_Methods.resize(rows[ClrTableId::MethodDef]);
	for (uint32_t i = 0; i < rows[ClrTableId::MethodDef]; i++) {
		ClrRowReader m(*this, ClrTableId::MethodDef, i + 1);
		auto& method = m_Methods[i];
		method.Rva = m.U32();
		method.ImplFlags = (uint16_t)m.U16();
		method.Flags = (uint16_t)m.U16();
		method.Name = String(m.Str());
		method.Signature = m.Blob();
		method.FirstParam = m.Simple(ClrTableId::Param);
	}
	for (size_t i = 0; i < m_Methods.size(); i++) {
		auto& method = m_Methods[i];
		method.EndParam = i + 1 < m_Methods.size() ? m_Methods[i + 1].FirstParam : paramList + 1;
		method.FirstParam = std::min(method.FirstParam, paramList + 1);
		method.EndParam = std::clamp(method.EndParam, method.FirstParam, paramList + 1);
	}

	m_Fields.resize(rows[ClrTableId::Field]);
	for (uint32_t i = 0; i < rows[ClrTableId::Field]; i++) {
		ClrRowReader f(*this, ClrTableId::Field, i + 1);
		auto& field = m_Fields[i];
		field.Flags = (uint16_t)f.U16();
		field.Name = String(f.Str());
		field.Signature = f.Blob();
	}

	// the owners of the members
	for (uint32_t t = 0; t < m_Types.size(); t++) {
		auto& type = m_Types[t];
		for (auto i = type.FirstMethod; i < type.EndMethod; i++)
			if (auto m = Indirect(ClrTableId::MethodPtr, i); m && m <= m_Methods.size())
				m_Methods[m - 1].Type = t + 1;
		for (auto i = type.FirstField; i < type.EndField; i++)
			if (auto f = Indirect(ClrTableId::FieldPtr, i); f && f <= m_Fields.size())
				m_Fields[f - 1].Type = t + 1;
	}

	for (uint32_t i = 1; i <= rows[ClrTableId::NestedClass]; i++) {
		ClrRowReader n(*this, ClrTableId::NestedClass, i);
		auto nested = n.Simple(ClrTableId::TypeDef);
		auto enclosing = n.Simple(ClrTableId::TypeDef);
		if (nested && nested <= m_Types.size() && enclosing != nested && enclosing <= m_Types.size())
			m_Types[nested - 1].Enclosing = enclosing;
	}

	for (uint32_t i = 1; i <= rows[ClrTableId::GenericParam]; i++) {
		ClrRowReader g(*this, ClrTableId::GenericParam, i);
		auto number = g.U16();
		g.U16();	// flags
		auto owner = g.Coded(TypeOrMethodDef);
		auto name = String(g.Str());
		if (!owner || number > 1024)
			continue;
		auto& names = m_GenericParams[owner];
		if (names.size() <= number)
			names.resize(number + 1);
		names[number] = std::move(name);
	}

	for (uint32_t i = 1; i <= rows[ClrTableId::InterfaceImpl]; i++) {
		ClrRowReader n(*this, ClrTableId::InterfaceImpl, i);
		auto type = n.Simple(ClrTableId::TypeDef);
		auto itf = n.Coded(TypeDefOrRef);
		if (type && itf)
			m_Interfaces.emplace_back(type, itf);
	}
	std::stable_sort(m_Interfaces.begin(), m_Interfaces.end(), [](auto& a, auto& b) { return a.first < b.first; });

	for (uint32_t i = 1; i <= rows[0x0B]; i++) {	// Constant
		ClrRowReader c(*this, 0x0B, i);
		c.U16();	// the type and padding
		if (auto parent = c.Coded(HasConstant))
			m_Constants.emplace(parent, i);
	}

	// the properties and events: each map row gives a type the ones from its list up to the next row's
	m_Properties.resize(rows[ClrTableId::Property]);
	for (uint32_t i = 0; i < rows[ClrTableId::Property]; i++) {
		ClrRowReader p(*this, ClrTableId::Property, i + 1);
		auto& property = m_Properties[i];
		property.Flags = (uint16_t)p.U16();
		property.Name = String(p.Str());
		property.Signature = p.Blob();
	}
	m_Events.resize(rows[ClrTableId::Event]);
	for (uint32_t i = 0; i < rows[ClrTableId::Event]; i++) {
		ClrRowReader e(*this, ClrTableId::Event, i + 1);
		auto& event = m_Events[i];
		event.Flags = (uint16_t)e.U16();
		event.Name = String(e.Str());
		event.EventType = e.Coded(TypeDefOrRef);
	}
	m_TypeProperties.assign(m_Types.size() + 1, {});
	m_TypeEvents.assign(m_Types.size() + 1, {});
	auto maps = [&](int mapTable, int listTable, int ptrTable, auto& lists, auto& members) {
		auto count = rows[ptrTable] ? rows[ptrTable] : rows[listTable];
		std::vector<std::pair<uint32_t, uint32_t>> starts;	// type, first
		for (uint32_t i = 1; i <= rows[mapTable]; i++) {
			ClrRowReader m(*this, mapTable, i);
			auto type = m.Simple(ClrTableId::TypeDef);
			auto first = m.Simple(listTable);
			if (m.R.Ok)
				starts.emplace_back(type, first);
		}
		for (size_t i = 0; i < starts.size(); i++) {
			auto [type, first] = starts[i];
			auto end = i + 1 < starts.size() ? starts[i + 1].second : count + 1;
			if (type == 0 || type > m_Types.size())
				continue;
			for (auto row = first; row < end && row <= count; row++)
				if (auto member = Indirect(ptrTable, row); member && member <= members.size()) {
					members[member - 1].Type = type;
					lists[type].push_back(member);
				}
		}
	};
	maps(ClrTableId::PropertyMap, ClrTableId::Property, ClrTableId::PropertyPtr, m_TypeProperties, m_Properties);
	maps(ClrTableId::EventMap, ClrTableId::Event, ClrTableId::EventPtr, m_TypeEvents, m_Events);

	for (uint32_t i = 1; i <= rows[ClrTableId::MethodSemantics]; i++) {
		ClrRowReader s(*this, ClrTableId::MethodSemantics, i);
		auto semantics = s.U16();
		auto method = s.Simple(ClrTableId::MethodDef);
		auto association = s.Coded(HasSemantics);
		if (!s.R.Ok || method == 0 || method > m_Methods.size())
			continue;
		auto row = TokenRow(association);
		ClrAccessor accessor = ClrAccessor::Other;
		if (TokenTable(association) == ClrTableId::Property && row && row <= m_Properties.size()) {
			auto& property = m_Properties[row - 1];
			if (semantics & 0x02)
				accessor = ClrAccessor::Getter, property.Getter = method;
			else if (semantics & 0x01)
				accessor = ClrAccessor::Setter, property.Setter = method;
		}
		else if (TokenTable(association) == ClrTableId::Event && row && row <= m_Events.size()) {
			auto& event = m_Events[row - 1];
			if (semantics & 0x08)
				accessor = ClrAccessor::Adder, event.Adder = method;
			else if (semantics & 0x10)
				accessor = ClrAccessor::Remover, event.Remover = method;
			else if (semantics & 0x20)
				accessor = ClrAccessor::Raiser, event.Raiser = method;
		}
		else
			continue;
		m_Accessors[method] = { association, accessor };
	}

	for (uint32_t i = 1; i <= rows[ClrTableId::CustomAttribute]; i++) {
		ClrRowReader a(*this, ClrTableId::CustomAttribute, i);
		if (auto parent = a.Coded(HasCustomAttribute); parent && a.R.Ok)
			m_Attributes.emplace_back(parent, i);
	}
	std::stable_sort(m_Attributes.begin(), m_Attributes.end(), [](auto& a, auto& b) { return a.first < b.first; });
	return true;
}

uint32_t ClrMetadata::EntryPointMethod() const {
	if (m_Header.Flags & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT)
		return 0;
	auto token = m_Header.EntryPointToken;
	return TokenTable(token) == ClrTableId::MethodDef && TokenRow(token) <= m_Methods.size() ? TokenRow(token) : 0;
}

std::vector<uint32_t> ClrMetadata::MethodsOf(uint32_t type) const {
	std::vector<uint32_t> methods;
	if (type == 0 || type > m_Types.size())
		return methods;
	for (auto i = m_Types[type - 1].FirstMethod; i < m_Types[type - 1].EndMethod; i++)
		if (auto m = Indirect(ClrTableId::MethodPtr, i); m && m <= m_Methods.size())
			methods.push_back(m);
	return methods;
}

std::vector<uint32_t> ClrMetadata::FieldsOf(uint32_t type) const {
	std::vector<uint32_t> fields;
	if (type == 0 || type > m_Types.size())
		return fields;
	for (auto i = m_Types[type - 1].FirstField; i < m_Types[type - 1].EndField; i++)
		if (auto f = Indirect(ClrTableId::FieldPtr, i); f && f <= m_Fields.size())
			fields.push_back(f);
	return fields;
}

std::vector<uint32_t> ClrMetadata::NestedTypes(uint32_t type) const {
	std::vector<uint32_t> nested;
	for (uint32_t i = 0; i < m_Types.size(); i++)
		if (m_Types[i].Enclosing == type)
			nested.push_back(i + 1);
	return nested;
}

std::vector<uint32_t> ClrMetadata::Interfaces(uint32_t type) const {
	std::vector<uint32_t> result;
	auto it = std::lower_bound(m_Interfaces.begin(), m_Interfaces.end(), type, [](auto& p, uint32_t t) { return p.first < t; });
	for (; it != m_Interfaces.end() && it->first == type; ++it)
		result.push_back(it->second);
	return result;
}

std::vector<std::wstring> const* ClrMetadata::GenericParams(uint32_t owner) const {
	auto it = m_GenericParams.find(owner);
	return it == m_GenericParams.end() ? nullptr : &it->second;
}

std::wstring ClrMetadata::TypeDefName(uint32_t row, bool withNamespace) const {
	if (row == 0 || row > m_Types.size())
		return std::format(L"(TypeDef {})", row);
	std::wstring name;
	// the enclosing types, outermost first (a loop in damaged metadata ends)
	std::vector<uint32_t> chain{ row };
	for (auto t = m_Types[row - 1].Enclosing; t && chain.size() < 32; t = m_Types[t - 1].Enclosing)
		chain.push_back(t);
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		auto& type = m_Types[*it - 1];
		if (!name.empty())
			name += L"/";
		else if (withNamespace && !type.Namespace.empty())
			name = type.Namespace + L".";
		name += type.Name;
	}
	return name;
}

std::wstring ClrMetadata::FullName(uint32_t type) const {
	return TypeDefName(type);
}

std::wstring ClrMetadata::TypeRefName(uint32_t row, int depth) const {
	ClrRowReader r(*this, ClrTableId::TypeRef, row);
	auto scope = r.Coded(ResolutionScope);
	auto name = String(r.Str());
	auto ns = String(r.Str());
	if (!r.R.Ok)
		return std::format(L"(TypeRef {})", row);
	auto full = ns.empty() ? name : ns + L"." + name;
	switch (TokenTable(scope)) {
		case ClrTableId::AssemblyRef:
		{
			ClrRowReader a(*this, ClrTableId::AssemblyRef, TokenRow(scope));
			a.Skip(12);
			a.Blob();
			return L"[" + String(a.Str()) + L"]" + full;
		}
		case ClrTableId::ModuleRef:
		{
			ClrRowReader m(*this, ClrTableId::ModuleRef, TokenRow(scope));
			return L"[.module " + String(m.Str()) + L"]" + full;
		}
		case ClrTableId::TypeRef:
			if (depth < 16)
				return TypeRefName(TokenRow(scope), depth + 1) + L"/" + name;
			break;
	}
	return full;
}

std::wstring ClrMetadata::TypeName(uint32_t token) const {
	auto row = TokenRow(token);
	switch (TokenTable(token)) {
		case ClrTableId::TypeDef:
			return TypeDefName(row);
		case ClrTableId::TypeRef:
			return TypeRefName(row, 0);
		case ClrTableId::TypeSpec:
		{
			// a TypeSpec that refers to itself would never end
			static thread_local int depth = 0;
			if (depth > 8)
				return L"?";
			ClrRowReader r(*this, ClrTableId::TypeSpec, row);
			auto blob = Blob(r.Blob());
			depth++;
			ClrSignature sig{ *this, blob };
			auto text = sig.Type();
			depth--;
			return text;
		}
	}
	return std::format(L"(token 0x{:08X})", token);
}

std::wstring ClrMetadata::MethodRefText(uint32_t parent, std::wstring const& name, std::span<const std::byte> blob, std::wstring const& generic) const {
	std::wstring owner;
	switch (TokenTable(parent)) {
		case ClrTableId::TypeDef: owner = TypeDefName(TokenRow(parent)); break;
		case ClrTableId::TypeRef: case ClrTableId::TypeSpec: owner = TypeName(parent); break;
		case ClrTableId::ModuleRef:
		{
			ClrRowReader m(*this, ClrTableId::ModuleRef, TokenRow(parent));
			owner = L"[.module " + String(m.Str()) + L"]";
			break;
		}
	}
	auto member = owner.empty() ? name : owner + L"::" + name;
	ClrSignature sig{ *this, blob };
	if (!blob.empty() && sig.Peek() == 0x06) {	// a field
		sig.Byte();
		return sig.Type() + L" " + member;
	}
	return sig.Method(member + generic, nullptr);
}

std::wstring ClrMetadata::MemberName(uint32_t token) const {
	auto row = TokenRow(token);
	switch (TokenTable(token)) {
		case ClrTableId::MethodDef:
		{
			if (row == 0 || row > m_Methods.size())
				break;
			auto& m = m_Methods[row - 1];
			ClrSignature sig{ *this, Blob(m.Signature), GenericParams((ClrTableId::TypeDef << 24) | m.Type), GenericParams(token) };
			auto name = (m.Type ? TypeDefName(m.Type) + L"::" : L"") + m.Name;
			if (auto params = GenericParams(token); params && !params->empty()) {
				name += L"<";
				for (size_t i = 0; i < params->size(); i++)
					name += (i ? L", " : L"") + (*params)[i];
				name += L">";
			}
			return sig.Method(name, nullptr);
		}
		case ClrTableId::Field:
		{
			if (row == 0 || row > m_Fields.size())
				break;
			auto& f = m_Fields[row - 1];
			ClrSignature sig{ *this, Blob(f.Signature), GenericParams((ClrTableId::TypeDef << 24) | f.Type) };
			sig.Byte();	// FIELD
			auto type = sig.Type();
			return type + L" " + (f.Type ? TypeDefName(f.Type) + L"::" : L"") + f.Name;
		}
		case ClrTableId::MemberRef:
		{
			ClrRowReader r(*this, ClrTableId::MemberRef, row);
			auto parent = r.Coded(MemberRefParent);
			auto name = String(r.Str());
			auto blob = Blob(r.Blob());
			if (!r.R.Ok)
				break;
			return MethodRefText(parent, name, blob);
		}
		case ClrTableId::MethodSpec:
		{
			ClrRowReader r(*this, ClrTableId::MethodSpec, row);
			auto method = r.Coded(MethodDefOrRef);
			auto inst = Blob(r.Blob());
			if (!r.R.Ok)
				break;
			ClrSignature args{ *this, inst };
			std::wstring generic;
			if (args.Byte() == 0x0A) {
				auto count = args.Compressed();
				generic = L"<";
				for (uint32_t i = 0; i < count && args.Ok && i < 64; i++)
					generic += (i ? L", " : L"") + args.Type();
				generic += L">";
			}
			if (TokenTable(method) == ClrTableId::MethodDef && TokenRow(method) && TokenRow(method) <= m_Methods.size()) {
				auto& m = m_Methods[TokenRow(method) - 1];
				return MethodRefText((ClrTableId::TypeDef << 24) | m.Type, m.Name, Blob(m.Signature), generic);
			}
			if (TokenTable(method) == ClrTableId::MemberRef) {
				ClrRowReader mr(*this, ClrTableId::MemberRef, TokenRow(method));
				auto parent = mr.Coded(MemberRefParent);
				auto name = String(mr.Str());
				auto blob = Blob(mr.Blob());
				if (mr.R.Ok)
					return MethodRefText(parent, name, blob, generic);
			}
			break;
		}
	}
	return std::format(L"(token 0x{:08X})", token);
}

std::optional<std::wstring> ClrMetadata::UserString(uint32_t token) const {
	auto index = TokenRow(token);
	if (TokenTable(token) != ClrTableId::UserString || index >= m_UserStrings.size())
		return {};
	ClrSignature s{ *this, m_UserStrings };
	s.Pos = index;
	auto size = s.Compressed();
	if (!s.Ok || s.Pos + size > m_UserStrings.size())
		return {};
	// UTF-16, then a byte that says whether there are characters beyond ASCII
	std::wstring text(size / 2, L'\0');
	memcpy(text.data(), m_UserStrings.data() + s.Pos, text.size() * 2);
	return text;
}

uint32_t ClrMetadata::UserStringRva(uint32_t token) const {
	auto index = TokenRow(token);
	if (TokenTable(token) != ClrTableId::UserString || index >= m_UserStrings.size())
		return 0;
	ClrSignature s{ *this, m_UserStrings };
	s.Pos = index;
	auto size = s.Compressed();
	if (!s.Ok || size < 2 || s.Pos + size > m_UserStrings.size())
		return 0;
	return m_MetadataRva + m_UserStringsOffset + (uint32_t)s.Pos;
}

std::wstring ClrMetadata::TokenText(uint32_t token) const {
	switch (TokenTable(token)) {
		case ClrTableId::TypeDef:
		case ClrTableId::TypeRef:
		case ClrTableId::TypeSpec:
			return TypeName(token);
		case ClrTableId::MethodDef:
		case ClrTableId::Field:
		case ClrTableId::MemberRef:
		case ClrTableId::MethodSpec:
			return MemberName(token);
		case ClrTableId::UserString:
			if (auto s = UserString(token))
				return L"\"" + *s + L"\"";
			break;
		case ClrTableId::StandAloneSig:
		{
			ClrRowReader r(*this, ClrTableId::StandAloneSig, TokenRow(token));
			auto blob = Blob(r.Blob());
			if (!r.R.Ok || blob.empty())
				break;
			ClrSignature sig{ *this, blob };
			return sig.Method(L"", nullptr, 0, true);
		}
	}
	return std::format(L"(token 0x{:08X})", token);
}

std::wstring ClrMetadata::TypeKind(uint32_t type) const {
	if (type == 0 || type > m_Types.size())
		return L"class";
	auto& t = m_Types[type - 1];
	if (t.Flags & 0x20)
		return L"interface";
	if (t.Extends) {
		auto base = TypeName(t.Extends);
		// the base type is in the core library (or is the core library itself)
		auto is = [&](std::wstring_view name) {
			return base == name || (base.ends_with(name) && base.size() > name.size() && base[base.size() - name.size() - 1] == L']');
		};
		if (is(L"System.Enum"))
			return L"enum";
		if (is(L"System.ValueType") && TypeDefName(type) != L"System.Enum")
			return L"struct";
		if (is(L"System.MulticastDelegate"))
			return L"delegate";
	}
	return L"class";
}

std::wstring ClrMetadata::TypeDeclaration(uint32_t type) const {
	if (type == 0 || type > m_Types.size())
		return {};
	auto& t = m_Types[type - 1];
	std::wstring text = L".class ";
	if (t.Flags & 0x20)
		text += L"interface ";
	static const PCWSTR visibility[] = { L"private", L"public", L"nested public", L"nested private", L"nested family",
		L"nested assembly", L"nested famandassem", L"nested famorassem" };
	text += visibility[t.Flags & 7];
	text += (t.Flags & 0x18) == 0x08 ? L" sequential" : (t.Flags & 0x18) == 0x10 ? L" explicit" : L" auto";
	auto format = t.Flags & 0x30000;
	text += format == 0x10000 ? L" unicode" : format == 0x20000 ? L" autochar" : L" ansi";
	if (t.Flags & 0x80)
		text += L" abstract";
	if (t.Flags & 0x100)
		text += L" sealed";
	if (t.Flags & 0x400)
		text += L" specialname";
	if (t.Flags & 0x1000)
		text += L" import";
	if (t.Flags & 0x2000)
		text += L" serializable";
	if (t.Flags & 0x100000)
		text += L" beforefieldinit";
	text += L" " + TypeDefName(type);
	if (auto params = GenericParams((ClrTableId::TypeDef << 24) | type); params && !params->empty()) {
		text += L"<";
		for (size_t i = 0; i < params->size(); i++)
			text += (i ? L", " : L"") + (*params)[i];
		text += L">";
	}
	if (t.Extends)
		text += L" extends " + TypeName(t.Extends);
	auto interfaces = Interfaces(type);
	for (size_t i = 0; i < interfaces.size(); i++)
		text += (i ? L", " : L" implements ") + TypeName(interfaces[i]);
	return text;
}

std::wstring ClrMetadata::ParamName(uint32_t method, uint32_t sequence) const {
	if (method == 0 || method > m_Methods.size())
		return {};
	auto& m = m_Methods[method - 1];
	for (auto i = m.FirstParam; i < m.EndParam; i++) {
		ClrRowReader p(*this, ClrTableId::Param, Indirect(ClrTableId::ParamPtr, i));
		p.U16();
		if (p.U16() == sequence)
			return String(p.Str());
	}
	return {};
}

std::wstring ClrMetadata::MethodSignature(uint32_t method) const {
	if (method == 0 || method > m_Methods.size())
		return {};
	auto& m = m_Methods[method - 1];
	auto token = (ClrTableId::MethodDef << 24) | method;
	std::vector<std::wstring> names;
	for (auto i = m.FirstParam; i < m.EndParam; i++) {
		ClrRowReader p(*this, ClrTableId::Param, Indirect(ClrTableId::ParamPtr, i));
		p.U16();
		auto sequence = p.U16();
		if (!p.R.Ok || sequence == 0 || sequence > 1024)
			continue;
		if (names.size() < sequence)
			names.resize(sequence);
		names[sequence - 1] = String(p.Str());
	}
	auto name = m.Name;
	if (auto params = GenericParams(token); params && !params->empty()) {
		name += L"<";
		for (size_t i = 0; i < params->size(); i++)
			name += (i ? L", " : L"") + (*params)[i];
		name += L">";
	}
	ClrSignature sig{ *this, Blob(m.Signature), GenericParams((ClrTableId::TypeDef << 24) | m.Type), GenericParams(token) };
	return sig.Method(name, &names);
}

std::wstring ClrMetadata::MethodDeclaration(uint32_t method) const {
	if (method == 0 || method > m_Methods.size())
		return {};
	auto& m = m_Methods[method - 1];
	std::wstring text = L".method ";
	text += MemberAccess(m.Flags);
	struct { uint16_t Flag; PCWSTR Text; } const flags[] = {
		{ 0x80, L"hidebysig" }, { 0x100, L"newslot" }, { 0x800, L"specialname" }, { 0x1000, L"rtspecialname" },
		{ 0x400, L"abstract" }, { 0x40, L"virtual" }, { 0x20, L"final" }, { 0x10, L"static" }, { 0x2000, L"pinvokeimpl" },
	};
	for (auto& f : flags)
		if (m.Flags & f.Flag) {
			text += std::format(L" {}", f.Text);
			// pinvokeimpl("kernel32.dll" as "CreateFileW" unicode lasterr winapi)
			if (f.Flag == 0x2000)
				if (auto native = PInvoke(method)) {
					text += L"(\"" + native->Module + L"\"";
					if (!native->EntryPoint.empty() && native->EntryPoint != m.Name)
						text += L" as \"" + native->EntryPoint + L"\"";
					if (native->Flags & 1)
						text += L" nomangle";
					static const PCWSTR charsets[] = { L"", L" ansi", L" unicode", L" autochar" };
					text += charsets[(native->Flags >> 1) & 3];
					if (native->Flags & 0x40)
						text += L" lasterr";
					static const PCWSTR conventions[] = { L"", L" winapi", L" cdecl", L" stdcall", L" thiscall", L" fastcall", L"", L"" };
					text += conventions[(native->Flags >> 8) & 7];
					text += L")";
				}
		}
	text += L" " + MethodSignature(method);
	static const PCWSTR code[] = { L" cil", L" native", L" optil", L" runtime" };
	text += code[m.ImplFlags & 3];
	text += (m.ImplFlags & 4) ? L" unmanaged" : L" managed";
	struct { uint16_t Flag; PCWSTR Text; } const impl[] = {
		{ 0x80, L"preservesig" }, { 0x1000, L"internalcall" }, { 0x20, L"synchronized" }, { 0x8, L"noinlining" },
		{ 0x100, L"aggressiveinlining" }, { 0x40, L"nooptimization" }, { 0x200, L"aggressiveoptimization" },
	};
	for (auto& f : impl)
		if (m.ImplFlags & f.Flag)
			text += std::format(L" {}", f.Text);
	return text;
}

std::wstring ClrMetadata::FieldDeclaration(uint32_t field) const {
	if (field == 0 || field > m_Fields.size())
		return {};
	auto& f = m_Fields[field - 1];
	std::wstring text = L".field ";
	text += MemberAccess(f.Flags);
	struct { uint16_t Flag; PCWSTR Text; } const flags[] = {
		{ 0x10, L"static" }, { 0x20, L"initonly" }, { 0x40, L"literal" }, { 0x80, L"notserialized" }, { 0x200, L"specialname" },
		{ 0x400, L"rtspecialname" }, { 0x2000, L"pinvokeimpl" },
	};
	for (auto& flag : flags)
		if (f.Flags & flag.Flag)
			text += std::format(L" {}", flag.Text);
	return text + L" " + FieldSignature(field);
}

std::wstring ClrMetadata::FieldSignature(uint32_t field) const {
	if (field == 0 || field > m_Fields.size())
		return {};
	auto& f = m_Fields[field - 1];
	ClrSignature sig{ *this, Blob(f.Signature), GenericParams((ClrTableId::TypeDef << 24) | f.Type) };
	sig.Byte();	// FIELD
	auto text = sig.Type() + L" " + f.Name;
	if (auto value = ConstantText((ClrTableId::Field << 24) | field); !value.empty())
		text += L" = " + value;
	return text;
}

std::vector<std::wstring> ClrMetadata::GenericParamNames(uint32_t owner) const {
	auto params = GenericParams(owner);
	return params ? *params : std::vector<std::wstring>();
}

std::wstring ClrMetadata::ConstantText(uint32_t token) const {
	auto it = m_Constants.find(token);
	if (it == m_Constants.end())
		return {};
	ClrRowReader c(*this, 0x0B, it->second);
	auto type = c.U16() & 0xFF;
	c.Coded(HasConstant);
	auto blob = Blob(c.Blob());
	if (!c.R.Ok)
		return {};
	uint64_t v = 0;
	for (size_t i = 0; i < blob.size() && i < 8; i++)
		v |= (uint64_t)std::to_integer<uint8_t>(blob[i]) << (8 * i);
	auto fits = [&](size_t size) { return blob.size() >= size; };
	switch (type) {
		case 0x02: return fits(1) ? (v & 0xFF ? L"bool(true)" : L"bool(false)") : L"";
		case 0x03: return fits(2) ? std::format(L"char(0x{:04X})", v & 0xFFFF) : L"";
		case 0x04: return fits(1) ? std::format(L"int8({})", (int8_t)v) : L"";
		case 0x05: return fits(1) ? std::format(L"uint8({})", (uint8_t)v) : L"";
		case 0x06: return fits(2) ? std::format(L"int16({})", (int16_t)v) : L"";
		case 0x07: return fits(2) ? std::format(L"uint16({})", (uint16_t)v) : L"";
		case 0x08: return fits(4) ? std::format(L"int32({})", (int32_t)v) : L"";
		case 0x09: return fits(4) ? std::format(L"uint32({})", (uint32_t)v) : L"";
		case 0x0A: return fits(8) ? std::format(L"int64({})", (int64_t)v) : L"";
		case 0x0B: return fits(8) ? std::format(L"uint64({})", v) : L"";
		case 0x0C:
		{
			if (!fits(4))
				return {};
			float f;
			uint32_t bits = (uint32_t)v;
			memcpy(&f, &bits, 4);
			return std::format(L"float32({})", f);
		}
		case 0x0D:
		{
			if (!fits(8))
				return {};
			double d;
			memcpy(&d, &v, 8);
			return std::format(L"float64({})", d);
		}
		case 0x0E:
		{
			std::wstring text(blob.size() / 2, L'\0');
			memcpy(text.data(), blob.data(), text.size() * 2);
			std::wstring quoted = L"\"";
			for (auto ch : text) {
				if (ch == L'"' || ch == L'\\')
					quoted += L'\\';
				quoted += ch < 0x20 ? L'?' : ch;
			}
			return quoted + L"\"";
		}
		case 0x12: return L"nullref";
	}
	return {};
}

std::vector<std::wstring> ClrMetadata::LocalTypes(uint32_t signatureToken, uint32_t method) const {
	std::vector<std::wstring> types;
	if (TokenTable(signatureToken) != ClrTableId::StandAloneSig)
		return types;
	ClrRowReader r(*this, ClrTableId::StandAloneSig, TokenRow(signatureToken));
	auto blob = Blob(r.Blob());
	if (!r.R.Ok || blob.empty())
		return types;
	std::vector<std::wstring> const* typeParams = nullptr, * methodParams = nullptr;
	if (method && method <= m_Methods.size()) {
		typeParams = GenericParams((ClrTableId::TypeDef << 24) | m_Methods[method - 1].Type);
		methodParams = GenericParams((ClrTableId::MethodDef << 24) | method);
	}
	ClrSignature sig{ *this, blob, typeParams, methodParams };
	if (sig.Byte() != 0x07)	// LOCAL_SIG
		return types;
	auto count = sig.Compressed();
	for (uint32_t i = 0; i < count && sig.Ok && i < 0x10000; i++)
		types.push_back(sig.Type());
	return types;
}

uint32_t ClrMetadata::Definition(uint32_t token) const {
	auto row = TokenRow(token);
	switch (TokenTable(token)) {
		case ClrTableId::TypeDef:
			return row && row <= m_Types.size() ? token : 0;
		case ClrTableId::MethodDef:
			return row && row <= m_Methods.size() ? token : 0;
		case ClrTableId::Field:
			return row && row <= m_Fields.size() ? token : 0;
		case ClrTableId::TypeSpec:
		{
			// an instance of a generic type of this file
			ClrRowReader r(*this, ClrTableId::TypeSpec, row);
			ClrSignature sig{ *this, Blob(r.Blob()) };
			if (sig.Byte() != 0x15)
				return 0;
			sig.Byte();	// class or valuetype
			auto type = sig.TypeToken();
			return sig.Ok && TokenTable(type) == ClrTableId::TypeDef ? Definition(type) : 0;
		}
		case ClrTableId::MethodSpec:
		{
			ClrRowReader r(*this, ClrTableId::MethodSpec, row);
			auto method = r.Coded(MethodDefOrRef);
			return r.R.Ok && TokenTable(method) != ClrTableId::MethodSpec ? Definition(method) : 0;
		}
		case ClrTableId::MemberRef:
		{
			// a member of a type of this file (of an instance of a generic one, for one): the member of the same name, and
			// of the same signature if there are several
			ClrRowReader r(*this, ClrTableId::MemberRef, row);
			auto parent = r.Coded(MemberRefParent);
			auto name = String(r.Str());
			auto blob = Blob(r.Blob());
			if (!r.R.Ok || (TokenTable(parent) != ClrTableId::TypeDef && TokenTable(parent) != ClrTableId::TypeSpec))
				return 0;
			auto type = Definition(parent);
			if (TokenTable(type) != ClrTableId::TypeDef)
				return 0;
			auto same = [&](uint32_t signature) {
				auto other = Blob(signature);
				return other.size() == blob.size() && std::equal(other.begin(), other.end(), blob.begin());
			};
			if (!blob.empty() && std::to_integer<uint8_t>(blob[0]) == 0x06) {
				for (auto f : FieldsOf(TokenRow(type)))
					if (m_Fields[f - 1].Name == name)
						return (ClrTableId::Field << 24) | f;
				return 0;
			}
			uint32_t first = 0;
			for (auto m : MethodsOf(TokenRow(type))) {
				if (m_Methods[m - 1].Name != name)
					continue;
				if (same(m_Methods[m - 1].Signature))
					return (ClrTableId::MethodDef << 24) | m;
				if (!first)
					first = m;
			}
			return first ? (ClrTableId::MethodDef << 24) | first : 0;
		}
	}
	return 0;
}

std::wstring CSharpQuote(std::wstring_view text) {
	std::wstring s = L"\"";
	for (auto c : text) {
		switch (c) {
			case L'"': s += L"\\\""; break;
			case L'\\': s += L"\\\\"; break;
			case L'\n': s += L"\\n"; break;
			case L'\r': s += L"\\r"; break;
			case L'\t': s += L"\\t"; break;
			case L'\0': s += L"\\0"; break;
			default:
				if (c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0xD800 && c < 0xE000))
					s += std::format(L"\\u{:04X}", (unsigned)c);
				else
					s += c;
		}
	}
	return s + L"\"";
}

std::wstring CSharpChar(wchar_t c) {
	switch (c) {
		case L'\'': return L"'\\''";
		case L'\\': return L"'\\\\'";
		case L'\n': return L"'\\n'";
		case L'\r': return L"'\\r'";
		case L'\t': return L"'\\t'";
		case L'\0': return L"'\\0'";
	}
	if (c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0xD800 && c < 0xE000) || c >= 0xFFFE)
		return std::format(L"'\\u{:04X}'", (unsigned)c);
	return std::wstring(L"'") + c + L"'";
}

std::wstring CSharpReal(double value, bool single) {
	if (std::isnan(value))
		return single ? L"float.NaN" : L"double.NaN";
	if (std::isinf(value))
		return std::wstring(single ? L"float" : L"double") + (value > 0 ? L".PositiveInfinity" : L".NegativeInfinity");
	if (single)
		return std::format(L"{}f", (float)value);
	auto text = std::format(L"{}", value);
	if (text.find_first_of(L".eE") == std::wstring::npos)
		text += L".0";
	return text;
}

namespace {
	// C#'s keywords for the types of the core library (System.Int32 is "int")
	PCWSTR CSharpKeyword(std::wstring_view name, ClrValueKind& kind) {
		using K = ClrValueKind;
		static const struct { PCWSTR Name, Keyword; K Kind; } map[] = {
			{ L"Boolean", L"bool", K::Bool }, { L"Char", L"char", K::Char }, { L"SByte", L"sbyte", K::Integer },
			{ L"Byte", L"byte", K::Integer }, { L"Int16", L"short", K::Integer }, { L"UInt16", L"ushort", K::Integer },
			{ L"Int32", L"int", K::Integer }, { L"UInt32", L"uint", K::Integer }, { L"Int64", L"long", K::Integer },
			{ L"UInt64", L"ulong", K::Integer }, { L"Single", L"float", K::Float }, { L"Double", L"double", K::Float },
			{ L"String", L"string", K::String }, { L"Object", L"object", K::Object }, { L"Void", L"void", K::Void },
			{ L"Decimal", L"decimal", K::Struct }, { L"IntPtr", L"IntPtr", K::Integer }, { L"UIntPtr", L"UIntPtr", K::Integer },
		};
		for (auto& m : map)
			if (name == m.Name) {
				kind = m.Kind;
				return m.Keyword;
			}
		return nullptr;
	}

	std::wstring WithoutArity(std::wstring_view name) {
		auto tick = name.find(L'`');
		return std::wstring(tick == std::wstring_view::npos ? name : name.substr(0, tick));
	}
}

std::wstring ClrMetadata::CSharpTypeDefName(uint32_t type) const {
	if (type == 0 || type > m_Types.size())
		return std::format(L"TypeDef{}", type);
	std::vector<uint32_t> chain{ type };
	for (auto t = m_Types[type - 1].Enclosing; t && chain.size() < 32; t = m_Types[t - 1].Enclosing)
		chain.push_back(t);
	if (chain.size() == 1 && m_Types[type - 1].Namespace == L"System") {
		ClrValueKind kind;
		if (auto keyword = CSharpKeyword(m_Types[type - 1].Name, kind))
			return keyword;
	}
	std::wstring name;
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
		name += (name.empty() ? L"" : L".") + WithoutArity(m_Types[*it - 1].Name);
	return name;
}

ClrTypeInfo ClrMetadata::CSharpType(uint32_t token, ClrGenericContext const& context) const {
	using K = ClrValueKind;
	auto row = TokenRow(token);
	switch (TokenTable(token)) {
		case ClrTableId::TypeDef:
		{
			if (row == 0 || row > m_Types.size())
				break;
			ClrTypeInfo info{ CSharpTypeDefName(row), K::Object, token };
			auto& t = m_Types[row - 1];
			if (t.Enclosing == 0 && t.Namespace == L"System" && CSharpKeyword(t.Name, info.Kind))
				return info;
			auto kind = TypeKind(row);
			info.Kind = kind == L"enum" ? K::Enum : kind == L"struct" ? K::Struct : K::Object;
			return info;
		}
		case ClrTableId::TypeRef:
		{
			ClrRowReader r(*this, ClrTableId::TypeRef, row);
			auto scope = r.Coded(ResolutionScope);
			auto name = String(r.Str());
			auto ns = String(r.Str());
			if (!r.R.Ok)
				break;
			ClrTypeInfo info{ {}, K::Unknown, token };
			if (TokenTable(scope) == ClrTableId::TypeRef) {
				static thread_local int depth = 0;
				if (depth < 16) {
					depth++;
					info.Name = CSharpType(scope).Name + L".";
					depth--;
				}
			}
			else if (ns == L"System")
				if (auto keyword = CSharpKeyword(name, info.Kind)) {
					info.Name = keyword;
					return info;
				}
			info.Name += WithoutArity(name);
			return info;
		}
		case ClrTableId::TypeSpec:
		{
			static thread_local int depth = 0;
			if (depth > 8)
				return { L"?" };
			ClrRowReader r(*this, ClrTableId::TypeSpec, row);
			ClrSignature sig{ *this, Blob(r.Blob()) };
			sig.Context = context;
			depth++;
			auto info = sig.CsType();
			depth--;
			if (info.Token == 0)
				info.Token = token;
			return info;
		}
	}
	return { std::format(L"Type0x{:08X}", token) };
}

ClrGenericContext ClrMetadata::ContextOf(uint32_t method) const {
	ClrGenericContext context;
	if (method == 0 || method > m_Methods.size())
		return context;
	context.TypeNames = GenericParams((ClrTableId::TypeDef << 24) | m_Methods[method - 1].Type);
	context.MethodNames = GenericParams((ClrTableId::MethodDef << 24) | method);
	return context;
}

ClrGenericContext ClrMetadata::ContextOfType(uint32_t type) const {
	ClrGenericContext context;
	context.TypeNames = GenericParams((ClrTableId::TypeDef << 24) | type);
	return context;
}

ClrMemberInfo ClrMetadata::MemberInfo(uint32_t token, ClrGenericContext const& context) const {
	return MemberInfoOf(token, context, nullptr);
}

ClrMemberInfo ClrMetadata::MemberInfoOf(uint32_t token, ClrGenericContext const& context, std::vector<ClrTypeInfo> const* methodArgs) const {
	ClrMemberInfo info;
	info.Token = token;
	auto row = TokenRow(token);
	// what !0 and !!0 stand for: the arguments of the instance of a generic type or method
	std::vector<std::wstring> typeNames, methodNames;
	std::vector<ClrValueKind> typeKinds, methodKinds;
	if (methodArgs)
		for (auto& arg : *methodArgs) {
			methodNames.push_back(arg.Name);
			methodKinds.push_back(arg.Kind);
		}
	auto decode = [&](std::span<const std::byte> blob, ClrGenericContext sigContext) {
		if (methodArgs) {
			sigContext.MethodNames = &methodNames;
			sigContext.MethodKinds = &methodKinds;
		}
		ClrSignature sig{ *this, blob };
		sig.Context = sigContext;
		if (!blob.empty() && std::to_integer<uint8_t>(blob[0]) == 0x06) {
			sig.Byte();
			info.IsField = true;
			info.FieldType = sig.CsType();
		}
		else
			info.Signature = sig.CsMethod();
		info.Ok = sig.Ok && !blob.empty();
	};
	switch (TokenTable(token)) {
		case ClrTableId::MethodDef:
		{
			if (row == 0 || row > m_Methods.size())
				break;
			auto& m = m_Methods[row - 1];
			info.Name = m.Name;
			info.Definition = token;
			info.Owner = CSharpType((ClrTableId::TypeDef << 24) | m.Type);
			decode(Blob(m.Signature), ContextOf(row));
			break;
		}
		case ClrTableId::Field:
		{
			if (row == 0 || row > m_Fields.size())
				break;
			auto& f = m_Fields[row - 1];
			info.Name = f.Name;
			info.Definition = token;
			info.Owner = CSharpType((ClrTableId::TypeDef << 24) | f.Type);
			decode(Blob(f.Signature), ContextOfType(f.Type));
			break;
		}
		case ClrTableId::MemberRef:
		{
			ClrRowReader r(*this, ClrTableId::MemberRef, row);
			auto parent = r.Coded(MemberRefParent);
			info.Name = String(r.Str());
			auto blob = Blob(r.Blob());
			if (!r.R.Ok)
				break;
			ClrGenericContext sigContext;
			switch (TokenTable(parent)) {
				case ClrTableId::TypeSpec:
				{
					info.Owner = CSharpType(parent, context);
					ClrRowReader ts(*this, ClrTableId::TypeSpec, TokenRow(parent));
					ClrSignature s{ *this, Blob(ts.Blob()) };
					s.Context = context;
					if (s.Byte() == 0x15) {
						s.Byte();
						s.TypeToken();
						auto count = s.Compressed();
						for (uint32_t i = 0; i < count && s.Ok && i < 64; i++) {
							auto arg = s.CsType();
							typeNames.push_back(arg.Name);
							typeKinds.push_back(arg.Kind);
						}
						sigContext.TypeNames = &typeNames;
						sigContext.TypeKinds = &typeKinds;
					}
					break;
				}
				case ClrTableId::TypeDef:
				case ClrTableId::TypeRef:
					info.Owner = CSharpType(parent, context);
					break;
				case ClrTableId::MethodDef:	// the signature of a call of a vararg method
					if (TokenRow(parent) && TokenRow(parent) <= m_Methods.size())
						info.Owner = CSharpType((ClrTableId::TypeDef << 24) | m_Methods[TokenRow(parent) - 1].Type);
					break;
			}
			decode(blob, sigContext);
			info.Definition = Definition(token);
			break;
		}
		case ClrTableId::MethodSpec:
		{
			ClrRowReader r(*this, ClrTableId::MethodSpec, row);
			auto method = r.Coded(MethodDefOrRef);
			auto inst = Blob(r.Blob());
			if (!r.R.Ok || TokenTable(method) == ClrTableId::MethodSpec)
				break;
			ClrSignature args{ *this, inst };
			args.Context = context;
			std::vector<ClrTypeInfo> list;
			if (args.Byte() == 0x0A) {
				auto count = args.Compressed();
				for (uint32_t i = 0; i < count && args.Ok && i < 64; i++)
					list.push_back(args.CsType());
			}
			info = MemberInfoOf(method, context, &list);
			info.Token = token;
			for (auto& arg : list)
				info.GenericArgs.push_back(arg.Name);
			info.Definition = Definition(token);
			break;
		}
	}
	return info;
}

std::vector<ClrTypeInfo> ClrMetadata::CSharpLocalTypes(uint32_t signatureToken, uint32_t method) const {
	std::vector<ClrTypeInfo> types;
	if (TokenTable(signatureToken) != ClrTableId::StandAloneSig)
		return types;
	ClrRowReader r(*this, ClrTableId::StandAloneSig, TokenRow(signatureToken));
	auto blob = Blob(r.Blob());
	if (!r.R.Ok || blob.empty())
		return types;
	ClrSignature sig{ *this, blob };
	sig.Context = ContextOf(method);
	if (sig.Byte() != 0x07)	// LOCAL_SIG
		return types;
	auto count = sig.Compressed();
	for (uint32_t i = 0; i < count && sig.Ok && i < 0x10000; i++)
		types.push_back(sig.CsType());
	return types;
}

ClrTypeInfo ClrMetadata::CSharpPropertyType(uint32_t property, std::vector<ClrTypeInfo>* parameters) const {
	if (property == 0 || property > m_Properties.size())
		return {};
	auto& p = m_Properties[property - 1];
	ClrSignature sig{ *this, Blob(p.Signature) };
	sig.Context = ContextOfType(p.Type);
	sig.Byte();
	auto count = sig.Compressed();
	auto type = sig.CsType();
	for (uint32_t i = 0; i < count && sig.Ok && i < 64; i++) {
		auto param = sig.CsType();
		if (parameters)
			parameters->push_back(param);
	}
	return type;
}

std::wstring ClrMetadata::ConstantCSharp(uint32_t token) const {
	auto it = m_Constants.find(token);
	if (it == m_Constants.end())
		return {};
	ClrRowReader c(*this, 0x0B, it->second);
	auto type = c.U16() & 0xFF;
	c.Coded(HasConstant);
	auto blob = Blob(c.Blob());
	if (!c.R.Ok)
		return {};
	uint64_t v = 0;
	for (size_t i = 0; i < blob.size() && i < 8; i++)
		v |= (uint64_t)std::to_integer<uint8_t>(blob[i]) << (8 * i);
	auto fits = [&](size_t size) { return blob.size() >= size; };
	switch (type) {
		case 0x02: return fits(1) ? (v & 0xFF ? L"true" : L"false") : L"";
		case 0x03: return fits(2) ? CSharpChar((wchar_t)v) : L"";
		case 0x04: return fits(1) ? std::to_wstring((int8_t)v) : L"";
		case 0x05: return fits(1) ? std::to_wstring((uint8_t)v) : L"";
		case 0x06: return fits(2) ? std::to_wstring((int16_t)v) : L"";
		case 0x07: return fits(2) ? std::to_wstring((uint16_t)v) : L"";
		case 0x08: return fits(4) ? std::to_wstring((int32_t)v) : L"";
		case 0x09: return fits(4) ? std::format(L"{}u", (uint32_t)v) : L"";
		case 0x0A: return fits(8) ? std::format(L"{}L", (int64_t)v) : L"";
		case 0x0B: return fits(8) ? std::format(L"{}UL", v) : L"";
		case 0x0C:
		{
			if (!fits(4))
				return {};
			float f;
			uint32_t bits = (uint32_t)v;
			memcpy(&f, &bits, 4);
			return CSharpReal(f, true);
		}
		case 0x0D:
		{
			if (!fits(8))
				return {};
			double d;
			memcpy(&d, &v, 8);
			return CSharpReal(d, false);
		}
		case 0x0E:
		{
			std::wstring text(blob.size() / 2, L'\0');
			memcpy(text.data(), blob.data(), text.size() * 2);
			return CSharpQuote(text);
		}
		case 0x12: return L"null";
	}
	return {};
}

std::optional<int64_t> ClrMetadata::ConstantInteger(uint32_t token) const {
	auto it = m_Constants.find(token);
	if (it == m_Constants.end())
		return {};
	ClrRowReader c(*this, 0x0B, it->second);
	auto type = c.U16() & 0xFF;
	c.Coded(HasConstant);
	auto blob = Blob(c.Blob());
	if (!c.R.Ok)
		return {};
	uint64_t v = 0;
	for (size_t i = 0; i < blob.size() && i < 8; i++)
		v |= (uint64_t)std::to_integer<uint8_t>(blob[i]) << (8 * i);
	switch (type) {
		case 0x02: case 0x05: return (int64_t)(uint8_t)v;
		case 0x03: case 0x07: return (int64_t)(uint16_t)v;
		case 0x04: return (int64_t)(int8_t)v;
		case 0x06: return (int64_t)(int16_t)v;
		case 0x08: return (int64_t)(int32_t)v;
		case 0x09: return (int64_t)(uint32_t)v;
		case 0x0A: case 0x0B: return (int64_t)v;
	}
	return {};
}

ClrTypeInfo ClrMetadata::EnumUnderlyingType(uint32_t type) const {
	for (auto f : FieldsOf(type))
		if ((m_Fields[f - 1].Flags & 0x10) == 0) {	// the one instance field
			auto info = MemberInfo((ClrTableId::Field << 24) | f);
			if (info.Ok)
				return info.FieldType;
		}
	return { L"int", ClrValueKind::Integer };
}

std::vector<uint32_t> ClrMetadata::PropertiesOf(uint32_t type) const {
	return type < m_TypeProperties.size() ? m_TypeProperties[type] : std::vector<uint32_t>();
}

std::vector<uint32_t> ClrMetadata::EventsOf(uint32_t type) const {
	return type < m_TypeEvents.size() ? m_TypeEvents[type] : std::vector<uint32_t>();
}

std::pair<uint32_t, ClrAccessor> ClrMetadata::AccessorOf(uint32_t method) const {
	auto it = m_Accessors.find(method);
	return it == m_Accessors.end() ? std::pair<uint32_t, ClrAccessor>{ 0, ClrAccessor::None } : it->second;
}

std::wstring ClrMetadata::PropertyDeclaration(uint32_t property) const {
	if (property == 0 || property > m_Properties.size())
		return {};
	auto& p = m_Properties[property - 1];
	std::wstring text = L".property ";
	if (p.Flags & 0x200)
		text += L"specialname ";
	if (p.Flags & 0x400)
		text += L"rtspecialname ";
	ClrSignature sig{ *this, Blob(p.Signature), GenericParams((ClrTableId::TypeDef << 24) | p.Type) };
	return text + sig.Method(p.Name, nullptr);
}

std::wstring ClrMetadata::EventDeclaration(uint32_t event) const {
	if (event == 0 || event > m_Events.size())
		return {};
	auto& e = m_Events[event - 1];
	std::wstring text = L".event ";
	if (e.Flags & 0x200)
		text += L"specialname ";
	if (e.Flags & 0x400)
		text += L"rtspecialname ";
	return text + EventSignature(event);
}

std::wstring ClrMetadata::PropertySignature(uint32_t property) const {
	if (property == 0 || property > m_Properties.size())
		return {};
	auto& p = m_Properties[property - 1];
	ClrSignature sig{ *this, Blob(p.Signature), GenericParams((ClrTableId::TypeDef << 24) | p.Type) };
	sig.Byte();
	auto count = sig.Compressed();
	auto text = sig.Type() + L" " + p.Name;
	if (count) {
		text += L"(";
		for (uint32_t i = 0; i < count && sig.Ok && i < 64; i++)
			text += (i ? L", " : L"") + sig.Type();
		text += L")";
	}
	return text;
}

std::wstring ClrMetadata::EventSignature(uint32_t event) const {
	if (event == 0 || event > m_Events.size())
		return {};
	auto& e = m_Events[event - 1];
	return (e.EventType ? TypeName(e.EventType) + L" " : L"") + e.Name;
}

uint32_t ClrMetadata::ParamToken(uint32_t method, uint32_t sequence) const {
	if (method == 0 || method > m_Methods.size())
		return 0;
	auto& m = m_Methods[method - 1];
	for (auto i = m.FirstParam; i < m.EndParam; i++) {
		auto row = Indirect(ClrTableId::ParamPtr, i);
		ClrRowReader p(*this, ClrTableId::Param, row);
		p.U16();
		if (p.U16() == sequence && p.R.Ok)
			return (ClrTableId::Param << 24) | row;
	}
	return 0;
}

uint16_t ClrMetadata::ParamFlags(uint32_t method, uint32_t sequence) const {
	auto token = ParamToken(method, sequence);
	if (!token)
		return 0;
	ClrRowReader p(*this, ClrTableId::Param, TokenRow(token));
	return (uint16_t)p.U16();
}

// Decodes the value of a custom attribute (ECMA-335 II.23.3): the arguments of the constructor, by the types of its
// parameters, then the named ones (fields and properties), each with its type
struct ClrAttributeReader {
	ClrMetadata const& Md;
	Reader R;

	struct ValueType {
		uint8_t Element{};		// ELEMENT_TYPE (0x02 to 0x0E), 0x50 System.Type, 0x51 boxed (object), 0x55 an enum
		uint8_t Underlying{ 0x08 };	// of an enum: int32 if it is not known
		std::wstring EnumIl, EnumCs;
		std::shared_ptr<ClrEnumInfo const> Enum;	// its values, if the enum is found
		bool Array{ false };
	};

	static PCWSTR ElementIl(uint8_t element) {
		static const PCWSTR names[] = { L"bool", L"char", L"int8", L"uint8", L"int16", L"uint16", L"int32", L"uint32",
			L"int64", L"uint64", L"float32", L"float64", L"string" };
		return element >= 0x02 && element <= 0x0E ? names[element - 2] : element == 0x50 ? L"type" : L"object";
	}
	static PCWSTR ElementCs(uint8_t element) {
		static const PCWSTR names[] = { L"bool", L"char", L"sbyte", L"byte", L"short", L"ushort", L"int", L"uint",
			L"long", L"ulong", L"float", L"double", L"string" };
		return element >= 0x02 && element <= 0x0E ? names[element - 2] : element == 0x50 ? L"Type" : L"object";
	}
	std::wstring TypeIl(ValueType const& type) const {
		auto text = type.Element == 0x55 ? L"enum " + type.EnumIl : std::wstring(ElementIl(type.Element));
		return type.Array ? text + L"[]" : text;
	}
	std::wstring TypeCs(ValueType const& type) const {
		return type.Element == 0x55 ? type.EnumCs : ElementCs(type.Element);
	}

	// an enum of the file or of an assembly it refers to (taken to be of int32 if it is not found)
	void SetEnum(ValueType& type, uint32_t token) {
		type.Element = 0x55;
		type.EnumIl = Md.TypeName(token);
		type.EnumCs = Md.CSharpType(token).Name;
		SetEnum(type, Md.EnumInfo(token));
	}
	// an enum named in the value ("Ns.Outer+Inner, Assembly")
	void SetEnum(ValueType& type, std::wstring const& name) {
		type.Element = 0x55;
		auto full = name.substr(0, name.find(L','));
		auto il = full;
		std::replace(il.begin(), il.end(), L'+', L'/');
		type.EnumIl = il;
		auto last = full.find_last_of(L".+");
		type.EnumCs = last == std::wstring::npos ? full : full.substr(last + 1);
		SetEnum(type, Md.EnumInfoByName(name));
	}
	void SetEnum(ValueType& type, std::shared_ptr<ClrEnumInfo const> info) {
		if (!info)
			return;
		type.Enum = info;
		type.EnumCs = info->Name;
		type.Underlying = info->Underlying;
	}

	// the type of a parameter of the constructor
	bool ParamType(ClrSignature& sig, ValueType& type) {
		auto et = sig.Byte();
		if (et == 0x1D) {
			type.Array = true;
			et = sig.Byte();
		}
		if (et >= 0x02 && et <= 0x0E) {
			type.Element = et;
			return true;
		}
		if (et == 0x1C) {
			type.Element = 0x51;
			return true;
		}
		if (et == 0x11 || et == 0x12) {
			auto token = sig.TypeToken();
			if (et == 0x12) {
				auto name = Md.TypeName(token);
				auto is = [&](std::wstring_view full) {
					return name == full || (name.ends_with(full) && name.size() > full.size() && name[name.size() - full.size() - 1] == L']');
				};
				if (is(L"System.Type"))
					type.Element = 0x50;
				else if (is(L"System.Object"))
					type.Element = 0x51;
				else
					return false;
				return true;
			}
			SetEnum(type, token);
			return true;
		}
		return false;
	}

	// the type of a named argument, or of a boxed value
	bool FieldOrPropType(ValueType& type) {
		auto b = (uint8_t)R.Read(1);
		if (b == 0x1D) {
			type.Array = true;
			b = (uint8_t)R.Read(1);
		}
		if ((b >= 0x02 && b <= 0x0E) || b == 0x50 || b == 0x51)
			type.Element = b;
		else if (b == 0x55) {
			bool null = false;
			auto name = SerString(null);
			if (!R.Ok || null)
				return false;
			SetEnum(type, name);
		}
		else
			return false;
		return R.Ok;
	}

	uint32_t Compressed() {
		auto b = R.Read(1);
		if ((b & 0x80) == 0)
			return b;
		if ((b & 0xC0) == 0x80)
			return ((b & 0x3F) << 8) | R.Read(1);
		uint32_t v = (b & 0x1F) << 24;
		v |= R.Read(1) << 16;
		v |= R.Read(1) << 8;
		return v | R.Read(1);
	}
	std::wstring SerString(bool& null) {
		null = false;
		if (R.Pos < R.Data.size() && std::to_integer<uint8_t>(R.Data[R.Pos]) == 0xFF) {
			R.Pos++;
			null = true;
			return {};
		}
		auto size = Compressed();
		if (!R.Ok || size > R.Data.size() - R.Pos) {
			R.Ok = false;
			return {};
		}
		auto text = Utf8ToWide(std::string_view(reinterpret_cast<const char*>(R.Data.data()) + R.Pos, size));
		R.Pos += size;
		return text;
	}

	// an integer of an element type: its value as a signed or unsigned number
	bool Integer(uint8_t element, int64_t& value) {
		static const uint8_t sizes[] = { 1, 2, 1, 1, 2, 2, 4, 4, 8, 8 };
		auto size = sizes[element - 2];
		uint64_t v = size == 8 ? (R.Read(4) | ((uint64_t)R.Read(4) << 32)) : R.Read(size);
		switch (element) {
			case 0x04: value = (int8_t)v; break;
			case 0x06: value = (int16_t)v; break;
			case 0x08: value = (int32_t)v; break;
			default: value = (int64_t)v; break;
		}
		return R.Ok;
	}
	static std::wstring IntegerCs(uint8_t element, int64_t value) {
		switch (element) {
			case 0x09: return std::format(L"{}u", (uint32_t)value);
			case 0x0A: return std::format(L"{}L", value);
			case 0x0B: return std::format(L"{}UL", (uint64_t)value);
		}
		return std::to_wstring(value);
	}

	static std::wstring IlSingleQuote(std::wstring const& text) {
		std::wstring s = L"'";
		for (auto c : text) {
			if (c == L'\'' || c == L'\\')
				s += L'\\';
			s += c;
		}
		return s + L"'";
	}

	// a type named in a value ("Ns.Outer+Inner, Assembly, Version=...": generic arguments in brackets have commas of their own)
	static std::wstring ReflectionTypeName(std::wstring const& name) {
		int brackets = 0;
		for (size_t i = 0; i < name.size(); i++) {
			if (name[i] == L'[')
				brackets++;
			else if (name[i] == L']')
				brackets--;
			else if (name[i] == L',' && brackets == 0)
				return name.substr(0, i);
		}
		return name;
	}

	bool Value(ValueType const& type, ClrAttributeArgument& arg, int depth = 0) {
		if (depth > 8)
			return false;
		if (type.Array) {
			auto count = R.Read(4);
			if (!R.Ok)
				return false;
			ValueType element = type;
			element.Array = false;
			if (count == 0xFFFFFFFF) {
				arg.Il = TypeIl(element) + L"[](nullref)";
				arg.CSharp = L"null";
				return true;
			}
			if (count > 0x10000)
				return false;
			std::wstring il, cs;
			for (uint32_t i = 0; i < count; i++) {
				ClrAttributeArgument e;
				if (!Value(element, e, depth + 1))
					return false;
				// "int32(5)" is "5" in the list of an array
				auto open = e.Il.find(L'(');
				il += (i ? L" " : L"") + (open != std::wstring::npos && e.Il.back() == L')' ? e.Il.substr(open + 1, e.Il.size() - open - 2) : e.Il);
				cs += (i ? L", " : L"") + e.CSharp;
			}
			arg.Il = std::format(L"{}[{}]({})", TypeIl(element), count, il);
			arg.CSharp = L"new " + TypeCs(element) + L"[] { " + cs + (count ? L" }" : L"}");
			return true;
		}
		auto element = type.Element;
		switch (element) {
			case 0x02:
			{
				auto v = R.Read(1);
				arg.Il = v ? L"bool(true)" : L"bool(false)";
				arg.CSharp = v ? L"true" : L"false";
				return R.Ok;
			}
			case 0x03:
			{
				auto v = (wchar_t)R.Read(2);
				arg.Il = std::format(L"char(0x{:04X})", (unsigned)v);
				arg.CSharp = CSharpChar(v);
				return R.Ok;
			}
			case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B:
			{
				int64_t v;
				if (!Integer(element, v))
					return false;
				arg.Il = std::format(L"{}({})", ElementIl(element), element == 0x0B ? std::to_wstring((uint64_t)v) : std::to_wstring(v));
				arg.CSharp = IntegerCs(element, v);
				return true;
			}
			case 0x0C:
			{
				auto bits = R.Read(4);
				float f;
				memcpy(&f, &bits, 4);
				arg.Il = std::format(L"float32({})", f);
				arg.CSharp = CSharpReal(f, true);
				return R.Ok;
			}
			case 0x0D:
			{
				uint64_t bits = R.Read(4);
				bits |= (uint64_t)R.Read(4) << 32;
				double d;
				memcpy(&d, &bits, 8);
				arg.Il = std::format(L"float64({})", d);
				arg.CSharp = CSharpReal(d, false);
				return R.Ok;
			}
			case 0x0E:
			{
				bool null;
				auto text = SerString(null);
				arg.Il = null ? L"string(nullref)" : L"string(" + IlSingleQuote(text) + L")";
				arg.CSharp = null ? L"null" : CSharpQuote(text);
				return R.Ok;
			}
			case 0x50:
			{
				bool null;
				auto name = SerString(null);
				if (null) {
					arg.Il = L"type(nullref)";
					arg.CSharp = L"null";
					return R.Ok;
				}
				auto full = ReflectionTypeName(name);
				auto il = full;
				std::replace(il.begin(), il.end(), L'+', L'/');
				arg.Il = L"type(" + il + L")";
				// C#: the name without its namespace and arity (a keyword for those that have one)
				auto cs = full.substr(0, full.find_first_of(L"`["));
				ClrValueKind kind;
				if (cs.starts_with(L"System.") && cs.find_first_of(L".+", 7) == std::wstring::npos)
					if (auto keyword = CSharpKeyword(std::wstring_view(cs).substr(7), kind))
						cs = keyword;
				if (auto last = cs.find_last_of(L'.'); last != std::wstring::npos)
					cs = cs.substr(last + 1);
				std::replace(cs.begin(), cs.end(), L'+', L'.');
				arg.CSharp = L"typeof(" + cs + L")";
				return R.Ok;
			}
			case 0x51:
			{
				ValueType boxed;
				if (!FieldOrPropType(boxed))
					return false;
				ClrAttributeArgument inner;
				if (!Value(boxed, inner, depth + 1))
					return false;
				arg.Il = L"object(" + inner.Il + L")";
				arg.CSharp = inner.CSharp;
				return true;
			}
			case 0x55:
			{
				int64_t v;
				auto underlying = type.Underlying >= 0x02 && type.Underlying <= 0x0B ? type.Underlying : (uint8_t)0x08;
				if (!Integer(underlying, v))
					return false;
				arg.Il = std::format(L"{}({})", ElementIl(underlying), v);
				arg.CSharp = std::format(L"({}){}", type.EnumCs, v);
				// the names of the value ("AttributeTargets.Class | AttributeTargets.Method")
				if (type.Enum)
					if (auto parts = type.Enum->Parts(v); !parts.empty()) {
						arg.CSharp.clear();
						for (auto i : parts)
							arg.CSharp += (arg.CSharp.empty() ? L"" : L" | ") + type.EnumCs + L"." + type.Enum->Values[i].Name;
					}
				return true;
			}
		}
		return false;
	}

	bool Decode(ClrCustomAttribute& attribute) {
		std::span<const std::byte> signature;
		auto ctor = attribute.Constructor;
		if (TokenTable(ctor) == ClrTableId::MethodDef) {
			if (TokenRow(ctor) == 0 || TokenRow(ctor) > Md.m_Methods.size())
				return false;
			auto& m = Md.m_Methods[TokenRow(ctor) - 1];
			signature = Md.Blob(m.Signature);
			attribute.AttributeType = (ClrTableId::TypeDef << 24) | m.Type;
		}
		else if (TokenTable(ctor) == ClrTableId::MemberRef) {
			ClrRowReader r(Md, ClrTableId::MemberRef, TokenRow(ctor));
			attribute.AttributeType = r.Coded(MemberRefParent);
			r.Str();
			signature = Md.Blob(r.Blob());
			if (!r.R.Ok)
				return false;
		}
		else
			return false;
		if (R.Read(2) != 1)	// the prolog
			return false;
		ClrSignature sig{ Md, signature };
		sig.Byte();
		auto count = sig.Compressed();
		sig.Type();	// void
		for (uint32_t i = 0; i < count && i < 256; i++) {
			ValueType type;
			if (!ParamType(sig, type) || !sig.Ok)
				return false;
			ClrAttributeArgument arg;
			arg.Type = TypeIl(type);
			if (!Value(type, arg))
				return false;
			attribute.Arguments.push_back(std::move(arg));
		}
		auto named = R.Read(2);
		for (uint32_t i = 0; i < named && R.Ok && i < 256; i++) {
			auto kind = R.Read(1);
			if (kind != 0x53 && kind != 0x54)
				return false;
			ValueType type;
			if (!FieldOrPropType(type))
				return false;
			ClrAttributeArgument arg;
			arg.Field = kind == 0x53;
			bool null;
			arg.Name = SerString(null);
			arg.Type = TypeIl(type);
			if (!R.Ok || !Value(type, arg))
				return false;
			attribute.Arguments.push_back(std::move(arg));
		}
		return R.Ok;
	}
};

std::vector<ClrCustomAttribute> ClrMetadata::CustomAttributes(uint32_t token) const {
	std::vector<ClrCustomAttribute> attributes;
	auto it = std::lower_bound(m_Attributes.begin(), m_Attributes.end(), token, [](auto& a, uint32_t t) { return a.first < t; });
	for (; it != m_Attributes.end() && it->first == token && attributes.size() < 256; ++it) {
		ClrRowReader r(*this, ClrTableId::CustomAttribute, it->second);
		ClrCustomAttribute attribute;
		attribute.Parent = r.Coded(HasCustomAttribute);
		attribute.Constructor = r.Coded(CustomAttributeType);
		auto blob = Blob(r.Blob());
		for (auto b : blob)
			attribute.Value.push_back(std::to_integer<uint8_t>(b));
		ClrAttributeReader reader{ *this, Reader{ blob } };
		attribute.Decoded = reader.Decode(attribute);
		if (!attribute.Decoded)
			attribute.Arguments.clear();
		attributes.push_back(std::move(attribute));
	}
	return attributes;
}

std::wstring ClrMetadata::AttributeIl(ClrCustomAttribute const& attribute) const {
	auto text = L".custom " + MemberName(attribute.Constructor);
	if (attribute.Decoded) {
		if (attribute.Arguments.empty())
			return text;
		text += L" = {";
		for (auto& arg : attribute.Arguments) {
			text += L" ";
			if (!arg.Name.empty())
				text += (arg.Field ? L"field " : L"property ") + arg.Type + L" '" + arg.Name + L"' = ";
			text += arg.Il;
		}
		return text + L" }";
	}
	text += L" = (";
	for (auto b : attribute.Value)
		text += std::format(L" {:02X}", b);
	return text + L" )";
}

std::wstring ClrMetadata::AttributeCSharp(ClrCustomAttribute const& attribute, ClrGenericContext const& context) const {
	auto name = CSharpType(attribute.AttributeType, context).Name;
	if (name.size() > 9 && name.ends_with(L"Attribute"))
		name.resize(name.size() - 9);
	if (!attribute.Decoded) {
		std::wstring bytes;
		for (auto b : attribute.Value)
			bytes += std::format(L" {:02X}", b);
		return name + L"(/*" + bytes + L" */)";
	}
	if (attribute.Arguments.empty())
		return name;
	name += L"(";
	for (size_t i = 0; i < attribute.Arguments.size(); i++) {
		auto& arg = attribute.Arguments[i];
		name += (i ? L", " : L"") + (arg.Name.empty() ? L"" : arg.Name + L" = ") + arg.CSharp;
	}
	return name + L")";
}

// what is found once: the types and assemblies of the references, the layouts, whether ImplMap is sorted
struct ClrMetadata::ResolvedReferences {
	std::mutex Lock;
	std::unordered_map<uint32_t, ClrMetadata const*> Assemblies;	// by the AssemblyRef row
	std::unordered_map<uint32_t, std::pair<ClrMetadata const*, uint32_t>> Types;	// by the TypeRef row
	std::unordered_map<std::wstring, std::shared_ptr<ClrEnumInfo const>> EnumsByName;
	std::unordered_map<uint32_t, std::shared_ptr<ClrEnumInfo const>> Enums;	// by the token
	std::unordered_map<std::wstring, uint32_t> TypeDefs;	// the types that are not nested, by "namespace|name"
	bool Indexed{ false };
	std::unordered_map<uint32_t, uint32_t> FieldOffsets;	// FieldLayout, by the field
	bool OffsetsRead{ false };
	int ImplMapSorted{ -1 };	// (not known yet)
};

std::optional<ClrPInvoke> ClrMetadata::PInvoke(uint32_t method) const {
	// ImplMap: Flags, MemberForwarded, ImportName, ImportScope; sorted by MemberForwarded (II.22): searched by halves, unless
	// the file does not keep it sorted
	auto token = (ClrTableId::MethodDef << 24) | method;
	auto rows = m_Rows[0x1C];
	auto member = [&](uint32_t row) {
		ClrRowReader r(*this, 0x1C, row);
		r.U16();
		auto forwarded = r.Coded(MemberForwarded);
		// (the order of the coded index: the row, then the table: Field 0, MethodDef 1)
		return std::pair<uint32_t, uint32_t>(TokenRow(forwarded), TokenTable(forwarded) == ClrTableId::MethodDef ? 1 : 0);
	};
	auto read = [&](uint32_t row) {
		ClrRowReader r(*this, 0x1C, row);
		ClrPInvoke info;
		info.Flags = (uint16_t)r.U16();
		r.Coded(MemberForwarded);
		info.EntryPoint = String(r.Str());
		ClrRowReader m(*this, ClrTableId::ModuleRef, r.Simple(ClrTableId::ModuleRef));
		info.Module = String(m.Str());
		return info;
	};
	bool sorted;
	{
		std::lock_guard lock(m_References->Lock);
		if (m_References->ImplMapSorted < 0) {
			bool ordered = true;
			for (uint32_t row = 2; row <= rows && ordered; row++)
				ordered = member(row - 1) <= member(row);
			m_References->ImplMapSorted = ordered;
		}
		sorted = m_References->ImplMapSorted > 0;
	}
	if (sorted) {
		std::pair<uint32_t, uint32_t> wanted{ method, 1 };
		uint32_t low = 1, high = rows;
		while (low <= high) {
			auto middle = low + (high - low) / 2;
			auto at = member(middle);
			if (at == wanted)
				return read(middle);
			if (at < wanted)
				low = middle + 1;
			else
				high = middle - 1;
		}
		return {};
	}
	for (uint32_t row = 1; row <= rows; row++) {
		ClrRowReader r(*this, 0x1C, row);
		r.U16();
		if (r.Coded(MemberForwarded) == token)
			return read(row);
	}
	return {};
}

//
// The assemblies the file refers to, and the enums of theirs that it uses
//

std::vector<size_t> ClrEnumInfo::Parts(int64_t value) const {
	// the values are compared in the size of the enum's (an int's -1 is a uint's 0xFFFFFFFF)
	int bits = Underlying == 0x04 || Underlying == 0x05 ? 8 : Underlying == 0x06 || Underlying == 0x07 || Underlying == 0x03 ? 16 :
		Underlying == 0x0A || Underlying == 0x0B ? 64 : 32;
	auto mask = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
	auto v = (uint64_t)value & mask;
	for (size_t i = 0; i < Values.size(); i++)
		if (((uint64_t)Values[i].Value & mask) == v)
			return { i };
	if (!Flags || v == 0)
		return {};
	// the largest values first, so that a value that names several bits is used rather than each of them
	std::vector<size_t> order(Values.size());
	std::iota(order.begin(), order.end(), size_t(0));
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return ((uint64_t)Values[a].Value & mask) > ((uint64_t)Values[b].Value & mask);
	});
	std::vector<size_t> parts;
	auto left = v;
	for (auto i : order) {
		auto part = (uint64_t)Values[i].Value & mask;
		if (part && (v & part) == part && (left & part)) {
			parts.push_back(i);
			left &= ~part;
		}
	}
	if (left)
		return {};
	std::sort(parts.begin(), parts.end());
	return parts;
}


namespace {
	// The assemblies that files refer to, loaded once (by their path) and kept for as long as the process runs
	class LoadedAssemblies {
	public:
		static LoadedAssemblies& Get() {
			static LoadedAssemblies loaded;
			return loaded;
		}
		ClrMetadata const* Load(std::wstring const& path) {
			auto key = path;
			for (auto& c : key)
				c = towlower(c);
			std::lock_guard lock(m_Lock);
			if (auto it = m_Files.find(key); it != m_Files.end())
				return it->second ? &it->second->Md : nullptr;
			auto file = std::make_unique<File>();
			if (!file->Pe.Open(path) || !file->Md.Parse(file->Pe) || !file->Md.HasMetadata())
				file.reset();
			auto md = file ? &file->Md : nullptr;
			m_Files[key] = std::move(file);
			return md;
		}

	private:
		struct File {
			PEFile Pe;
			ClrMetadata Md;
		};
		std::mutex m_Lock;
		std::unordered_map<std::wstring, std::unique_ptr<File>> m_Files;
	};

	std::wstring Environment(PCWSTR name) {
		WCHAR value[MAX_PATH];
		auto len = ::GetEnvironmentVariableW(name, value, _countof(value));
		return len && len < _countof(value) ? std::wstring(value, len) : std::wstring();
	}

	std::vector<std::wstring> Subdirectories(std::wstring const& dir) {
		std::vector<std::wstring> names;
		WIN32_FIND_DATAW fd;
		wil::unique_hfind find(::FindFirstFileW((dir + L"\\*").c_str(), &fd));
		if (!find)
			return names;
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
				names.push_back(fd.cFileName);
		} while (::FindNextFileW(find.get(), &fd));
		return names;
	}

	// "8.0.11" -> { 8, 0, 11 }; empty if it is not a version
	std::vector<int> VersionOf(std::wstring const& text) {
		std::vector<int> parts;
		for (size_t i = 0; i < text.size(); ) {
			if (!iswdigit(text[i]))
				return {};
			parts.push_back(_wtoi(text.c_str() + i));
			while (i < text.size() && iswdigit(text[i]))
				i++;
			if (i < text.size() && text[i] != L'.')
				return {};
			if (i < text.size())
				i++;
		}
		return parts;
	}

	// the folders of a shared framework of the .NET runtime ("Microsoft.NETCore.App"): the latest version of each major
	// one (by the major one), the latest first
	std::vector<std::pair<int, std::wstring>> RuntimeVersions(std::wstring const& framework) {
		static std::mutex lock;
		static std::unordered_map<std::wstring, std::vector<std::pair<int, std::wstring>>> found;
		std::lock_guard guard(lock);
		if (auto it = found.find(framework); it != found.end())
			return it->second;
		auto root = Environment(L"DOTNET_ROOT");
		if (root.empty())
			root = Environment(L"ProgramFiles") + L"\\dotnet";
		auto dir = root + L"\\shared\\" + framework;
		std::vector<std::pair<std::vector<int>, std::wstring>> versions;
		for (auto& name : Subdirectories(dir))
			if (auto v = VersionOf(name); !v.empty())
				versions.push_back({ v, dir + L"\\" + name });
		std::sort(versions.begin(), versions.end(), [](auto& a, auto& b) { return a.first > b.first; });
		auto& list = found[framework];
		for (auto& [v, path] : versions)
			if (std::none_of(list.begin(), list.end(), [&](auto& e) { return e.first == v[0]; }))
				list.push_back({ v[0], path });
		return list;
	}

	// the folder of the .NET Framework 4 (with mscorlib)
	std::wstring const& FrameworkFolder() {
		static std::wstring const dir = [] {
			auto windows = Environment(L"WINDIR");
			auto dir = windows + L"\\Microsoft.NET\\Framework64\\v4.0.30319";
			if (::GetFileAttributesW((dir + L"\\mscorlib.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
				dir = windows + L"\\Microsoft.NET\\Framework\\v4.0.30319";
			return dir;
		}();
		return dir;
	}

	bool FileExists(std::wstring const& path) {
		auto attributes = ::GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	// the C# name of a type an attribute names ("Outer+Inner`1" -> "Outer.Inner")
	std::wstring AttributeEnumName(std::vector<std::wstring> const& path) {
		std::wstring name;
		for (size_t i = 0; i < path.size(); i++) {
			auto part = path[i];
			if (i == 0)
				if (auto dot = part.rfind(L'.'); dot != std::wstring::npos)
					part = part.substr(dot + 1);
			name += (i ? L"." : L"") + part.substr(0, part.find(L'`'));
		}
		return name;
	}
}

ClrMetadata::ClrMetadata() : m_References(std::make_shared<ResolvedReferences>()) {
}

bool ClrMetadata::IsCoreApp() const {
	for (auto& r : m_Refs)
		if (_wcsicmp(r.Name.c_str(), L"mscorlib") == 0)
			return false;
	for (auto& r : m_Refs)
		if (_wcsicmp(r.Name.c_str(), L"System.Runtime") == 0 || _wcsicmp(r.Name.c_str(), L"System.Private.CoreLib") == 0)
			return true;
	if (m_HasAssembly && _wcsicmp(m_Assembly.Name.c_str(), L"System.Private.CoreLib") == 0)
		return true;
	if (m_PE) {
		auto path = m_PE->GetPath();
		for (auto& c : path)
			c = towlower(c);
		if (path.find(L"\\dotnet\\shared\\") != std::wstring::npos)
			return true;
		if (path.find(L"\\microsoft.net\\") != std::wstring::npos)
			return false;
	}
	// netstandard: whichever there is
	return !RuntimeVersions(L"Microsoft.NETCore.App").empty();
}

// next to the file, then in the .NET runtime or the .NET Framework (the one the file is for first)
ClrMetadata const* ClrMetadata::FindAssembly(std::wstring const& name, uint16_t major) const {
	if (name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos)
		return nullptr;
	std::vector<std::wstring> folders;
	if (m_PE) {
		auto& path = m_PE->GetPath();
		if (auto slash = path.find_last_of(L"\\/"); slash != std::wstring::npos)
			folders.push_back(path.substr(0, slash));
	}
	std::vector<std::wstring> runtime;
	for (auto framework : { L"Microsoft.NETCore.App", L"Microsoft.WindowsDesktop.App", L"Microsoft.AspNetCore.App" }) {
		// the version the assembly is of, or the latest one
		auto versions = RuntimeVersions(framework);
		auto it = std::find_if(versions.begin(), versions.end(), [&](auto& v) { return v.first == major; });
		if (it == versions.end())
			it = versions.begin();
		if (it != versions.end())
			runtime.push_back(it->second);
	}
	std::vector<std::wstring> framework{ FrameworkFolder(), FrameworkFolder() + L"\\WPF" };
	auto windows = Environment(L"WINDIR");
	for (auto gac : { L"GAC_MSIL", L"GAC_64", L"GAC_32" }) {
		auto dir = windows + L"\\Microsoft.NET\\assembly\\" + gac + L"\\" + name;
		for (auto& version : Subdirectories(dir))
			if (version.starts_with(L"v4.0_"))
				framework.push_back(dir + L"\\" + version);
	}
	auto core = IsCoreApp();
	auto& first = core ? runtime : framework;
	auto& second = core ? framework : runtime;
	folders.insert(folders.end(), first.begin(), first.end());
	folders.insert(folders.end(), second.begin(), second.end());

	for (auto& folder : folders)
		for (auto extension : { L".dll", L".exe" }) {
			auto path = folder + L"\\" + name + extension;
			if (!FileExists(path))
				continue;
			if (auto md = LoadedAssemblies::Get().Load(path); md && md->m_HasAssembly && _wcsicmp(md->m_Assembly.Name.c_str(), name.c_str()) == 0)
				return md;
		}
	return nullptr;
}

ClrMetadata const* ClrMetadata::ReferencedAssembly(uint32_t assemblyRef) const {
	if (assemblyRef == 0 || assemblyRef > m_Refs.size())
		return nullptr;
	{
		std::lock_guard lock(m_References->Lock);
		if (auto it = m_References->Assemblies.find(assemblyRef); it != m_References->Assemblies.end())
			return it->second;
	}
	auto& ref = m_Refs[assemblyRef - 1];
	auto md = FindAssembly(ref.Name, ref.Major);
	std::lock_guard lock(m_References->Lock);
	m_References->Assemblies[assemblyRef] = md;
	return md;
}

std::pair<ClrMetadata const*, uint32_t> ClrMetadata::FindType(std::wstring const& ns, std::wstring const& name, int depth) const {
	{
		std::lock_guard lock(m_References->Lock);
		if (!m_References->Indexed) {
			for (uint32_t t = 1; t <= m_Types.size(); t++)
				if (m_Types[t - 1].Enclosing == 0)
					m_References->TypeDefs.emplace(m_Types[t - 1].Namespace + L"|" + m_Types[t - 1].Name, t);
			m_References->Indexed = true;
		}
		if (auto it = m_References->TypeDefs.find(ns + L"|" + name); it != m_References->TypeDefs.end())
			return { this, it->second };
	}
	// a type forwarded to another assembly (ExportedType: Flags, TypeDefId, TypeName, TypeNamespace, Implementation)
	if (depth > 8)
		return {};
	for (uint32_t row = 1; row <= m_Rows[ClrTableId::ExportedType]; row++) {
		ClrRowReader r(*this, ClrTableId::ExportedType, row);
		r.U32();
		r.U32();
		auto typeName = r.Str();
		auto typeNamespace = r.Str();
		auto implementation = r.Coded(Implementation);
		if (!r.R.Ok || TokenTable(implementation) != ClrTableId::AssemblyRef || String(typeName) != name || String(typeNamespace) != ns)
			continue;
		if (auto md = ReferencedAssembly(TokenRow(implementation)))
			return md->FindType(ns, name, depth + 1);
		return {};
	}
	return {};
}

std::pair<ClrMetadata const*, uint32_t> ClrMetadata::ResolveType(uint32_t token) const {
	auto row = TokenRow(token);
	if (TokenTable(token) == ClrTableId::TypeDef) {
		if (row == 0 || row > m_Types.size())
			return {};
		return { this, row };
	}
	if (TokenTable(token) != ClrTableId::TypeRef || row == 0 || row > m_Rows[ClrTableId::TypeRef])
		return {};
	{
		std::lock_guard lock(m_References->Lock);
		if (auto it = m_References->Types.find(row); it != m_References->Types.end())
			return it->second;
	}
	std::pair<ClrMetadata const*, uint32_t> found{};
	ClrRowReader r(*this, ClrTableId::TypeRef, row);
	auto scope = r.Coded(ResolutionScope);
	auto name = String(r.Str());
	auto ns = String(r.Str());
	if (r.R.Ok) {
		switch (TokenTable(scope)) {
			case ClrTableId::AssemblyRef:
				if (auto md = ReferencedAssembly(TokenRow(scope)))
					found = md->FindType(ns, name);
				break;
			case ClrTableId::Module:
				found = FindType(ns, name);
				break;
			case ClrTableId::TypeRef:
			{
				// a nested type: in the type it is nested in
				static thread_local int depth = 0;
				if (depth < 16) {
					depth++;
					auto [md, outer] = ResolveType(scope);
					depth--;
					if (md)
						for (auto nested : md->NestedTypes(outer))
							if (md->m_Types[nested - 1].Name == name) {
								found = { md, nested };
								break;
							}
				}
				break;
			}
		}
	}
	std::lock_guard lock(m_References->Lock);
	m_References->Types[row] = found;
	return found;
}

std::shared_ptr<ClrEnumInfo const> ClrMetadata::MakeEnumInfo(uint32_t type, std::wstring name) const {
	if (type == 0 || type > m_Types.size() || TypeKind(type) != L"enum")
		return nullptr;
	auto info = std::make_shared<ClrEnumInfo>();
	info->Name = std::move(name);
	for (auto f : FieldsOf(type)) {
		auto& field = m_Fields[f - 1];
		if ((field.Flags & 0x10) == 0) {	// value__
			auto blob = Blob(field.Signature);
			if (blob.size() >= 2) {
				auto et = std::to_integer<uint8_t>(blob[1]);
				if (et >= 0x02 && et <= 0x0B)
					info->Underlying = et;
			}
			continue;
		}
		if (auto value = ConstantInteger((ClrTableId::Field << 24) | f))
			info->Values.push_back({ field.Name, *value, (ClrTableId::Field << 24) | f });
	}
	for (auto& attribute : CustomAttributes((ClrTableId::TypeDef << 24) | type)) {
		auto attributeName = TypeName(attribute.AttributeType);
		if (attributeName == L"System.FlagsAttribute" || attributeName.ends_with(L"]System.FlagsAttribute"))
			info->Flags = true;
	}
	return info;
}

// an enum of another assembly, for this file: its fields are not this file's
static std::shared_ptr<ClrEnumInfo const> WithoutFields(std::shared_ptr<ClrEnumInfo const> info) {
	if (!info)
		return info;
	auto copy = std::make_shared<ClrEnumInfo>(*info);
	for (auto& v : copy->Values)
		v.Field = 0;
	return copy;
}

std::shared_ptr<ClrEnumInfo const> ClrMetadata::EnumInfo(uint32_t token) const {
	auto table = TokenTable(token);
	if (table != ClrTableId::TypeDef && table != ClrTableId::TypeRef)
		return nullptr;
	{
		std::lock_guard lock(m_References->Lock);
		if (auto it = m_References->Enums.find(token); it != m_References->Enums.end())
			return it->second;
	}
	std::shared_ptr<ClrEnumInfo const> info;
	if (auto [md, type] = ResolveType(token); md) {
		info = md->MakeEnumInfo(type, CSharpType(token).Name);
		if (md != this)
			info = WithoutFields(info);
	}
	std::lock_guard lock(m_References->Lock);
	m_References->Enums[token] = info;
	return info;
}

std::shared_ptr<ClrEnumInfo const> ClrMetadata::EnumInfoByName(std::wstring const& serialized) const {
	{
		std::lock_guard lock(m_References->Lock);
		if (auto it = m_References->EnumsByName.find(serialized); it != m_References->EnumsByName.end())
			return it->second;
	}
	// "Ns.Outer+Inner, Assembly, Version=4.0.0.0, ...": the type in that assembly; without one, in this one or the core library
	auto comma = serialized.find(L',');
	auto full = serialized.substr(0, comma);
	std::wstring assembly;
	if (comma != std::wstring::npos)
		if (auto start = serialized.find_first_not_of(L' ', comma + 1); start != std::wstring::npos)
			assembly = serialized.substr(start, serialized.find(L',', start) - start);
	std::vector<std::wstring> path;
	for (size_t start = 0; ; ) {
		auto plus = full.find(L'+', start);
		path.push_back(full.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
		if (plus == std::wstring::npos)
			break;
		start = plus + 1;
	}
	auto dot = path[0].rfind(L'.');
	auto ns = dot == std::wstring::npos ? std::wstring() : path[0].substr(0, dot);
	auto name = dot == std::wstring::npos ? path[0] : path[0].substr(dot + 1);

	std::vector<ClrMetadata const*> places;
	if (!assembly.empty())
		places.push_back(m_HasAssembly && _wcsicmp(m_Assembly.Name.c_str(), assembly.c_str()) == 0 ? this : FindAssembly(assembly, 0));
	else {
		places.push_back(this);
		for (auto core : { L"mscorlib", L"System.Private.CoreLib", L"System.Runtime" }) {
			auto it = std::find_if(m_Refs.begin(), m_Refs.end(), [&](auto& r) { return _wcsicmp(r.Name.c_str(), core) == 0; });
			if (it != m_Refs.end())
				places.push_back(ReferencedAssembly((uint32_t)(it - m_Refs.begin()) + 1));
		}
	}
	std::shared_ptr<ClrEnumInfo const> info;
	for (auto place : places) {
		if (!place)
			continue;
		auto [md, type] = place->FindType(ns, name);
		for (size_t i = 1; md && type && i < path.size(); i++) {
			uint32_t inner = 0;
			for (auto nested : md->NestedTypes(type))
				if (md->m_Types[nested - 1].Name == path[i])
					inner = nested;
			type = inner;
		}
		if (!md || !type)
			continue;
		info = md->MakeEnumInfo(type, md == this ? CSharpTypeDefName(type) : AttributeEnumName(path));
		if (md != this)
			info = WithoutFields(info);
		break;
	}
	std::lock_guard lock(m_References->Lock);
	m_References->EnumsByName[serialized] = info;
	return info;
}

//
// Signatures as types, and layouts
//

namespace {
	bool DecodeSigType(ClrSignature& sig, ClrSigType& type, int depth) {
		if (depth > 32 || !sig.Ok)
			return false;
		auto et = sig.Byte();
		while (et == 0x1F || et == 0x20 || et == 0x45) {	// modifiers, pinned
			if (et != 0x45)
				sig.TypeToken();
			et = sig.Byte();
		}
		type = {};
		type.Element = et;
		switch (et) {
			case 0x11: case 0x12:
				type.Token = sig.TypeToken();
				break;
			case 0x0F: case 0x10: case 0x1D:
			{
				ClrSigType inner;
				if (!DecodeSigType(sig, inner, depth + 1))
					return false;
				type.Inner.push_back(std::move(inner));
				break;
			}
			case 0x14:
			{
				ClrSigType inner;
				if (!DecodeSigType(sig, inner, depth + 1))
					return false;
				type.Inner.push_back(std::move(inner));
				auto rank = sig.Compressed();
				auto sizes = std::min<uint32_t>(sig.Compressed(), 32);
				uint64_t count = sizes ? 1 : 0;
				for (uint32_t i = 0; i < sizes; i++)
					count *= sig.Compressed();
				for (auto bounds = std::min<uint32_t>(sig.Compressed(), 32); bounds--; )
					sig.Signed();
				(void)rank;
				type.Count = count > 0xFFFFFFFF ? 0 : (uint32_t)count;
				break;
			}
			case 0x15:
			{
				ClrSigType generic;
				if (!DecodeSigType(sig, generic, depth + 1))
					return false;
				type.Inner.push_back(std::move(generic));
				for (auto count = std::min<uint32_t>(sig.Compressed(), 32); count--; ) {
					ClrSigType arg;
					if (!DecodeSigType(sig, arg, depth + 1))
						return false;
					type.Inner.push_back(std::move(arg));
				}
				break;
			}
			case 0x13: case 0x1E:
				type.Count = sig.Compressed();	// the generic parameter's number
				break;
			case 0x1B:
				return false;	// (a function pointer: not decoded)
		}
		return sig.Ok;
	}
}

bool ClrMetadata::FieldSigType(uint32_t field, ClrSigType& type) const {
	if (field == 0 || field > m_Fields.size())
		return false;
	ClrSignature sig{ *this, Blob(m_Fields[field - 1].Signature) };
	if ((sig.Byte() & 0x0F) != 0x06)
		return false;
	return DecodeSigType(sig, type, 0);
}

bool ClrMetadata::MethodSigTypes(uint32_t method, ClrSigType& result, std::vector<ClrSigType>& params) const {
	if (method == 0 || method > m_Methods.size())
		return false;
	ClrSignature sig{ *this, Blob(m_Methods[method - 1].Signature) };
	auto conv = sig.Byte();
	if (conv & 0x10)
		sig.Compressed();	// generic parameters
	auto count = std::min<uint32_t>(sig.Compressed(), 256);
	if (!DecodeSigType(sig, result, 0))
		return false;
	params.clear();
	for (uint32_t i = 0; i < count; i++) {
		ClrSigType param;
		if (!DecodeSigType(sig, param, 0))
			return false;
		params.push_back(std::move(param));
	}
	return true;
}

// ClassLayout: PackingSize (2), ClassSize (4), Parent (TypeDef); FieldLayout: Offset (4), Field
std::pair<uint16_t, uint32_t> ClrMetadata::ClassLayout(uint32_t type) const {
	for (uint32_t row = 1; row <= m_Rows[0x0F]; row++) {
		ClrRowReader r(*this, 0x0F, row);
		auto packing = (uint16_t)r.U16();
		auto size = r.U32();
		if (r.R.Ok && r.Simple(ClrTableId::TypeDef) == type)
			return { packing, size };
	}
	return {};
}

std::optional<uint32_t> ClrMetadata::FieldOffset(uint32_t field) const {
	{
		std::lock_guard lock(m_References->Lock);
		auto& offsets = m_References->FieldOffsets;
		if (!m_References->OffsetsRead) {
			for (uint32_t row = 1; row <= m_Rows[0x10]; row++) {
				ClrRowReader r(*this, 0x10, row);
				auto offset = r.U32();
				auto of = r.Simple(ClrTableId::Field);
				if (r.R.Ok)
					offsets[of] = offset;
			}
			m_References->OffsetsRead = true;
		}
		if (auto it = offsets.find(field); it != offsets.end())
			return it->second;
	}
	return std::nullopt;
}
