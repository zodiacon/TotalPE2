#include "pch.h"
#include "ClrMetadata.h"
#include <cstring>

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
		if (m.Flags & f.Flag)
			text += std::format(L" {}", f.Text);
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
