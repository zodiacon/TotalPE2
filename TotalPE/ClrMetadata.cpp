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

	std::wstring ReadString(std::span<const std::byte> md, ClrStream const* heap, uint32_t index) {
		if (!heap || index >= heap->Size || (uint64_t)heap->Offset + heap->Size > md.size())
			return {};
		auto p = reinterpret_cast<const char*>(md.data()) + heap->Offset + index;
		auto max = heap->Size - index;
		return Utf8ToWide(std::string_view(p, strnlen(p, max)));
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
}

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

	auto& md = m_Header.MetaData;
	if (md.VirtualAddress == 0 || md.Size < 16)
		return true;
	auto mdOff = pe.GetOffsetFromRVA(md.VirtualAddress);
	if (mdOff == 0 || mdOff >= pe.GetFileSize())
		return true;
	auto size = (uint32_t)std::min<uint64_t>(md.Size, pe.GetFileSize() - mdOff);
	auto span = pe.GetSpan((uint32_t)mdOff, size);

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

	ClrStream const* tables = nullptr, * strings = nullptr;
	for (auto& s : m_Streams) {
		if (s.Name == "#~" || s.Name == "#-")
			tables = &s;
		else if (s.Name == "#Strings")
			strings = &s;
	}
	if (tables && (uint64_t)tables->Offset + tables->Size <= span.size())
		ParseTables(span, *tables, strings);
	return true;
}

bool ClrMetadata::ParseTables(std::span<const std::byte> md, ClrStream const& stream, ClrStream const* strings) {
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

	uint32_t rows[64]{};
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

	auto strIdx = (heapSizes & 1) ? 4u : 2u;
	auto guidIdx = (heapSizes & 2) ? 4u : 2u;
	auto blobIdx = (heapSizes & 4) ? 4u : 2u;
	auto simple = [&](int t) { return rows[t] > 0xFFFF ? 4u : 2u; };
	auto coded = [&](Coded c) {
		uint32_t max = 0;
		for (auto t : codedTables[c])
			if (t >= 0)
				max = std::max(max, rows[t]);
		return max < (1u << (16 - codedBits[c])) ? 2u : 4u;
	};

	// row sizes (ECMA-335 II.22)
	uint32_t sz[64]{};
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

	// tables above 0x2C (portable PDB) have no size here, so stop there
	size_t starts[64]{};
	size_t pos = r.Pos;
	for (int i = 0; i < 0x2D; i++) {
		if (!(valid & (1ULL << i)))
			continue;
		starts[i] = pos;
		pos += (size_t)rows[i] * sz[i];
	}

	auto readAt = [&](int table, uint32_t row, size_t colOffset, size_t colSize) -> uint32_t {
		Reader rr{ r.Data, starts[table] + (size_t)row * sz[table] + colOffset };
		return rr.Read(colSize);
	};

	if (rows[0x00] && starts[0x00])
		m_ModuleName = ReadString(md, strings, readAt(0x00, 0, 2, strIdx));

	// Assembly: HashAlgId(4), version(8), Flags(4), PublicKey, Name, Culture
	// AssemblyRef: version(8), Flags(4), PublicKeyOrToken, Name, Culture, HashValue
	auto readAsm = [&](int table, uint32_t row, bool isRef) {
		ClrAssemblyInfo a;
		size_t o = isRef ? 0 : 4;
		a.Major = (uint16_t)readAt(table, row, o, 2);
		a.Minor = (uint16_t)readAt(table, row, o + 2, 2);
		a.Build = (uint16_t)readAt(table, row, o + 4, 2);
		a.Revision = (uint16_t)readAt(table, row, o + 6, 2);
		a.Flags = readAt(table, row, o + 8, 4);
		o += 12 + blobIdx;
		a.Name = ReadString(md, strings, readAt(table, row, o, strIdx));
		a.Culture = ReadString(md, strings, readAt(table, row, o + strIdx, strIdx));
		return a;
	};

	if (rows[0x20] && starts[0x20]) {
		m_Assembly = readAsm(0x20, 0, false);
		m_HasAssembly = true;
	}
	if (starts[0x23]) {
		for (uint32_t i = 0; i < rows[0x23] && i < 4096; i++)
			m_Refs.push_back(readAsm(0x23, i, true));
	}
	return true;
}
