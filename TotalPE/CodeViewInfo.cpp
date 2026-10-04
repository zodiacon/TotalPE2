#include "pch.h"
#include "CodeViewInfo.h"
#include "CoffObject.h"
#include <algorithm>
#include <format>
#include <span>
#include <unordered_map>

namespace {
	using Bytes = std::span<const std::byte>;

	constexpr uint32_t CvSignatureC13 = 4;
	constexpr uint32_t FirstTypeIndex = 0x1000;

	// subsections of .debug$S
	constexpr uint32_t DebugSymbols = 0xF1, DebugLines = 0xF2, DebugStringTable = 0xF3, DebugFileChecksums = 0xF4;

	// symbol records
	enum : uint16_t {
		S_END = 0x0006, S_FRAMEPROC = 0x1012, S_OBJNAME = 0x1101, S_THUNK32 = 0x1102, S_BLOCK32 = 0x1103, S_WITH32 = 0x1104,
		S_LABEL32 = 0x1105, S_REGISTER = 0x1106, S_CONSTANT = 0x1107, S_UDT = 0x1108, S_BPREL32 = 0x110B, S_LDATA32 = 0x110C,
		S_GDATA32 = 0x110D, S_PUB32 = 0x110E, S_LPROC32 = 0x110F, S_GPROC32 = 0x1110, S_REGREL32 = 0x1111, S_LTHREAD32 = 0x1112,
		S_GTHREAD32 = 0x1113, S_COMPILE2 = 0x1116, S_LMANDATA = 0x111C, S_GMANDATA = 0x111D, S_UNAMESPACE = 0x1124,
		S_SEPCODE = 0x1132, S_SECTION = 0x1136, S_COFFGROUP = 0x1137, S_CALLSITEINFO = 0x1139, S_FRAMECOOKIE = 0x113A,
		S_COMPILE3 = 0x113C, S_ENVBLOCK = 0x113D, S_LOCAL = 0x113E, S_LPROC32_ID = 0x1146, S_GPROC32_ID = 0x1147,
		S_BUILDINFO = 0x114C, S_INLINESITE = 0x114D, S_INLINESITE_END = 0x114E, S_PROC_ID_END = 0x114F,
		S_LPROC32_DPC = 0x1155, S_LPROC32_DPC_ID = 0x1156, S_INLINESITE2 = 0x115D, S_HEAPALLOCSITE = 0x115E,
	};

	// type records (leaves)
	enum : uint16_t {
		LF_VTSHAPE = 0x000A, LF_ENDPRECOMP = 0x0014, LF_MODIFIER = 0x1001, LF_POINTER = 0x1002, LF_PROCEDURE = 0x1008,
		LF_MFUNCTION = 0x1009, LF_ARGLIST = 0x1201, LF_FIELDLIST = 0x1203, LF_BITFIELD = 0x1205, LF_METHODLIST = 0x1206,
		LF_BCLASS = 0x1400, LF_VBCLASS = 0x1401, LF_IVBCLASS = 0x1402, LF_INDEX = 0x1404, LF_VFUNCTAB = 0x1409,
		LF_TYPESERVER = 0x1501, LF_ENUMERATE = 0x1502, LF_ARRAY = 0x1503, LF_CLASS = 0x1504, LF_STRUCTURE = 0x1505,
		LF_UNION = 0x1506, LF_ENUM = 0x1507, LF_PRECOMP = 0x1509, LF_MEMBER = 0x150D, LF_STMEMBER = 0x150E,
		LF_METHOD = 0x150F, LF_NESTTYPE = 0x1510, LF_ONEMETHOD = 0x1511, LF_NESTTYPEEX = 0x1512, LF_TYPESERVER2 = 0x1515,
		LF_INTERFACE = 0x1519, LF_BINTERFACE = 0x151A, LF_VFTABLE = 0x151D,
		LF_FUNC_ID = 0x1601, LF_MFUNC_ID = 0x1602, LF_BUILDINFO = 0x1603, LF_SUBSTR_LIST = 0x1604, LF_STRING_ID = 0x1605,
		LF_UDT_SRC_LINE = 0x1606, LF_UDT_MOD_SRC_LINE = 0x1607, LF_CLASS2 = 0x1608, LF_STRUCTURE2 = 0x1609,
		LF_UNION2 = 0x160A, LF_INTERFACE2 = 0x160B,
	};

	template<typename T>
	T Read(Bytes data, size_t offset) {
		T value{};
		if (offset <= data.size() && data.size() - offset >= sizeof(T))
			memcpy(&value, data.data() + offset, sizeof(T));
		return value;
	}

	// a zero-terminated string (up to the end of the data if it has no zero)
	std::string_view Str(Bytes data, size_t offset) {
		if (offset >= data.size())
			return {};
		auto p = (const char*)data.data() + offset;
		auto size = data.size() - offset;
		return std::string_view(p, strnlen(p, size));
	}

	// the size of a string with its zero
	size_t StrSize(Bytes data, size_t offset) {
		return Str(data, offset).size() + 1;
	}

	// names are UTF-8
	std::wstring Utf8(std::string_view s) {
		if (s.empty())
			return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring text(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), text.data(), n);
		return text;
	}

	// A numeric leaf: a value below 0x8000 is the value itself; above, the leaf says what follows
	struct Numeric {
		int64_t Value{};
		size_t Size{};		// 0 if the leaf is not a known one
	};

	Numeric ReadNumeric(Bytes data, size_t offset) {
		auto leaf = Read<uint16_t>(data, offset);
		if (leaf < 0x8000)
			return { leaf, 2 };
		switch (leaf) {
			case 0x8000: return { Read<int8_t>(data, offset + 2), 3 };		// LF_CHAR
			case 0x8001: return { Read<int16_t>(data, offset + 2), 4 };		// LF_SHORT
			case 0x8002: return { Read<uint16_t>(data, offset + 2), 4 };	// LF_USHORT
			case 0x8003: return { Read<int32_t>(data, offset + 2), 6 };		// LF_LONG
			case 0x8004: return { Read<uint32_t>(data, offset + 2), 6 };	// LF_ULONG
			case 0x8009: return { Read<int64_t>(data, offset + 2), 10 };	// LF_QUADWORD
			case 0x800A: return { (int64_t)Read<uint64_t>(data, offset + 2), 10 };	// LF_UQUADWORD
		}
		return {};
	}

	std::wstring BasicTypeName(uint32_t index) {
		PCWSTR name = nullptr;
		switch (index & 0xFF) {
			case 0x00: name = L"<no type>"; break;
			case 0x03: name = L"void"; break;
			case 0x07: name = L"<not translated>"; break;
			case 0x08: name = L"HRESULT"; break;
			case 0x10: name = L"signed char"; break;
			case 0x20: name = L"unsigned char"; break;
			case 0x68: name = L"int8_t"; break;
			case 0x69: name = L"uint8_t"; break;
			case 0x70: name = L"char"; break;
			case 0x71: name = L"wchar_t"; break;
			case 0x7A: name = L"char16_t"; break;
			case 0x7B: name = L"char32_t"; break;
			case 0x7C: name = L"char8_t"; break;
			case 0x11: case 0x72: name = L"short"; break;
			case 0x21: case 0x73: name = L"unsigned short"; break;
			case 0x12: name = L"long"; break;
			case 0x22: name = L"unsigned long"; break;
			case 0x74: name = L"int"; break;
			case 0x75: name = L"unsigned int"; break;
			case 0x13: case 0x76: name = L"__int64"; break;
			case 0x23: case 0x77: name = L"unsigned __int64"; break;
			case 0x14: case 0x78: name = L"__int128"; break;
			case 0x24: case 0x79: name = L"unsigned __int128"; break;
			case 0x46: name = L"half"; break;
			case 0x40: name = L"float"; break;
			case 0x41: name = L"double"; break;
			case 0x42: name = L"long double"; break;
			case 0x30: name = L"bool"; break;
			case 0x31: name = L"bool16"; break;
			case 0x32: name = L"bool32"; break;
			case 0x33: name = L"bool64"; break;
		}
		if (!name)
			return std::format(L"0x{:X}", index);
		// the mode: a pointer of some size
		return (index & 0xF00) ? std::wstring(name) + L"*" : name;
	}

	PCWSTR CallingConventionName(uint8_t cc) {
		switch (cc) {
			case 0x00: return L"__cdecl";
			case 0x02: return L"__pascal";
			case 0x04: return L"__fastcall";
			case 0x07: return L"__stdcall";
			case 0x09: return L"__syscall";
			case 0x0B: return L"__thiscall";
			case 0x16: return L"__clrcall";
			case 0x18: return L"__vectorcall";
		}
		return nullptr;
	}

	std::wstring RegisterName(uint16_t machine, uint16_t reg) {
		static PCWSTR const x64[] = { L"rax", L"rbx", L"rcx", L"rdx", L"rsi", L"rdi", L"rbp", L"rsp", L"r8", L"r9", L"r10", L"r11", L"r12", L"r13", L"r14", L"r15" };
		static PCWSTR const x86[] = { L"eax", L"ecx", L"edx", L"ebx", L"esp", L"ebp", L"esi", L"edi" };
		if (machine == IMAGE_FILE_MACHINE_ARM64) {
			if (reg >= 50 && reg <= 78)
				return std::format(L"x{}", reg - 50);
			if (reg == 79)
				return L"fp";
			if (reg == 80)
				return L"lr";
			if (reg == 81)
				return L"sp";
		}
		else if (machine == IMAGE_FILE_MACHINE_I386) {
			if (reg >= 17 && reg <= 24)
				return x86[reg - 17];
		}
		else if (reg >= 328 && reg <= 343)
			return x64[reg - 328];
		return std::format(L"register {}", reg);
	}

	PCWSTR LanguageName(uint8_t language) {
		static PCWSTR const names[] = {
			L"C", L"C++", L"Fortran", L"MASM", L"Pascal", L"Basic", L"Cobol", L"Link", L"Cvtres", L"Cvtpgd", L"C#", L"Visual Basic",
			L"ILAsm", L"Java", L"JScript", L"MSIL", L"HLSL", L"Objective-C", L"Objective-C++", L"Swift", L"AliasObj", L"Rust", L"Go",
		};
		return language < _countof(names) ? names[language] : L"Unknown";
	}

	// The types, by their index: the names of types (and the strings of the IDs) are built from them
	class TypeTable {
	public:
		void Add(uint32_t index, Bytes record) {
			m_Records[index] = record;
		}

		// the record from its leaf on (empty if there is no such type)
		Bytes Record(uint32_t index) const {
			auto it = m_Records.find(index);
			return it == m_Records.end() ? Bytes() : it->second;
		}

		std::wstring Name(uint32_t index, int depth = 0) const {
			if (index < FirstTypeIndex)
				return BasicTypeName(index);
			auto name = depth < 8 ? Declaration(index, depth) : L"...";
			return name.empty() ? std::format(L"0x{:X}", index) : name;
		}

		// what a string ID says: its substrings, then its own text (long strings are split this way)
		std::wstring String(uint32_t id, int depth = 0) const {
			auto rec = Record(id);
			if (rec.empty() || depth > 4)
				return {};
			switch (Read<uint16_t>(rec, 0)) {
				case LF_STRING_ID:
					return String(Read<uint32_t>(rec, 2), depth + 1) + Utf8(Str(rec, 6));
				case LF_SUBSTR_LIST:
				{
					std::wstring text;
					auto count = Read<uint32_t>(rec, 2);
					for (uint32_t i = 0; i < count && 6 + i * 4 + 4 <= rec.size(); i++)
						text += String(Read<uint32_t>(rec, 6 + i * 4), depth + 1);
					return text;
				}
			}
			return {};
		}

		// the type of a function's ID (an S_GPROC32_ID refers to the ID)
		uint32_t FunctionType(uint32_t id) const {
			auto rec = Record(id);
			auto leaf = Read<uint16_t>(rec, 0);
			return leaf == LF_FUNC_ID || leaf == LF_MFUNC_ID ? Read<uint32_t>(rec, 6) : id;
		}

		std::wstring Arguments(uint32_t arglist, int depth) const {
			auto rec = Record(arglist);
			if (Read<uint16_t>(rec, 0) != LF_ARGLIST)
				return L"...";
			std::wstring text;
			auto count = Read<uint32_t>(rec, 2);
			for (uint32_t i = 0; i < count && 6 + i * 4 + 4 <= rec.size(); i++) {
				if (i)
					text += L", ";
				text += Name(Read<uint32_t>(rec, 6 + i * 4), depth + 1);
			}
			return text;
		}

	private:
		std::wstring Declaration(uint32_t index, int depth) const {
			auto rec = Record(index);
			if (rec.size() < 2)
				return {};
			switch (Read<uint16_t>(rec, 0)) {
				case LF_MODIFIER:
				{
					auto attr = Read<uint16_t>(rec, 6);
					std::wstring prefix = (attr & 1) ? L"const " : L"";
					if (attr & 2)
						prefix += L"volatile ";
					if (attr & 4)
						prefix += L"__unaligned ";
					return prefix + Name(Read<uint32_t>(rec, 2), depth + 1);
				}
				case LF_POINTER:
				{
					auto mode = (Read<uint32_t>(rec, 6) >> 5) & 7;
					auto pointee = Name(Read<uint32_t>(rec, 2), depth + 1);
					return pointee + (mode == 1 ? L"&" : mode == 4 ? L"&&" : mode == 2 || mode == 3 ? L"::*" : L"*");
				}
				case LF_PROCEDURE:
					return std::format(L"{}({})", Name(Read<uint32_t>(rec, 2), depth + 1), Arguments(Read<uint32_t>(rec, 10), depth));
				case LF_MFUNCTION:
					return std::format(L"{} {}::({})", Name(Read<uint32_t>(rec, 2), depth + 1), Name(Read<uint32_t>(rec, 6), depth + 1),
						Arguments(Read<uint32_t>(rec, 18), depth));
				case LF_ARGLIST:
					return L"(" + Arguments(index, depth) + L")";
				case LF_ARRAY:
					return std::format(L"{}[]", Name(Read<uint32_t>(rec, 2), depth + 1));
				case LF_BITFIELD:
					return std::format(L"{} : {}", Name(Read<uint32_t>(rec, 2), depth + 1), Read<uint8_t>(rec, 6));
				case LF_CLASS: case LF_STRUCTURE: case LF_INTERFACE:
					return Utf8(Str(rec, 18 + ReadNumeric(rec, 18).Size));
				case LF_UNION:
					return Utf8(Str(rec, 10 + ReadNumeric(rec, 10).Size));
				case LF_CLASS2: case LF_STRUCTURE2: case LF_INTERFACE2:
					return Utf8(Str(rec, 20 + ReadNumeric(rec, 20).Size));
				case LF_UNION2:
					return Utf8(Str(rec, 12 + ReadNumeric(rec, 12).Size));
				case LF_ENUM:
					return Utf8(Str(rec, 14));
				case LF_FUNC_ID:
					return Utf8(Str(rec, 10));
				case LF_MFUNC_ID:
					return Name(Read<uint32_t>(rec, 2), depth + 1) + L"::" + Utf8(Str(rec, 10));
				case LF_STRING_ID: case LF_SUBSTR_LIST:
					return String(index);
				case LF_UDT_SRC_LINE: case LF_UDT_MOD_SRC_LINE:
					return Name(Read<uint32_t>(rec, 2), depth + 1);
				case LF_TYPESERVER2:
					return Utf8(Str(rec, 22));
				case LF_TYPESERVER:
					return Utf8(Str(rec, 10));
				case LF_PRECOMP:
					return Utf8(Str(rec, 14));
			}
			return {};
		}

		std::unordered_map<uint32_t, Bytes> m_Records;
	};

	// Reads the CodeView information of an object
	class Reader {
	public:
		Reader(CoffObject const& obj, CodeViewInfo& info) : m_Object(obj), m_Info(info) {
			for (auto const& r : obj.Relocations())
				m_Relocations[Key(r.Section, r.Offset)] = r.SymbolIndex;
		}

		void Read() {
			auto const& sections = m_Object.Sections();
			for (uint32_t s = 0; s < (uint32_t)sections.size(); s++)
				if (sections[s].Name == ".debug$T" || sections[s].Name == ".debug$P")
					ReadTypes(s);

			// the file names first: the lines refer to them
			std::vector<uint32_t> debugSections;
			for (uint32_t s = 0; s < (uint32_t)sections.size(); s++)
				if (sections[s].Name == ".debug$S")
					debugSections.push_back(s);
			for (auto s : debugSections)
				ReadSubsections(s);
			for (auto const& sub : m_Info.Subsections)
				if (sub.Type == DebugStringTable && m_Strings.empty())
					m_Strings = m_Object.SectionData(sub.Section).subspan(sub.Offset + 8, sub.Size);
			for (auto const& sub : m_Info.Subsections)
				if (sub.Type == DebugFileChecksums) {
					ReadFiles(sub);
					break;
				}
			for (auto const& sub : m_Info.Subsections) {
				if (sub.Type == DebugSymbols)
					ReadSymbols(sub);
				else if (sub.Type == DebugLines)
					ReadLines(sub);
			}
		}

	private:
		static uint64_t Key(uint32_t section, uint32_t offset) {
			return ((uint64_t)section << 32) | offset;
		}

		// The relocation of a field of a record: the symbol it refers to
		CoffSymbol const* RelocatedSymbol(uint32_t section, size_t offset) const {
			auto it = m_Relocations.find(Key(section, (uint32_t)offset));
			return it == m_Relocations.end() ? nullptr : m_Object.SymbolByIndex(it->second);
		}

		// a section-relative offset (an address in an object), relocated against a symbol of a section
		CvTarget Resolve(uint32_t section, size_t offset, uint32_t value) const {
			auto sym = RelocatedSymbol(section, offset);
			if (sym == nullptr || sym->SectionNumber <= 0 || sym->SectionNumber > (int32_t)m_Object.Sections().size())
				return {};
			return { sym->SectionNumber - 1, sym->Value + value };
		}

		void ReadTypes(uint32_t section) {
			auto data = m_Object.SectionData(section);
			if (data.size() < 4)
				return;
			if (auto sig = ::Read<uint32_t>(data, 0); sig != CvSignatureC13) {
				m_Info.Problems.push_back(std::format(L"Section {} ({}) has CodeView signature {}; only 4 (C13) is supported",
					section + 1, Utf8(m_Object.Sections()[section].Name), sig));
				return;
			}
			bool table = m_Types.Record(FirstTypeIndex).empty();	// the first section of types is the one the indices refer to
			uint32_t index = FirstTypeIndex;
			size_t first = m_Info.Types.size();
			for (size_t pos = 4; pos + 4 <= data.size();) {
				auto len = ::Read<uint16_t>(data, pos);
				if (len < 2 || pos + 2 + len > data.size()) {
					m_Info.Problems.push_back(std::format(L"A type record at 0x{:X} of section {} does not fit in the section", pos, section + 1));
					break;
				}
				auto rec = data.subspan(pos + 2, len);
				auto leaf = ::Read<uint16_t>(rec, 0);
				CvType type;
				type.Section = section;
				type.Offset = (uint32_t)pos;
				type.Kind = leaf;
				type.Size = (uint16_t)(len + 2);
				// the types of a precompiled header come first: they are in the object of the header
				if (leaf == LF_PRECOMP) {
					index = ::Read<uint32_t>(rec, 2) + ::Read<uint32_t>(rec, 6);
					type.Index = 0;
				}
				else {
					type.Index = index++;
					if (table)
						m_Types.Add(type.Index, rec);
				}
				m_Info.Types.push_back(std::move(type));
				pos += 2 + len;
			}
			// the names, now that all the types are known
			for (size_t i = first; i < m_Info.Types.size(); i++)
				Describe(m_Info.Types[i], data.subspan(m_Info.Types[i].Offset + 2, m_Info.Types[i].Size - 2));
		}

		std::wstring TypeFlags(uint16_t property) const {
			std::wstring text;
			if (property & 0x80)
				text += L", forward reference";
			if (property & 0x08)
				text += L", nested";
			if (property & 0x01)
				text += L", packed";
			if (property & 0x400)
				text += L", sealed";
			return text;
		}

		void Describe(CvType& type, Bytes rec) {
			auto u32 = [&](size_t at) { return ::Read<uint32_t>(rec, at); };
			auto name = [&](uint32_t index) { return m_Types.Name(index); };
			if (type.Index)
				type.Name = m_Types.Name(type.Index);
			switch (type.Kind) {
				case LF_MODIFIER:
					type.Details = std::format(L"Modifies {}", name(u32(2)));
					break;
				case LF_POINTER:
				{
					auto attr = u32(6);
					PCWSTR modes[] = { L"Pointer", L"Reference", L"Pointer to data member", L"Pointer to member function", L"R-value reference" };
					auto mode = (attr >> 5) & 7;
					type.Details = std::format(L"{} to {}, {} bytes", mode < _countof(modes) ? modes[mode] : L"Pointer", name(u32(2)), (attr >> 13) & 0x3F);
					break;
				}
				case LF_PROCEDURE: case LF_MFUNCTION:
				{
					bool member = type.Kind == LF_MFUNCTION;
					auto cc = ::Read<uint8_t>(rec, member ? 14 : 6);
					auto count = ::Read<uint16_t>(rec, member ? 16 : 8);
					auto ccName = CallingConventionName(cc);
					type.Details = std::format(L"{}, {} parameters", ccName ? ccName : std::format(L"Calling convention {}", cc).c_str(), count);
					if (member)
						type.Details += std::format(L", this: {}", name(u32(10)));
					break;
				}
				case LF_ARGLIST:
					type.Details = std::format(L"{} arguments", u32(2));
					break;
				case LF_FIELDLIST:
					type.Details = Fields(rec);
					break;
				case LF_BITFIELD:
				{
					auto length = ::Read<uint8_t>(rec, 6), position = ::Read<uint8_t>(rec, 7);
					type.Details = std::format(L"Bits {}-{} of {}", position, position + length - 1, name(u32(2)));
					break;
				}
				case LF_ARRAY:
					type.Details = std::format(L"Of {}, {} bytes", name(u32(2)), ReadNumeric(rec, 10).Value);
					break;
				case LF_CLASS: case LF_STRUCTURE: case LF_INTERFACE:
				{
					auto property = ::Read<uint16_t>(rec, 4);
					type.Details = (property & 0x80) ? L"Forward reference" : std::format(L"{} bytes, {} members, fields 0x{:X}{}",
						ReadNumeric(rec, 18).Value, ::Read<uint16_t>(rec, 2), u32(6), TypeFlags(property));
					if (auto derived = u32(10))
						type.Details += std::format(L", derived 0x{:X}", derived);
					break;
				}
				case LF_CLASS2: case LF_STRUCTURE2: case LF_INTERFACE2:
				{
					auto property = (uint16_t)u32(2);
					type.Details = (property & 0x80) ? L"Forward reference" : std::format(L"{} bytes, {} members, fields 0x{:X}{}",
						ReadNumeric(rec, 20).Value, ::Read<uint16_t>(rec, 18), u32(6), TypeFlags(property));
					break;
				}
				case LF_UNION:
				{
					auto property = ::Read<uint16_t>(rec, 4);
					type.Details = (property & 0x80) ? L"Forward reference" : std::format(L"{} bytes, {} members, fields 0x{:X}{}",
						ReadNumeric(rec, 10).Value, ::Read<uint16_t>(rec, 2), u32(6), TypeFlags(property));
					break;
				}
				case LF_UNION2:
				{
					auto property = (uint16_t)u32(2);
					type.Details = (property & 0x80) ? L"Forward reference" : std::format(L"{} bytes, {} members, fields 0x{:X}{}",
						ReadNumeric(rec, 12).Value, ::Read<uint16_t>(rec, 10), u32(6), TypeFlags(property));
					break;
				}
				case LF_ENUM:
				{
					auto property = ::Read<uint16_t>(rec, 4);
					type.Details = (property & 0x80) ? L"Forward reference" : std::format(L"{} values of {}, fields 0x{:X}{}",
						::Read<uint16_t>(rec, 2), name(u32(6)), u32(10), TypeFlags(property));
					break;
				}
				case LF_FUNC_ID:
					type.Details = std::format(L"Type {}", name(u32(6)));
					if (auto scope = u32(2))
						type.Details += std::format(L", scope {}", name(scope));
					break;
				case LF_MFUNC_ID:
					type.Details = std::format(L"Type {}", name(u32(6)));
					break;
				case LF_STRING_ID:
					if (auto substrings = u32(2))
						type.Details = std::format(L"Starts with the substrings of 0x{:X}", substrings);
					break;
				case LF_SUBSTR_LIST:
					type.Details = std::format(L"{} strings", u32(2));
					break;
				case LF_BUILDINFO:
					type.Details = BuildInfo(rec);
					break;
				case LF_UDT_SRC_LINE: case LF_UDT_MOD_SRC_LINE:
					type.Details = std::format(L"{}({})", m_Types.String(u32(6)), u32(10));
					break;
				case LF_TYPESERVER2:
				{
					auto guid = ::Read<GUID>(rec, 2);
					type.Details = std::format(L"The types are in this PDB: GUID {{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}, age {}",
						guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
						guid.Data4[6], guid.Data4[7], u32(18));
					if (m_Info.TypeServer.empty())
						m_Info.TypeServer = type.Name;
					break;
				}
				case LF_TYPESERVER:
					type.Details = std::format(L"The types are in this PDB: signature 0x{:08X}, age {}", u32(2), u32(6));
					if (m_Info.TypeServer.empty())
						m_Info.TypeServer = Utf8(Str(rec, 10));
					break;
				case LF_PRECOMP:
					type.Name = Utf8(Str(rec, 14));
					type.Details = std::format(L"Types 0x{:X}-0x{:X} are in the object of the precompiled header, signature 0x{:08X}",
						u32(2), u32(2) + u32(6) - 1, u32(10));
					break;
				case LF_ENDPRECOMP:
					type.Details = std::format(L"Signature 0x{:08X}", u32(2));
					break;
				case LF_VTSHAPE:
					type.Details = std::format(L"{} entries", ::Read<uint16_t>(rec, 2));
					break;
			}
			// the index is all a record without a name has
			if (type.Name.size() > 2 && type.Name.starts_with(L"0x"))
				type.Name.clear();
		}

		// "x: int (+0); y: int (+4)"
		std::wstring Fields(Bytes rec) const {
			std::wstring text;
			int count = 0;
			auto add = [&](std::wstring field) {
				if (!text.empty())
					text += L"; ";
				text += field;
				count++;
			};
			size_t pos = 2;
			while (pos + 2 <= rec.size()) {
				// padding before the next field
				if (::Read<uint8_t>(rec, pos) >= 0xF0) {
					pos++;
					continue;
				}
				auto leaf = ::Read<uint16_t>(rec, pos);
				size_t at = pos + 2;
				switch (leaf) {
					case LF_MEMBER:
					{
						auto offset = ReadNumeric(rec, at + 6);
						if (offset.Size == 0)
							return text;
						auto nameAt = at + 6 + offset.Size;
						add(std::format(L"{}: {} (+{})", Utf8(Str(rec, nameAt)), m_Types.Name(::Read<uint32_t>(rec, at + 2)), offset.Value));
						pos = nameAt + StrSize(rec, nameAt);
						break;
					}
					case LF_ENUMERATE:
					{
						auto value = ReadNumeric(rec, at + 2);
						if (value.Size == 0)
							return text;
						auto nameAt = at + 2 + value.Size;
						add(std::format(L"{} = {}", Utf8(Str(rec, nameAt)), value.Value));
						pos = nameAt + StrSize(rec, nameAt);
						break;
					}
					case LF_BCLASS: case LF_BINTERFACE:
					{
						auto offset = ReadNumeric(rec, at + 6);
						if (offset.Size == 0)
							return text;
						add(std::format(L"base {} (+{})", m_Types.Name(::Read<uint32_t>(rec, at + 2)), offset.Value));
						pos = at + 6 + offset.Size;
						break;
					}
					case LF_VBCLASS: case LF_IVBCLASS:
					{
						auto first = ReadNumeric(rec, at + 10);
						auto second = ReadNumeric(rec, at + 10 + first.Size);
						if (first.Size == 0 || second.Size == 0)
							return text;
						add(std::format(L"virtual base {}", m_Types.Name(::Read<uint32_t>(rec, at + 2))));
						pos = at + 10 + first.Size + second.Size;
						break;
					}
					case LF_INDEX:
						add(std::format(L"(continued in 0x{:X})", ::Read<uint32_t>(rec, at + 2)));
						pos = at + 6;
						break;
					case LF_VFUNCTAB:
						add(L"vfptr");
						pos = at + 6;
						break;
					case LF_STMEMBER:
					{
						auto nameAt = at + 6;
						add(std::format(L"static {}: {}", Utf8(Str(rec, nameAt)), m_Types.Name(::Read<uint32_t>(rec, at + 2))));
						pos = nameAt + StrSize(rec, nameAt);
						break;
					}
					case LF_METHOD:
					{
						auto nameAt = at + 6;
						auto overloads = ::Read<uint16_t>(rec, at);
						add(overloads > 1 ? std::format(L"{}() ({} overloads)", Utf8(Str(rec, nameAt)), overloads) : std::format(L"{}()", Utf8(Str(rec, nameAt))));
						pos = nameAt + StrSize(rec, nameAt);
						break;
					}
					case LF_ONEMETHOD:
					{
						// an introducing virtual method has the offset in the vtable
						auto kind = (::Read<uint16_t>(rec, at) >> 2) & 7;
						auto nameAt = at + 6 + (kind == 4 || kind == 6 ? 4 : 0);
						add(std::format(L"{}()", Utf8(Str(rec, nameAt))));
						pos = nameAt + StrSize(rec, nameAt);
						break;
					}
					case LF_NESTTYPE: case LF_NESTTYPEEX:
					{
						auto nameAt = at + 6;
						add(std::format(L"nested {}", Utf8(Str(rec, nameAt))));
						pos = nameAt + StrSize(rec, nameAt);
						break;
					}
					default:
						return std::format(L"{} fields: {}...", count, text);
				}
			}
			return std::format(L"{} fields: {}", count, text);
		}

		std::wstring BuildInfo(Bytes rec) const {
			static PCWSTR const names[] = { L"Directory", L"Tool", L"Source", L"PDB", L"Command line" };
			std::wstring text;
			auto count = ::Read<uint16_t>(rec, 2);
			for (uint16_t i = 0; i < count && 4 + i * 4u + 4 <= rec.size(); i++) {
				auto value = m_Types.String(::Read<uint32_t>(rec, 4 + i * 4));
				if (value.empty())
					continue;
				if (!text.empty())
					text += L"; ";
				text += std::format(L"{}: {}", i < _countof(names) ? names[i] : L"Argument", value);
			}
			return text;
		}

		void ReadSubsections(uint32_t section) {
			auto data = m_Object.SectionData(section);
			if (data.size() < 4)
				return;
			if (auto sig = ::Read<uint32_t>(data, 0); sig != CvSignatureC13) {
				m_Info.Problems.push_back(std::format(L"Section {} (.debug$S) has CodeView signature {}; only 4 (C13) is supported", section + 1, sig));
				return;
			}
			for (size_t pos = 4; pos + 8 <= data.size();) {
				auto type = ::Read<uint32_t>(data, pos), size = ::Read<uint32_t>(data, pos + 4);
				if (size > data.size() - pos - 8) {
					m_Info.Problems.push_back(std::format(L"A subsection at 0x{:X} of section {} does not fit in the section", pos, section + 1));
					break;
				}
				m_Info.Subsections.push_back({ section, (uint32_t)pos, type, size });
				pos = (pos + 8 + size + 3) & ~(size_t)3;
			}
		}

		void ReadFiles(CvSubsection const& sub) {
			auto data = m_Object.SectionData(sub.Section);
			size_t start = sub.Offset + 8, end = start + sub.Size;
			for (size_t pos = start; pos + 6 <= end;) {
				CvFile file;
				file.Section = sub.Section;
				file.Offset = (uint32_t)pos;
				file.Id = (uint32_t)(pos - start);
				file.Name = Utf8(Str(m_Strings, ::Read<uint32_t>(data, pos)));
				auto size = ::Read<uint8_t>(data, pos + 4);
				file.ChecksumKind = ::Read<uint8_t>(data, pos + 5);
				if (pos + 6 + size > end)
					break;
				for (size_t i = 0; i < size; i++)
					file.Checksum += std::format(L"{:02X}", ::Read<uint8_t>(data, pos + 6 + i));
				m_Files[file.Id] = file.Name;
				m_Info.Files.push_back(std::move(file));
				pos = start + ((pos - start + 6 + size + 3) & ~(size_t)3);
			}
		}

		void ReadLines(CvSubsection const& sub) {
			auto data = m_Object.SectionData(sub.Section);
			size_t start = sub.Offset + 8, end = start + sub.Size;
			if (sub.Size < 12)
				return;
			auto offset = ::Read<uint32_t>(data, start);
			auto flags = ::Read<uint16_t>(data, start + 6);
			bool columns = flags & 1;
			auto sym = RelocatedSymbol(sub.Section, start);
			auto function = sym ? Utf8(sym->Name) : std::wstring();
			auto target = Resolve(sub.Section, start, offset);

			for (size_t pos = start + 12; pos + 12 <= end;) {
				auto fileId = ::Read<uint32_t>(data, pos), count = ::Read<uint32_t>(data, pos + 4), size = ::Read<uint32_t>(data, pos + 8);
				if (size < 12 || size > end - pos || (uint64_t)count * (columns ? 12 : 8) > size - 12) {
					m_Info.Problems.push_back(std::format(L"A block of lines at 0x{:X} of section {} does not fit in its subsection", pos, sub.Section + 1));
					break;
				}
				auto it = m_Files.find(fileId);
				auto file = it == m_Files.end() ? std::format(L"File 0x{:X}", fileId) : it->second;
				for (uint32_t i = 0; i < count; i++) {
					auto at = pos + 12 + i * 8;
					auto bits = ::Read<uint32_t>(data, at + 4);
					CvLine line;
					line.Section = sub.Section;
					line.Offset = (uint32_t)at;
					line.Function = function;
					line.File = file;
					line.CodeOffset = ::Read<uint32_t>(data, at);
					line.Line = bits & 0xFFFFFF;
					line.EndLine = line.Line + ((bits >> 24) & 0x7F);
					line.Statement = bits >> 31;
					if (columns) {
						auto col = pos + 12 + count * 8 + i * 4;
						line.Column = ::Read<uint16_t>(data, col);
						line.EndColumn = ::Read<uint16_t>(data, col + 2);
					}
					if (target.Section >= 0)
						line.Target = { target.Section, target.Offset + line.CodeOffset };
					m_Info.Lines.push_back(std::move(line));
				}
				pos += size;
			}
		}

		void ReadSymbols(CvSubsection const& sub) {
			auto data = m_Object.SectionData(sub.Section);
			size_t start = sub.Offset + 8, end = start + sub.Size;
			int depth = 0;
			for (size_t pos = start; pos + 4 <= end;) {
				auto len = ::Read<uint16_t>(data, pos);
				if (len < 2 || pos + 2 + len > end) {
					m_Info.Problems.push_back(std::format(L"A symbol record at 0x{:X} of section {} does not fit in its subsection", pos, sub.Section + 1));
					break;
				}
				CvSymbol sym;
				sym.Section = sub.Section;
				sym.Offset = (uint32_t)pos;
				sym.Kind = ::Read<uint16_t>(data, pos + 2);
				sym.Size = (uint16_t)(len + 2);
				// the body of the record, and its place in the section (for the relocations)
				auto body = data.subspan(pos + 4, len - 2);
				size_t at = pos + 4;
				bool opens = false;
				if (sym.Kind == S_END || sym.Kind == S_PROC_ID_END || sym.Kind == S_INLINESITE_END)
					depth = std::max(0, depth - 1);
				sym.Depth = depth;
				Describe(sym, body, at, opens);
				if (opens)
					depth++;
				m_Info.Symbols.push_back(std::move(sym));
				pos += 2 + len;
			}
		}

		void Describe(CvSymbol& sym, Bytes body, size_t at, bool& opens) {
			auto u32 = [&](size_t offset) { return ::Read<uint32_t>(body, offset); };
			auto name = [&](size_t offset) { return Utf8(Str(body, offset)); };
			auto type = [&](uint32_t index) { return m_Types.Name(index); };
			auto machine = m_Object.Machine();
			switch (sym.Kind) {
				case S_OBJNAME:
					sym.Name = name(4);
					sym.Details = std::format(L"Signature 0x{:X}", u32(0));
					break;
				case S_COMPILE3: case S_COMPILE2:
				{
					bool v3 = sym.Kind == S_COMPILE3;
					auto flags = u32(0);
					auto ver = [&](size_t offset, int count) {
						std::wstring text;
						for (int i = 0; i < count; i++)
							text += std::format(L"{}{}", i ? L"." : L"", ::Read<uint16_t>(body, offset + i * 2));
						return text;
					};
					sym.Name = name(v3 ? 22 : 18);
					sym.Details = std::format(L"{}, front end {}, back end {}", LanguageName(flags & 0xFF), ver(6, v3 ? 4 : 3), ver(v3 ? 14 : 12, v3 ? 4 : 3));
					static const std::pair<uint32_t, PCWSTR> options[] = {
						{ 0x100, L"Edit and Continue" }, { 0x200, L"No debug info" }, { 0x400, L"LTCG" }, { 0x2000, L"/GS" },
						{ 0x4000, L"/hotpatch" }, { 0x20000, L"/sdl" }, { 0x40000, L"PGO" },
					};
					for (auto [bit, option] : options)
						if (flags & bit)
							sym.Details += std::format(L", {}", option);
					if (m_Info.Compiler.empty())
						m_Info.Compiler = std::format(L"{} ({})", sym.Name, LanguageName(flags & 0xFF));
					break;
				}
				case S_BUILDINFO:
				{
					auto rec = m_Types.Record(u32(0));
					sym.Details = ::Read<uint16_t>(rec, 0) == LF_BUILDINFO ? BuildInfo(rec) : std::format(L"Build information 0x{:X}", u32(0));
					break;
				}
				case S_ENVBLOCK:
				{
					// pairs of strings, then an empty one
					for (size_t pos = 1; pos < body.size();) {
						auto key = Str(body, pos);
						if (key.empty())
							break;
						pos += key.size() + 1;
						auto value = Str(body, pos);
						pos += value.size() + 1;
						if (!sym.Details.empty())
							sym.Details += L"; ";
						sym.Details += Utf8(key) + L": " + Utf8(value);
					}
					break;
				}
				case S_GPROC32: case S_LPROC32: case S_GPROC32_ID: case S_LPROC32_ID: case S_LPROC32_DPC: case S_LPROC32_DPC_ID:
				{
					sym.Name = name(35);
					auto t = u32(24);
					if (sym.Kind == S_GPROC32_ID || sym.Kind == S_LPROC32_ID || sym.Kind == S_LPROC32_DPC_ID)
						t = m_Types.FunctionType(t);
					sym.Details = std::format(L"{} bytes, {}", u32(12), type(t));
					sym.Target = Resolve(sym.Section, at + 28, u32(28));
					opens = true;
					break;
				}
				case S_THUNK32:
					sym.Name = name(21);
					sym.Details = std::format(L"{} bytes", ::Read<uint16_t>(body, 18));
					sym.Target = Resolve(sym.Section, at + 12, u32(12));
					opens = true;
					break;
				case S_BLOCK32:
					sym.Name = name(18);
					sym.Details = std::format(L"{} bytes", u32(8));
					sym.Target = Resolve(sym.Section, at + 12, u32(12));
					opens = true;
					break;
				case S_WITH32: case S_SEPCODE:
					opens = true;
					break;
				case S_INLINESITE: case S_INLINESITE2:
					sym.Name = m_Types.Name(u32(8));
					sym.Details = L"Inlined";
					opens = true;
					break;
				case S_LABEL32:
					sym.Name = name(7);
					sym.Target = Resolve(sym.Section, at, u32(0));
					break;
				case S_REGISTER:
					sym.Name = name(6);
					sym.Details = std::format(L"{}, in {}", type(u32(0)), RegisterName(machine, ::Read<uint16_t>(body, 4)));
					break;
				case S_CONSTANT:
				{
					auto value = ReadNumeric(body, 4);
					sym.Name = name(4 + value.Size);
					sym.Details = std::format(L"{} = {}", type(u32(0)), value.Value);
					break;
				}
				case S_UDT:
					sym.Name = name(4);
					sym.Details = type(u32(0));
					break;
				case S_BPREL32:
					sym.Name = name(8);
					sym.Details = std::format(L"{}, frame {:+}", type(u32(4)), (int32_t)u32(0));
					break;
				case S_LDATA32: case S_GDATA32: case S_LTHREAD32: case S_GTHREAD32: case S_LMANDATA: case S_GMANDATA:
					sym.Name = name(10);
					sym.Details = type(u32(0));
					if (sym.Kind == S_LTHREAD32 || sym.Kind == S_GTHREAD32)
						sym.Details += L", thread local";
					sym.Target = Resolve(sym.Section, at + 4, u32(4));
					break;
				case S_PUB32:
					sym.Name = name(10);
					sym.Target = Resolve(sym.Section, at + 4, u32(4));
					break;
				case S_REGREL32:
				{
					auto offset = (int32_t)u32(0);
					sym.Name = name(10);
					sym.Details = std::format(L"{}, [{}{}0x{:X}]", type(u32(4)), RegisterName(machine, ::Read<uint16_t>(body, 8)),
						offset < 0 ? L"-" : L"+", offset < 0 ? -(int64_t)offset : offset);
					break;
				}
				case S_LOCAL:
					sym.Name = name(6);
					sym.Details = type(u32(0));
					if (::Read<uint16_t>(body, 4) & 1)
						sym.Details += L", parameter";
					break;
				case S_FRAMEPROC:
					sym.Details = std::format(L"Frame 0x{:X} bytes, saved registers 0x{:X} bytes", u32(0), u32(12));
					break;
				case S_FRAMECOOKIE:
					sym.Details = std::format(L"[{}{:+}]", RegisterName(machine, ::Read<uint16_t>(body, 4)), (int32_t)u32(0));
					break;
				case S_CALLSITEINFO:
					sym.Details = std::format(L"Calls {}", type(u32(8)));
					sym.Target = Resolve(sym.Section, at, u32(0));
					break;
				case S_HEAPALLOCSITE:
					sym.Details = std::format(L"Allocates {}", type(u32(8)));
					sym.Target = Resolve(sym.Section, at, u32(0));
					break;
				case S_UNAMESPACE:
					sym.Name = name(0);
					break;
				case S_SECTION:
					sym.Name = name(16);
					sym.Details = std::format(L"{} bytes", u32(8));
					break;
				case S_COFFGROUP:
					sym.Name = name(14);
					sym.Details = std::format(L"{} bytes", u32(0));
					break;
			}
		}

		CoffObject const& m_Object;
		CodeViewInfo& m_Info;
		std::unordered_map<uint64_t, uint32_t> m_Relocations;	// (section, offset) -> symbol index
		TypeTable m_Types;
		Bytes m_Strings;
		std::unordered_map<uint32_t, std::wstring> m_Files;
	};
}

CodeViewInfo ReadCodeView(CoffObject const& obj) {
	CodeViewInfo info;
	Reader(obj, info).Read();
	return info;
}

std::wstring CodeViewInfo::SubsectionName(uint32_t type) {
	PCWSTR name = nullptr;
	switch (type & 0x7FFFFFFF) {
		case 0xF1: name = L"Symbols"; break;
		case 0xF2: name = L"Lines"; break;
		case 0xF3: name = L"String Table"; break;
		case 0xF4: name = L"File Checksums"; break;
		case 0xF5: name = L"Frame Data"; break;
		case 0xF6: name = L"Inlinee Lines"; break;
		case 0xF7: name = L"Cross-Scope Imports"; break;
		case 0xF8: name = L"Cross-Scope Exports"; break;
		case 0xF9: name = L"IL Lines"; break;
		case 0xFA: name = L"Function Token Map"; break;
		case 0xFB: name = L"Type Token Map"; break;
		case 0xFC: name = L"Merged Assembly Input"; break;
		case 0xFD: name = L"COFF Symbol RVAs"; break;
		case 0xFF: name = L"XFG Type Hashes"; break;
		case 0x100: name = L"XFG Virtual Hashes"; break;
	}
	auto text = name ? std::wstring(name) : std::format(L"0x{:X}", type & 0x7FFFFFFF);
	return (type & 0x80000000) ? text + L" (ignored)" : text;
}

std::wstring CodeViewInfo::ChecksumKindName(uint8_t kind) {
	switch (kind) {
		case 0: return L"None";
		case 1: return L"MD5";
		case 2: return L"SHA-1";
		case 3: return L"SHA-256";
	}
	return std::to_wstring(kind);
}

std::wstring CodeViewInfo::SymbolKindName(uint16_t kind) {
	static const std::unordered_map<uint16_t, PCWSTR> names = {
		{ 0x0006, L"S_END" }, { 0x1012, L"S_FRAMEPROC" }, { 0x1019, L"S_ANNOTATION" }, { 0x1101, L"S_OBJNAME" }, { 0x1102, L"S_THUNK32" },
		{ 0x1103, L"S_BLOCK32" }, { 0x1104, L"S_WITH32" }, { 0x1105, L"S_LABEL32" }, { 0x1106, L"S_REGISTER" }, { 0x1107, L"S_CONSTANT" },
		{ 0x1108, L"S_UDT" }, { 0x1109, L"S_COBOLUDT" }, { 0x110A, L"S_MANYREG" }, { 0x110B, L"S_BPREL32" }, { 0x110C, L"S_LDATA32" },
		{ 0x110D, L"S_GDATA32" }, { 0x110E, L"S_PUB32" }, { 0x110F, L"S_LPROC32" }, { 0x1110, L"S_GPROC32" }, { 0x1111, L"S_REGREL32" },
		{ 0x1112, L"S_LTHREAD32" }, { 0x1113, L"S_GTHREAD32" }, { 0x1116, L"S_COMPILE2" }, { 0x111C, L"S_LMANDATA" }, { 0x111D, L"S_GMANDATA" },
		{ 0x1124, L"S_UNAMESPACE" }, { 0x112C, L"S_TRAMPOLINE" }, { 0x1132, L"S_SEPCODE" }, { 0x1136, L"S_SECTION" }, { 0x1137, L"S_COFFGROUP" },
		{ 0x1138, L"S_EXPORT" }, { 0x1139, L"S_CALLSITEINFO" }, { 0x113A, L"S_FRAMECOOKIE" }, { 0x113C, L"S_COMPILE3" }, { 0x113D, L"S_ENVBLOCK" },
		{ 0x113E, L"S_LOCAL" }, { 0x113F, L"S_DEFRANGE" }, { 0x1140, L"S_DEFRANGE_SUBFIELD" }, { 0x1141, L"S_DEFRANGE_REGISTER" },
		{ 0x1142, L"S_DEFRANGE_FRAMEPOINTER_REL" }, { 0x1143, L"S_DEFRANGE_SUBFIELD_REGISTER" }, { 0x1144, L"S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE" },
		{ 0x1145, L"S_DEFRANGE_REGISTER_REL" }, { 0x1146, L"S_LPROC32_ID" }, { 0x1147, L"S_GPROC32_ID" }, { 0x114C, L"S_BUILDINFO" },
		{ 0x114D, L"S_INLINESITE" }, { 0x114E, L"S_INLINESITE_END" }, { 0x114F, L"S_PROC_ID_END" }, { 0x1153, L"S_FILESTATIC" },
		{ 0x1155, L"S_LPROC32_DPC" }, { 0x1156, L"S_LPROC32_DPC_ID" }, { 0x1159, L"S_ARMSWITCHTABLE" }, { 0x115A, L"S_CALLEES" },
		{ 0x115B, L"S_CALLERS" }, { 0x115C, L"S_POGODATA" }, { 0x115D, L"S_INLINESITE2" }, { 0x115E, L"S_HEAPALLOCSITE" },
		{ 0x115F, L"S_MOD_TYPEREF" }, { 0x1160, L"S_REF_MINIPDB" }, { 0x1161, L"S_PDBMAP" }, { 0x1167, L"S_FRAMEREG" },
		{ 0x1168, L"S_INLINEES" }, { 0x1169, L"S_HOTPATCHFUNC" },
	};
	auto it = names.find(kind);
	return it == names.end() ? std::format(L"0x{:04X}", kind) : it->second;
}

std::wstring CodeViewInfo::TypeKindName(uint16_t kind) {
	static const std::unordered_map<uint16_t, PCWSTR> names = {
		{ 0x000A, L"LF_VTSHAPE" }, { 0x000E, L"LF_LABEL" }, { 0x0014, L"LF_ENDPRECOMP" }, { 0x1001, L"LF_MODIFIER" }, { 0x1002, L"LF_POINTER" },
		{ 0x1008, L"LF_PROCEDURE" }, { 0x1009, L"LF_MFUNCTION" }, { 0x100A, L"LF_COBOL0" }, { 0x1201, L"LF_ARGLIST" }, { 0x1203, L"LF_FIELDLIST" },
		{ 0x1205, L"LF_BITFIELD" }, { 0x1206, L"LF_METHODLIST" }, { 0x1501, L"LF_TYPESERVER" }, { 0x1503, L"LF_ARRAY" }, { 0x1504, L"LF_CLASS" },
		{ 0x1505, L"LF_STRUCTURE" }, { 0x1506, L"LF_UNION" }, { 0x1507, L"LF_ENUM" }, { 0x1508, L"LF_DIMARRAY" }, { 0x1509, L"LF_PRECOMP" },
		{ 0x1515, L"LF_TYPESERVER2" }, { 0x1519, L"LF_INTERFACE" }, { 0x151D, L"LF_VFTABLE" }, { 0x1601, L"LF_FUNC_ID" }, { 0x1602, L"LF_MFUNC_ID" },
		{ 0x1603, L"LF_BUILDINFO" }, { 0x1604, L"LF_SUBSTR_LIST" }, { 0x1605, L"LF_STRING_ID" }, { 0x1606, L"LF_UDT_SRC_LINE" },
		{ 0x1607, L"LF_UDT_MOD_SRC_LINE" }, { 0x1608, L"LF_CLASS2" }, { 0x1609, L"LF_STRUCTURE2" }, { 0x160A, L"LF_UNION2" }, { 0x160B, L"LF_INTERFACE2" },
	};
	auto it = names.find(kind);
	return it == names.end() ? std::format(L"0x{:04X}", kind) : it->second;
}
