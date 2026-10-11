#include "pch.h"
#include "TypeExporter.h"
#include "Interfaces.h"
#include "SaveData.h"
#include "resource.h"
#include <set>
#include <unordered_set>

namespace {
	// A member of a struct as far as its layout goes: base classes and the vtable pointer are members too (but not in C++)
	struct Field {
		DiaSymbol Type;
		std::wstring Name;
		uint32_t Offset;
		uint32_t Size;
		bool BitField{ false };
		uint32_t BitPosition{ 0 }, Bits{ 0 };
		bool VTable{ false };
		bool Base{ false };
		AccessMode Access{ AccessMode::Public };

		uint32_t End() const {
			return Offset + Size;
		}
	};

	// Types without a name (in a member) are written in place, or (in C#) get one
	bool IsUnnamed(DiaSymbol const& type) {
		auto name = type.Name();
		return name.empty() || name[0] == L'<' || name.find(L"<unnamed") != std::wstring::npos
			|| name.find(L"__unnamed") != std::wstring::npos || name.find(L"<anonymous") != std::wstring::npos;
	}

	// A name the language accepts: C++ names have scopes and template arguments, and a name may be a keyword
	std::wstring Identifier(std::wstring const& name, TypeFormat format) {
		static const std::unordered_set<std::wstring> cKeywords{
			L"auto", L"break", L"case", L"char", L"const", L"continue", L"default", L"do", L"double", L"else", L"enum",
			L"extern", L"float", L"for", L"goto", L"if", L"inline", L"int", L"long", L"register", L"restrict", L"return",
			L"short", L"signed", L"sizeof", L"static", L"struct", L"switch", L"typedef", L"union", L"unsigned", L"void",
			L"volatile", L"while", L"_Bool",
		};
		static const std::unordered_set<std::wstring> cppKeywords{
			L"alignas", L"alignof", L"and", L"asm", L"bool", L"catch", L"char8_t", L"char16_t", L"char32_t", L"class",
			L"concept", L"consteval", L"constexpr", L"constinit", L"co_await", L"co_return", L"co_yield", L"decltype",
			L"delete", L"explicit", L"export", L"false", L"friend", L"mutable", L"namespace", L"new", L"noexcept", L"not",
			L"nullptr", L"operator", L"or", L"private", L"protected", L"public", L"requires", L"static_assert", L"template",
			L"this", L"throw", L"true", L"try", L"typeid", L"typename", L"using", L"virtual", L"wchar_t", L"xor",
		};
		static const std::unordered_set<std::wstring> csKeywords{
			L"abstract", L"as", L"base", L"bool", L"break", L"byte", L"case", L"catch", L"char", L"checked", L"class",
			L"const", L"continue", L"decimal", L"default", L"delegate", L"do", L"double", L"else", L"enum", L"event",
			L"explicit", L"extern", L"false", L"finally", L"fixed", L"float", L"for", L"foreach", L"goto", L"if",
			L"implicit", L"in", L"int", L"interface", L"internal", L"is", L"lock", L"long", L"namespace", L"new", L"null",
			L"object", L"operator", L"out", L"override", L"params", L"private", L"protected", L"public", L"readonly",
			L"ref", L"return", L"sbyte", L"sealed", L"short", L"sizeof", L"stackalloc", L"static", L"string", L"struct",
			L"switch", L"this", L"throw", L"true", L"try", L"typeof", L"uint", L"ulong", L"unchecked", L"unsafe",
			L"ushort", L"using", L"virtual", L"void", L"volatile", L"while",
		};

		std::wstring result;
		for (auto ch : name)
			result += iswalnum(ch) || ch == L'_' ? ch : L'_';
		if (result.empty() || iswdigit(result[0]))
			result.insert(0, 1, L'_');

		if (format == TypeFormat::CSharp)
			return csKeywords.contains(result) ? L"@" + result : result;
		if (cKeywords.contains(result) || (format == TypeFormat::Cpp && cppKeywords.contains(result)))
			result += L'_';
		return result;
	}

	// The value of an enum constant as the bits there are
	uint64_t VariantBits(VARIANT const& value) {
		switch (value.vt) {
			case VT_I1: return (uint64_t)(int64_t)value.cVal;
			case VT_I2: return (uint64_t)(int64_t)value.iVal;
			case VT_I4: case VT_INT: return (uint64_t)(int64_t)value.lVal;
			case VT_I8: return (uint64_t)value.llVal;
			case VT_UI1: return value.bVal;
			case VT_UI2: return value.uiVal;
			case VT_UI4: case VT_UINT: return value.ulVal;
			case VT_UI8: return value.ullVal;
		}
		return 0;
	}

	// An enum constant in a type of the size (signed or not): fits the underlying type of a C++ or C# enum
	std::wstring EnumValue(VARIANT const& value, uint64_t size, bool isSigned) {
		auto bits = VariantBits(value);
		if (size == 0 || size > 8)
			size = 8;
		if (!isSigned) {
			if (size < 8)
				bits &= (1ULL << (size * 8)) - 1;
			return std::format(L"0x{:X}", bits);
		}
		auto shift = 64 - (int)size * 8;
		return std::format(L"{}", (int64_t)(bits << shift) >> shift);
	}

	// The alignment of a type as the compiler gives it (a packed struct aside: taken as 1)
	uint32_t Alignment(DiaSymbol const& type) {
		if (!type)
			return 1;
		switch (type.Tag()) {
			case SymbolTag::ArrayType:
				return Alignment(type.Type());
			case SymbolTag::UDT:
			{
				if (type.IsPacked())
					return 1;
				uint32_t align = 1;
				for (auto const& child : type.FindChildren()) {
					switch (child.Tag()) {
						case SymbolTag::VTable:
							align = std::max(align, (uint32_t)(child.Type() ? child.Type().Length() : sizeof(void*)));
							break;
						case SymbolTag::BaseClass:
							align = std::max(align, Alignment(child.Type() ? child.Type() : child));
							break;
						case SymbolTag::Data:
							if (child.Location() == LocationKind::ThisRel || child.Location() == LocationKind::BitField)
								align = std::max(align, Alignment(child.Type()));
							break;
					}
				}
				return align;
			}
		}
		// base types, enums, pointers
		auto size = (uint32_t)type.Length();
		return size == 0 ? 1 : std::min(size, 8u);
	}

	bool IsSignedBase(DiaSymbol const& type) {
		if (!type)
			return true;
		switch (type.Simple()) {
			case SimpleType::Int: case SimpleType::Int4B: case SimpleType::Char: case SimpleType::Hresult:
				return true;
		}
		return false;
	}

	std::vector<Field> Fields(DiaSymbol const& udt, TypeFormat format) {
		std::vector<Field> fields;
		for (auto const& child : udt.FindChildren()) {
			switch (child.Tag()) {
				case SymbolTag::VTable:
				{
					if (format == TypeFormat::Cpp)
						break;	// virtual functions make it
					auto type = child.Type();
					fields.push_back({ DiaSymbol::Empty, L"__vftable", (uint32_t)child.Offset(), type ? (uint32_t)type.Length() : (uint32_t)sizeof(void*) });
					fields.back().VTable = true;
					break;
				}

				case SymbolTag::BaseClass:
					if (format != TypeFormat::Cpp && !child.IsVirtual()) {
						auto base = child.Type() ? child.Type() : child;
						fields.push_back({ base, L"__base_" + Identifier(base.Name(), format), (uint32_t)child.Offset(), (uint32_t)base.Length() });
						fields.back().Base = true;
					}
					break;

				case SymbolTag::Data:
				{
					auto location = child.Location();
					if (location != LocationKind::ThisRel && location != LocationKind::BitField)
						break;	// static members are not part of the layout
					auto type = child.Type();
					Field f{ type, Identifier(child.Name(), format), (uint32_t)child.Offset(), type ? (uint32_t)type.Length() : 0 };
					f.Access = child.Access();
					if (location == LocationKind::BitField) {
						f.BitField = true;
						f.BitPosition = child.BitPosition();
						f.Bits = (uint32_t)child.Length();
					}
					fields.push_back(std::move(f));
					break;
				}
			}
		}
		return fields;
	}

	//
	// C and C++: the types a definition contains come first, the ones it points to are declared
	//
	class CWriter {
	public:
		explicit CWriter(TypeFormat format) : m_Format(format), m_Cpp(format == TypeFormat::Cpp) {}
		std::wstring Write(DiaSymbol const& type);

	private:
		void Define(DiaSymbol const& type);
		std::wstring Declare(DiaSymbol const& type, std::wstring inner, int indent, bool byValue);
		std::wstring UdtBody(DiaSymbol const& udt, int indent, std::vector<Field>* fields = nullptr);
		std::wstring EnumBody(DiaSymbol const& type, int indent);
		std::wstring VirtualFunctions(DiaSymbol const& udt, std::wstring const& className, bool needVTable);
		void Struct(std::vector<Field> const& fields, size_t begin, size_t end, int indent, std::wstring& out);
		void Union(std::vector<Field> const& fields, size_t begin, size_t end, uint32_t start, int indent, std::wstring& out);
		void Member(Field const& field, int indent, std::wstring& out);
		std::wstring Keyword(DiaSymbol const& type) const;
		std::wstring BaseTypeName(DiaSymbol const& type) const;
		std::wstring Name(DiaSymbol const& type) const {
			return Identifier(type.Name(), m_Format);
		}

		static bool IsRestart(std::vector<Field> const& fields, size_t index, uint32_t start);
		static std::wstring Indent(int indent) {
			return std::wstring(indent * 4, L' ');
		}

		TypeFormat m_Format;
		bool m_Cpp;
		std::wstring m_Definitions;
		std::set<std::wstring> m_Defined, m_InProgress;
		std::vector<std::wstring> m_Forward;	// "struct X", in the order they are met
		AccessMode m_Access{ AccessMode::Public };	// of the members of the C++ class being written
	};

	std::wstring CWriter::Keyword(DiaSymbol const& type) const {
		if (type.Tag() == SymbolTag::Enum)
			return L"enum";
		switch (type.UdtKind()) {
			case UdtType::Union: return L"union";
			case UdtType::Class: return m_Cpp ? L"class" : L"struct";	// a class is a struct in C
		}
		return L"struct";
	}

	std::wstring CWriter::BaseTypeName(DiaSymbol const& type) const {
		auto size = type.Length();
		switch (type.Simple()) {
			case SimpleType::Void: return L"void";
			case SimpleType::Char: case SimpleType::Char8: return L"char";
			case SimpleType::WChar: return L"wchar_t";
			case SimpleType::Char16: return m_Cpp ? L"char16_t" : L"uint16_t";
			case SimpleType::Char32: return m_Cpp ? L"char32_t" : L"uint32_t";
			case SimpleType::Bool: return m_Cpp ? L"bool" : L"_Bool";
			case SimpleType::Int4B: return L"long";
			case SimpleType::UInt4B: return L"unsigned long";
			case SimpleType::Hresult: return L"int32_t";
			case SimpleType::Float:
				return size == 4 ? L"float" : size == 8 ? L"double" : L"long double";
			case SimpleType::Int:
				switch (size) {
					case 1: return L"int8_t";
					case 2: return L"int16_t";
					case 4: return L"int32_t";
				}
				return L"int64_t";
		}
		// unsigned, and anything else of its size
		switch (size) {
			case 1: return L"uint8_t";
			case 2: return L"uint16_t";
			case 4: return L"uint32_t";
		}
		return L"uint64_t";
	}

	// The declaration of 'inner' (a name, or what is built around it: pointers, arrays) as of the type.
	// A type that is contained (not pointed to) is defined first.
	std::wstring CWriter::Declare(DiaSymbol const& type, std::wstring inner, int indent, bool byValue) {
		auto withInner = [&](std::wstring text) {
			return inner.empty() ? text : text + L" " + inner;
		};
		if (!type)
			return withInner(L"void");
		auto cv = std::wstring(type.IsConst() ? L"const " : L"") + (type.IsVolatile() ? L"volatile " : L"");

		switch (type.Tag()) {
			case SymbolTag::PointerType:
			{
				auto target = type.Type();
				inner = L"*" + std::wstring(type.IsConst() ? L" const " : L"") + inner;
				if (target && (target.Tag() == SymbolTag::ArrayType || target.Tag() == SymbolTag::FunctionType))
					inner = L"(" + inner + L")";
				return Declare(target, inner, indent, false);
			}

			case SymbolTag::ArrayType:
				return Declare(type.Type(), inner + std::format(L"[{}]", type.Count()), indent, byValue);

			case SymbolTag::FunctionType:
			{
				std::wstring args;
				for (auto const& arg : type.FindChildren(nullptr, SymbolTag::FunctionArgType)) {
					if (!args.empty())
						args += L", ";
					args += Declare(arg.Type(), L"", indent, false);
				}
				return Declare(type.Type(), inner + L"(" + (args.empty() ? L"void" : args) + L")", indent, false);
			}

			case SymbolTag::BaseType:
				return withInner(cv + BaseTypeName(type));

			case SymbolTag::UDT:
			case SymbolTag::Enum:
			{
				auto keyword = Keyword(type);
				if (IsUnnamed(type)) {
					auto body = type.Tag() == SymbolTag::Enum ? EnumBody(type, indent + 1) : UdtBody(type, indent + 1);
					return withInner(cv + keyword + L" {\n" + body + Indent(indent) + L"}");
				}
				auto name = Name(type);
				// an enum cannot be declared ahead in C: it is always defined
				if (byValue || type.Tag() == SymbolTag::Enum)
					Define(type);
				else if (!m_Defined.contains(keyword + name)) {
					auto decl = keyword + L" " + name;
					if (std::ranges::find(m_Forward, decl) == m_Forward.end())
						m_Forward.push_back(decl);
				}
				// C++ needs no keyword: the type is defined or declared ahead
				return withInner(cv + (m_Cpp ? name : keyword + L" " + name));
			}

			case SymbolTag::Typedef:
				return withInner(cv + Name(type));
		}
		return withInner(L"void");
	}

	// A member that starts again at the start of a group starts another arm of a union; a bit field that follows
	// another in the same storage unit does not
	bool CWriter::IsRestart(std::vector<Field> const& fields, size_t index, uint32_t start) {
		auto& f = fields[index];
		if (f.Offset != start)
			return false;
		auto& prev = fields[index - 1];
		return !(f.BitField && prev.BitField && prev.Offset == f.Offset && f.BitPosition > prev.BitPosition);
	}

	void CWriter::Member(Field const& field, int indent, std::wstring& out) {
		// the access of the members of a C++ class (not of the members of unions and structs without names)
		if (m_Cpp && indent == 1 && field.Access != m_Access && field.Access != AccessMode::Unknown) {
			m_Access = field.Access;
			out += m_Access == AccessMode::Private ? L"private:\n" : m_Access == AccessMode::Protected ? L"protected:\n" : L"public:\n";
		}
		auto decl = field.VTable ? L"void* " + field.Name : Declare(field.Type, field.Name, indent, true);
		out += Indent(indent) + decl;
		if (field.BitField)
			out += std::format(L" : {}", field.Bits);
		out += std::format(L";\t// +0x{:X}\n", field.Offset);
	}

	// Members in sequence; members that overlap are unions without names
	void CWriter::Struct(std::vector<Field> const& fields, size_t begin, size_t end, int indent, std::wstring& out) {
		for (auto i = begin; i < end; ) {
			auto start = fields[i].Offset;
			size_t last = 0;
			for (auto k = i + 1; k < end; k++)
				if (IsRestart(fields, k, start))
					last = k;
			if (last == 0) {
				Member(fields[i], indent, out);
				i++;
				continue;
			}
			// the union takes the members up to its last arm, and those of the last arm (that are inside the union).
			// The size of a union is rounded up to its alignment: a member in that padding is in its last arm.
			uint32_t unionEnd = 0, align = 1;
			auto extend = [&](Field const& f) {
				unionEnd = std::max(unionEnd, f.End());
				align = std::max(align, f.VTable ? f.Size : Alignment(f.Type));
			};
			for (auto k = i; k <= last; k++)
				extend(fields[k]);
			auto alignedEnd = [&] {
				return start + (unionEnd - start + align - 1) / align * align;
			};
			auto j = last + 1;
			for (; j < end && (fields[j].Offset < alignedEnd() || IsRestart(fields, j, start)); j++)
				extend(fields[j]);
			if (m_Cpp && indent == 1 && m_Access != AccessMode::Public && fields[i].Access == AccessMode::Public) {
				m_Access = AccessMode::Public;
				out += L"public:\n";
			}
			out += Indent(indent) + L"union {\n";
			Union(fields, i, j, start, indent + 1, out);
			out += Indent(indent) + L"};\n";
			i = j;
		}
	}

	// The arms of a union: a member, or a struct without a name for several
	void CWriter::Union(std::vector<Field> const& fields, size_t begin, size_t end, uint32_t start, int indent, std::wstring& out) {
		auto arm = begin;
		for (auto k = begin + 1; k <= end; k++) {
			if (k < end && !IsRestart(fields, k, start))
				continue;
			if (k - arm == 1)
				Member(fields[arm], indent, out);
			else {
				out += Indent(indent) + L"struct {\n";
				Struct(fields, arm, k, indent + 1, out);
				out += Indent(indent) + L"};\n";
			}
			arm = k;
		}
	}

	std::wstring CWriter::UdtBody(DiaSymbol const& udt, int indent, std::vector<Field>* result) {
		auto fields = Fields(udt, m_Format);
		std::wstring out;
		if (!fields.empty()) {
			if (udt.UdtKind() == UdtType::Union)
				Union(fields, 0, fields.size(), fields[0].Offset, indent, out);
			else
				Struct(fields, 0, fields.size(), indent, out);
		}
		if (result)
			*result = std::move(fields);
		return out;
	}

	// The virtual functions of a C++ class (compiler generated ones aside): with them the class has its vtable pointer.
	// A class that has one but no virtual functions to show gets a virtual destructor.
	std::wstring CWriter::VirtualFunctions(DiaSymbol const& udt, std::wstring const& className, bool needVTable) {
		std::wstring out;
		for (auto const& f : udt.FindChildren(nullptr, SymbolTag::Function)) {
			if (!f.IsVirtual() || f.IsCompilerGenerated())
				continue;
			auto name = f.Name();
			auto type = f.Type();
			std::wstring decl;
			if (name.starts_with(L'~'))
				decl = L"~" + className + L"()";
			else {
				std::wstring args;
				if (type) {
					for (auto const& arg : type.FindChildren(nullptr, SymbolTag::FunctionArgType)) {
						if (!args.empty())
							args += L", ";
						args += Declare(arg.Type(), L"", 1, false);
					}
				}
				if (!name.starts_with(L"operator"))
					name = Identifier(name, m_Format);
				decl = Declare(type ? type.Type() : DiaSymbol::Empty, name + L"(" + args + L")", 1, false);
			}
			out += Indent(1) + L"virtual " + decl + (f.IsPureVirtual() ? L" = 0;\n" : L";\n");
		}
		if (out.empty() && needVTable)
			out = Indent(1) + L"virtual ~" + className + L"();\n";
		if (!out.empty() && m_Access != AccessMode::Public)
			out.insert(0, L"public:\n");
		return out;
	}

	std::wstring CWriter::EnumBody(DiaSymbol const& type, int indent) {
		auto underlying = type.Type();
		auto size = underlying ? underlying.Length() : 4;
		bool isSigned = IsSignedBase(underlying);
		std::wstring out;
		for (auto const& value : type.FindChildren(nullptr, SymbolTag::Data)) {
			auto v = value.Value();
			out += Indent(indent) + Identifier(value.Name(), m_Format) + L" = " + EnumValue(v, size, isSigned) + L",\n";
			::VariantClear(&v);
		}
		return out;
	}

	void CWriter::Define(DiaSymbol const& type) {
		auto keyword = Keyword(type);
		auto name = Name(type);
		auto key = keyword + name;
		if (m_Defined.contains(key) || m_InProgress.contains(key))
			return;
		m_InProgress.insert(key);
		auto access = m_Access;	// of the class that contains this one, if it is being written

		// the types the body contains are defined while it is written
		std::wstring head = keyword + L" " + name, body;
		std::vector<Field> fields;
		bool polymorphic = false, hasBases = false;
		if (type.Tag() == SymbolTag::Enum) {
			if (m_Cpp) {
				head = std::wstring(type.IsScoped() ? L"enum class " : L"enum ") + name;
				if (auto underlying = type.Type())
					head += L" : " + BaseTypeName(underlying);
			}
			body = EnumBody(type, 1);
		}
		else {
			if (m_Cpp) {
				// the bases come first: a base is contained
				std::wstring bases;
				for (auto const& base : type.FindChildren(nullptr, SymbolTag::BaseClass)) {
					auto baseType = base.Type() ? base.Type() : base;
					Define(baseType);
					auto access = base.Access() == AccessMode::Private ? L"private " : base.Access() == AccessMode::Protected ? L"protected " : L"public ";
					bases += (bases.empty() ? L" : " : L", ") + std::wstring(base.IsVirtual() ? L"virtual " : L"") + access + Name(baseType);
					hasBases = true;
				}
				head += bases;
				polymorphic = !type.FindChildren(nullptr, SymbolTag::VTable).empty();
				m_Access = type.UdtKind() == UdtType::Class ? AccessMode::Private : AccessMode::Public;
			}
			body = UdtBody(type, 1, &fields);
			if (m_Cpp)
				body += VirtualFunctions(type, name, polymorphic && !hasBases);
		}
		m_Definitions += std::format(L"// sizeof: 0x{:X}\n{} {{\n{}}};\n\n", type.Length(), head, body);

		if (type.Tag() == SymbolTag::UDT && type.Length()) {
			if (m_Cpp) {
				auto checks = std::format(L"static_assert(sizeof({}) == 0x{:X});\n", name, type.Length());
				// offsetof is for classes with a standard layout
				if (!hasBases && !polymorphic) {
					for (auto const& f : fields)
						if (!f.BitField)
							checks += std::format(L"static_assert(offsetof({}, {}) == 0x{:X});\n", name, f.Name, f.Offset);
				}
				m_Definitions += checks + L"\n";
			}
			else {
				m_Definitions += std::format(L"_Static_assert(sizeof({}) == 0x{:X}, \"{}\");\n\n", head, type.Length(), name);
			}
		}

		m_InProgress.erase(key);
		m_Defined.insert(key);
		m_Access = access;
	}

	std::wstring CWriter::Write(DiaSymbol const& type) {
		Define(type);

		std::wstring text = std::format(L"// {} {}: generated by TotalPE from the symbols\n\n", Keyword(type), type.Name());
		text += m_Cpp ? L"#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n" : L"#pragma once\n\n#include <stddef.h>\n#include <stdint.h>\n\n";
		bool forward = false;
		for (auto const& decl : m_Forward) {
			// declared ahead, unless it is defined after all (it was contained elsewhere). C++ needs the declaration
			// anyway: a pointer may come before the definition, and the type has no keyword there.
			auto space = decl.find(L' ');
			if (!m_Cpp && m_Defined.contains(decl.substr(0, space) + decl.substr(space + 1)))
				continue;
			text += decl + L";\n";
			forward = true;
		}
		if (forward)
			text += L"\n";
		return text + m_Definitions;
	}

	//
	// C#: explicit layout, so unions need no reconstruction; every type is defined (order does not matter)
	//
	class CSharpWriter {
	public:
		std::wstring Write(DiaSymbol const& type);

	private:
		void Define(DiaSymbol const& type, std::wstring const& name);
		std::wstring TypeName(DiaSymbol const& type, std::wstring const& unnamedName);
		std::wstring PointerTarget(DiaSymbol const& type);
		std::wstring Member(Field const& field, std::wstring const& owner);
		static PCWSTR BaseTypeName(DiaSymbol const& type);
		static std::wstring Name(DiaSymbol const& type) {
			return Identifier(type.Name(), TypeFormat::CSharp);
		}

		std::wstring m_Definitions;
		std::set<std::wstring> m_Defined;
	};

	PCWSTR CSharpWriter::BaseTypeName(DiaSymbol const& type) {
		auto size = type.Length();
		switch (type.Simple()) {
			case SimpleType::Void: return L"void";
			case SimpleType::Char: case SimpleType::Char8: return L"byte";
			case SimpleType::WChar: case SimpleType::Char16: return L"char";
			case SimpleType::Char32: return L"uint";
			case SimpleType::Bool: return L"byte";	// one byte; a C# bool is not blittable
			case SimpleType::Int4B: case SimpleType::Hresult: return L"int";
			case SimpleType::UInt4B: return L"uint";
			case SimpleType::Float:
				return size == 4 ? L"float" : L"double";
			case SimpleType::Int:
				switch (size) {
					case 1: return L"sbyte";
					case 2: return L"short";
					case 4: return L"int";
				}
				return L"long";
		}
		switch (size) {
			case 1: return L"byte";
			case 2: return L"ushort";
			case 4: return L"uint";
		}
		return L"ulong";
	}

	// What a pointer points to: a struct or enum (defined), a base type, or void (a function, an array, a type without a name)
	std::wstring CSharpWriter::PointerTarget(DiaSymbol const& type) {
		if (!type)
			return L"void";
		switch (type.Tag()) {
			case SymbolTag::BaseType: return BaseTypeName(type);
			case SymbolTag::PointerType: return PointerTarget(type.Type()) + L"*";
			case SymbolTag::UDT:
			case SymbolTag::Enum:
				if (IsUnnamed(type))
					return L"void";
				Define(type, Name(type));
				return Name(type);
		}
		return L"void";
	}

	// The type of a member that is not an array or a bit field. A type without a name is defined with the name given.
	std::wstring CSharpWriter::TypeName(DiaSymbol const& type, std::wstring const& unnamedName) {
		if (!type)
			return L"byte";
		switch (type.Tag()) {
			case SymbolTag::BaseType: return BaseTypeName(type);
			case SymbolTag::PointerType: return PointerTarget(type.Type()) + L"*";
			case SymbolTag::UDT:
			case SymbolTag::Enum:
			{
				auto name = IsUnnamed(type) ? unnamedName : Name(type);
				Define(type, name);
				return name;
			}
		}
		return L"byte";
	}

	std::wstring CSharpWriter::Member(Field const& field, std::wstring const& owner) {
		auto offset = std::format(L"    [FieldOffset(0x{:X})] public ", field.Offset);
		if (field.VTable)
			return offset + L"void* " + field.Name + L";\n";

		auto const& type = field.Type;
		if (type && type.Tag() == SymbolTag::ArrayType) {
			// a fixed buffer of the elements, if they are of a base type (or an enum: of its underlying type); of bytes if not
			uint64_t count = 1;
			auto element = type;
			for (; element && element.Tag() == SymbolTag::ArrayType; element = element.Type())
				count *= element.Count();
			auto underlying = element && element.Tag() == SymbolTag::Enum ? element.Type() : element;
			if (underlying && underlying.Tag() == SymbolTag::BaseType && underlying.Simple() != SimpleType::Void)
				return offset + std::format(L"fixed {} {}[{}];\n", BaseTypeName(underlying), field.Name, count);
			auto elementName = element ? element.Name() : std::wstring(L"?");
			return offset + std::format(L"fixed byte {}[0x{:X}];\t// {}[{}]\n", field.Name, field.Size, elementName, count);
		}
		auto unnamed = owner + L"_" + (field.Name.starts_with(L'@') ? field.Name.substr(1) : field.Name);
		return offset + TypeName(type, unnamed) + L" " + field.Name + L";\n";
	}

	void CSharpWriter::Define(DiaSymbol const& type, std::wstring const& name) {
		if (m_Defined.contains(name))
			return;
		m_Defined.insert(name);

		std::wstring text;
		if (type.Tag() == SymbolTag::Enum) {
			auto underlying = type.Type();
			std::wstring base = underlying ? BaseTypeName(underlying) : L"int";
			if (base == L"char" || base == L"float" || base == L"double" || base == L"void")
				base = L"int";
			bool isSigned = base == L"sbyte" || base == L"short" || base == L"int" || base == L"long";
			auto size = underlying ? underlying.Length() : 4;
			text = std::format(L"public enum {} : {} {{\n", name, base);
			for (auto const& value : type.FindChildren(nullptr, SymbolTag::Data)) {
				auto v = value.Value();
				text += L"    " + Identifier(value.Name(), TypeFormat::CSharp) + L" = " + EnumValue(v, size, isSigned) + L",\n";
				::VariantClear(&v);
			}
			text += L"}\n\n";
		}
		else if (type.Length() == 0) {
			// only pointed to (an opaque type): there is a name to point to
			text = std::format(L"// {}: no definition in the symbols\npublic struct {} {{ }}\n\n", type.Name(), name);
		}
		else {
			auto fields = Fields(type, TypeFormat::CSharp);
			std::wstring body;
			std::set<std::pair<uint32_t, uint32_t>> storage;	// of bit fields: offset and size
			for (auto const& f : fields) {
				if (!f.BitField) {
					body += Member(f, name);
					continue;
				}
				// a field of the storage unit (once), and a property for the bits
				auto storageType = BaseTypeName(f.Type);
				auto storageName = std::format(L"_bitfield_0x{:X}", f.Offset);
				if (storage.insert({ f.Offset, f.Size }).second)
					body += std::format(L"    [FieldOffset(0x{:X})] public {} {};\n", f.Offset, storageType, storageName);
				auto mask = f.Bits >= 64 ? ~0ULL : (1ULL << f.Bits) - 1;
				body += std::format(L"    public {0} {1} {{\n"
					L"        get => ({0})(((ulong){2} >> {3}) & 0x{4:X}UL);\n"
					L"        set => {2} = ({0})(((ulong){2} & ~(0x{4:X}UL << {3})) | (((ulong)value & 0x{4:X}UL) << {3}));\n"
					L"    }}\n",
					storageType, f.Name, storageName, f.BitPosition, mask);
			}
			text = std::format(L"// {} {}\n[StructLayout(LayoutKind.Explicit, Size = 0x{:X})]\npublic unsafe struct {} {{\n{}}}\n\n",
				type.UdtKind() == UdtType::Union ? L"union" : type.UdtKind() == UdtType::Class ? L"class" : L"struct",
				type.Name(), type.Length(), name, body);
		}
		m_Definitions += text;
	}

	std::wstring CSharpWriter::Write(DiaSymbol const& type) {
		auto name = Name(type);
		Define(type, name);
		return std::format(L"// {}: generated by TotalPE from the symbols\n\nusing System.Runtime.InteropServices;\n\n", type.Name())
			+ m_Definitions;
	}
}

DiaSymbol TypeExporter::ExportableType(DiaSymbol const& sym) {
	if (!sym)
		return DiaSymbol::Empty;
	auto type = sym.Tag() == SymbolTag::Typedef ? sym.Type() : sym;
	if (type && (type.Tag() == SymbolTag::UDT || type.Tag() == SymbolTag::Enum))
		return type;
	return DiaSymbol::Empty;
}

std::wstring TypeExporter::ToText(DiaSymbol const& type, TypeFormat format) {
	if (format == TypeFormat::CSharp) {
		CSharpWriter writer;
		return writer.Write(type);
	}
	CWriter writer(format);
	return writer.Write(type);
}

bool TypeExporter::Save(HWND hOwner, DiaSymbol const& type, TypeFormat format) {
	PCWSTR ext, filter, title;
	switch (format) {
		case TypeFormat::Cpp:
			ext = L"h";
			filter = L"C++ Header Files (*.h)\0*.h;*.hpp\0All Files\0*.*\0";
			title = L"Save C++ Definition";
			break;
		case TypeFormat::CSharp:
			ext = L"cs";
			filter = L"C# Files (*.cs)\0*.cs\0All Files\0*.*\0";
			title = L"Save C# Definition";
			break;
		default:
			ext = L"h";
			filter = L"C Header Files (*.h)\0*.h\0All Files\0*.*\0";
			title = L"Save C Definition";
			break;
	}
	auto path = AskSaveFile(hOwner, title, ext, ToFileName(type.Name().c_str()) + L"." + ext, filter);
	if (path.IsEmpty())
		return false;

	std::wstring text;
	{
		CWaitCursor wait;
		text = ToText(type, format);
	}
	// UTF-8, with Windows line breaks
	std::wstring crlf;
	for (auto ch : text) {
		if (ch == L'\n')
			crlf += L'\r';
		crlf += ch;
	}
	std::string utf8((PCSTR)CW2A(crlf.c_str(), CP_UTF8));
	if (!WriteFileData(path, utf8.data(), utf8.size())) {
		AtlMessageBox(hOwner, std::format(L"The file could not be saved (error {}).", ::GetLastError()).c_str(), IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}
	return true;
}

void TypeExporter::ShowMenu(IMainFrame* frame, HWND hOwner, DiaSymbol const& type, POINT const& pt) {
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_TYPE_SAVEASC, L"Save as &C Definition...");
	menu.AppendMenu(MF_STRING, ID_TYPE_SAVEASCPP, L"Save as C&++ Definition...");
	menu.AppendMenu(MF_STRING, ID_TYPE_SAVEASCS, L"Save as C&# Definition...");
	switch (frame->ShowContextMenu(menu, TPM_RETURNCMD, pt.x, pt.y, hOwner)) {
		case ID_TYPE_SAVEASC: Save(hOwner, type, TypeFormat::C); break;
		case ID_TYPE_SAVEASCPP: Save(hOwner, type, TypeFormat::Cpp); break;
		case ID_TYPE_SAVEASCS: Save(hOwner, type, TypeFormat::CSharp); break;
	}
}
