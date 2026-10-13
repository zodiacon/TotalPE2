#pragma once

#include <PEFile.h>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct ClrStream {
	std::string Name;
	uint32_t Offset;	// relative to metadata root
	uint32_t Size;
};

struct ClrTable {
	int Id;
	uint32_t Rows;
};

struct ClrAssemblyInfo {
	std::wstring Name;
	std::wstring Culture;
	uint16_t Major{}, Minor{}, Build{}, Revision{};
	uint32_t Flags{};
};

// The metadata tables that the types and members are read from (ECMA-335 II.22)
namespace ClrTableId {
	enum : int {
		Module = 0x00, TypeRef = 0x01, TypeDef = 0x02, FieldPtr = 0x03, Field = 0x04, MethodPtr = 0x05, MethodDef = 0x06,
		ParamPtr = 0x07, Param = 0x08, InterfaceImpl = 0x09, MemberRef = 0x0A, Constant = 0x0B, CustomAttribute = 0x0C,
		StandAloneSig = 0x11, EventMap = 0x12, EventPtr = 0x13, Event = 0x14, PropertyMap = 0x15, PropertyPtr = 0x16,
		Property = 0x17, MethodSemantics = 0x18, ModuleRef = 0x1A,
		TypeSpec = 0x1B, Assembly = 0x20, AssemblyRef = 0x23, NestedClass = 0x29, GenericParam = 0x2A, MethodSpec = 0x2B,
		UserString = 0x70,	// not a table: the tokens of ldstr are offsets in the #US heap
	};
}

// A type that the file defines. Rows are 1-based, as in tokens; the ranges of fields and methods are [first, end).
struct ClrTypeDef {
	uint32_t Flags{};
	std::wstring Namespace, Name;
	uint32_t Extends{};		// a TypeDef, TypeRef or TypeSpec token; 0 for none (interfaces, System.Object)
	uint32_t FirstField{}, EndField{};
	uint32_t FirstMethod{}, EndMethod{};
	uint32_t Enclosing{};	// the TypeDef row of the type this one is nested in, 0 if it is not nested
};

struct ClrMethodDef {
	uint32_t Rva{};			// of the body: 0 for abstract, extern and runtime methods
	uint16_t ImplFlags{}, Flags{};
	std::wstring Name;
	uint32_t Signature{};	// in the #Blob heap
	uint32_t FirstParam{}, EndParam{};
	uint32_t Type{};		// the TypeDef row that defines it
};

struct ClrFieldDef {
	uint16_t Flags{};
	std::wstring Name;
	uint32_t Signature{};
	uint32_t Type{};
};

// A property, and the methods that are its accessors (MethodDef rows, 0 for none)
struct ClrPropertyDef {
	uint16_t Flags{};
	std::wstring Name;
	uint32_t Signature{};
	uint32_t Type{};
	uint32_t Getter{}, Setter{};
};

// An event, its type (a token) and its accessors
struct ClrEventDef {
	uint16_t Flags{};
	std::wstring Name;
	uint32_t EventType{};
	uint32_t Type{};
	uint32_t Adder{}, Remover{}, Raiser{};
};

// A method that is a function of a native DLL (ImplMap): the DLL, the function's name in it and the flags (CharSet...)
struct ClrPInvoke {
	std::wstring Module, EntryPoint;
	uint16_t Flags{};
};

// What a method is to a property or an event (MethodSemantics)
enum class ClrAccessor : uint8_t { None, Getter, Setter, Adder, Remover, Raiser, Other };

// A custom attribute: the constructor and its arguments, decoded, in IL and in C#
struct ClrAttributeArgument {
	std::wstring Name;		// of a named argument (a field or property of the attribute); empty for a constructor's
	bool Field{ false };	// a named argument that is a field
	std::wstring Type;		// in IL: "int32", "string", "type", the type of an enum...
	std::wstring Il, CSharp;	// the value: "int32(5)" / "5", "string('a')" / "\"a\"", "type(T)" / "typeof(T)"
};
struct ClrCustomAttribute {
	uint32_t Parent{};		// the token it is on
	uint32_t Constructor{};	// a MethodDef or MemberRef token
	uint32_t AttributeType{};	// the token of the attribute's type
	std::vector<ClrAttributeArgument> Arguments;	// those of the constructor, then the named ones
	bool Decoded{ false };	// false: the value is not what the constructor says (it is shown as bytes)
	std::vector<uint8_t> Value;
};

// What kind of value a type is, as the C# decompiler needs to know it (to write a condition, a literal...)
enum class ClrValueKind : uint8_t {
	Unknown, Void, Bool, Char, Integer, Float, String, Object, Struct, Enum, Pointer, ByRef, Generic,
};

struct ClrTypeInfo {
	std::wstring Name;	// in C#: "int", "List<string>", "ref int", "T"
	ClrValueKind Kind{ ClrValueKind::Unknown };
	uint32_t Token{};	// of a class, struct or enum (TypeDef, TypeRef or TypeSpec), 0 for the others
};

struct ClrSignatureInfo {
	bool HasThis{ false };
	bool Vararg{ false };
	uint32_t GenericCount{ 0 };
	ClrTypeInfo Return;
	std::vector<ClrTypeInfo> Params;
};

// A method or field that a token refers to, with what the C# decompiler needs: the type it is in, its name, its signature
struct ClrMemberInfo {
	uint32_t Token{};
	bool IsField{ false };
	bool Ok{ false };
	std::wstring Name;
	ClrTypeInfo Owner;		// the type it is a member of (an instance of a generic type: with its arguments)
	ClrSignatureInfo Signature;	// of a method
	ClrTypeInfo FieldType;	// of a field
	std::vector<std::wstring> GenericArgs;	// of an instance of a generic method
	uint32_t Definition{};	// the MethodDef or Field of this file it is, 0 if it is elsewhere
};

// The names (and kinds) of the generic parameters that !0 and !!0 stand for
struct ClrGenericContext {
	std::vector<std::wstring> const* TypeNames{ nullptr };
	std::vector<std::wstring> const* MethodNames{ nullptr };
	std::vector<ClrValueKind> const* TypeKinds{ nullptr };
	std::vector<ClrValueKind> const* MethodKinds{ nullptr };
};

// Parses the CLR (COM descriptor) header and the metadata: version, streams, table row counts, module name, assembly
// identity and references, and the types and members, whose names and signatures it turns into text (in the style of
// ildasm). The metadata stays in the file: the PEFile must outlive this object.
// C# literals: a string, a character ('a', '\n'), a real number ("1.5", "2.0", "1.5f", "double.NaN")
std::wstring CSharpQuote(std::wstring_view text);
std::wstring CSharpChar(wchar_t c);
std::wstring CSharpReal(double value, bool single);

class ClrMetadata {
public:
	bool Parse(PEFile const& pe);

	bool HasHeader() const { return m_HasHeader; }
	bool HasMetadata() const { return m_HasMetadata; }
	IMAGE_COR20_HEADER const& Header() const { return m_Header; }
	std::wstring const& Version() const { return m_Version; }
	std::vector<ClrStream> const& Streams() const { return m_Streams; }
	std::vector<ClrTable> const& Tables() const { return m_Tables; }
	bool HasAssembly() const { return m_HasAssembly; }
	ClrAssemblyInfo const& Assembly() const { return m_Assembly; }
	std::wstring const& ModuleName() const { return m_ModuleName; }
	std::vector<ClrAssemblyInfo> const& References() const { return m_Refs; }

	static PCWSTR TableName(int id);

	// The types and members, indexed by row - 1
	std::vector<ClrTypeDef> const& Types() const { return m_Types; }
	std::vector<ClrMethodDef> const& Methods() const { return m_Methods; }
	std::vector<ClrFieldDef> const& Fields() const { return m_Fields; }
	uint32_t Rows(int table) const { return table >= 0 && table < 64 ? m_Rows[table] : 0; }
	// The method of the entry point (a MethodDef row), 0 if there is none
	uint32_t EntryPointMethod() const;
	// The methods and fields of a type (rows)
	std::vector<uint32_t> MethodsOf(uint32_t type) const;
	std::vector<uint32_t> FieldsOf(uint32_t type) const;
	// The types nested in a type (TypeDef rows)
	std::vector<uint32_t> NestedTypes(uint32_t type) const;
	// The interfaces a type implements (tokens)
	std::vector<uint32_t> Interfaces(uint32_t type) const;

	// The name of a type: with its namespace, the types it is nested in ("Outer/Inner"), and for a type of another
	// assembly the assembly ("[mscorlib]System.Object"). A TypeSpec (an instance of a generic type, an array...) is decoded.
	std::wstring TypeName(uint32_t token) const;
	// A method or field that the code refers to: "void [mscorlib]System.Console::WriteLine(string)", "int32 Program::count"
	std::wstring MemberName(uint32_t token) const;
	// Any token that an instruction refers to: a type, a member, a string (quoted) or a signature
	std::wstring TokenText(uint32_t token) const;
	// A string of the #US heap (the token of ldstr), without quotes; none if the token is not in the heap
	std::optional<std::wstring> UserString(uint32_t token) const;
	// The RVA of the characters of a user string; 0 if there is none
	uint32_t UserStringRva(uint32_t token) const;

	// The definition in this file of what a token refers to (a TypeDef, MethodDef or Field token), through the references
	// to the members of its generic types and the instances of its generic methods; 0 if it is defined elsewhere
	uint32_t Definition(uint32_t token) const;

	// The declarations, as ildasm shows them:
	// ".class public auto ansi beforefieldinit Ns.Name extends [mscorlib]System.Object"
	std::wstring TypeDeclaration(uint32_t type) const;
	// ".method public hidebysig static void Main(string[] args) cil managed"
	std::wstring MethodDeclaration(uint32_t method) const;
	// ".field private static int32 count"
	std::wstring FieldDeclaration(uint32_t field) const;
	// "void Main(string[] args)": the name, parameters and return type of a method, without the flags
	std::wstring MethodSignature(uint32_t method) const;
	// "int32 count", "valuetype Color Green = int32(5)": the type, name and value of a field, without the flags
	std::wstring FieldSignature(uint32_t field) const;
	// The names of the generic parameters of a type or method (by its token)
	std::vector<std::wstring> GenericParamNames(uint32_t owner) const;
	// the name of a parameter (1-based, as ldarg counts them without 'this'), empty if it has none
	std::wstring ParamName(uint32_t method, uint32_t sequence) const;
	// The types of the locals of a method body (the token of its LocalVarSig); empty if there are none
	std::vector<std::wstring> LocalTypes(uint32_t signatureToken, uint32_t method) const;
	// the name of a type the file defines: "Ns.Outer/Inner"
	std::wstring FullName(uint32_t type) const;
	// The value of a constant field (literal): "int32(5)", "\"text\"", "nullref"; empty if it has none
	std::wstring ConstantText(uint32_t token) const;
	// "class", "interface", "struct", "enum" or "delegate"
	std::wstring TypeKind(uint32_t type) const;

	// The properties and events (rows, 1-based) and their accessors
	std::vector<ClrPropertyDef> const& Properties() const { return m_Properties; }
	std::vector<ClrEventDef> const& Events() const { return m_Events; }
	std::vector<uint32_t> PropertiesOf(uint32_t type) const;
	std::vector<uint32_t> EventsOf(uint32_t type) const;
	// What a method is an accessor of: the token of the property or event, and which accessor (None if it is not one)
	std::pair<uint32_t, ClrAccessor> AccessorOf(uint32_t method) const;
	// ".property instance int32 Count()", ".event [mscorlib]System.EventHandler Changed"
	std::wstring PropertyDeclaration(uint32_t property) const;
	std::wstring EventDeclaration(uint32_t event) const;
	// "int32 Count", "int32 Item(int32)", "[mscorlib]System.EventHandler Changed": for the tree
	std::wstring PropertySignature(uint32_t property) const;
	std::wstring EventSignature(uint32_t event) const;

	// The custom attributes on what a token is (a type, method, field, parameter, property, event, the assembly...)
	std::vector<ClrCustomAttribute> CustomAttributes(uint32_t token) const;
	// ".custom instance void [mscorlib]System.ObsoleteAttribute::.ctor(string) = { string('old') }"
	std::wstring AttributeIl(ClrCustomAttribute const& attribute) const;
	// "Obsolete(\"old\")" (without the brackets)
	std::wstring AttributeCSharp(ClrCustomAttribute const& attribute, ClrGenericContext const& context = {}) const;
	// The token of a parameter (Param), by its sequence (0: the return value); 0 if it has no row
	uint32_t ParamToken(uint32_t method, uint32_t sequence) const;
	// The flags of a parameter (in = 1, out = 2, optional = 0x10)
	uint16_t ParamFlags(uint32_t method, uint32_t sequence) const;
	// The native function a method is (P/Invoke), if it is one
	std::optional<ClrPInvoke> PInvoke(uint32_t method) const;

	// C#: the names of types, the signatures and the members that code refers to
	ClrGenericContext ContextOf(uint32_t method) const;	// of a method of the file (row)
	ClrGenericContext ContextOfType(uint32_t type) const;
	ClrTypeInfo CSharpType(uint32_t token, ClrGenericContext const& context = {}) const;
	ClrMemberInfo MemberInfo(uint32_t token, ClrGenericContext const& context = {}) const;
	std::vector<ClrTypeInfo> CSharpLocalTypes(uint32_t signatureToken, uint32_t method) const;
	ClrTypeInfo CSharpPropertyType(uint32_t property, std::vector<ClrTypeInfo>* parameters = nullptr) const;
	// The value of a constant (a field, parameter or property) as C# writes it: "5", "\"text\"", "null"; empty if it has none
	std::wstring ConstantCSharp(uint32_t token) const;
	// The integer value of a constant, if it is one
	std::optional<int64_t> ConstantInteger(uint32_t token) const;
	// The type of an enum's values (its value__ field), "int" if it is not known
	ClrTypeInfo EnumUnderlyingType(uint32_t type) const;
	// C#'s name of a type of the file, with the types it is nested in ("Outer.Inner"), without the namespace or arity
	std::wstring CSharpTypeDefName(uint32_t type) const;

private:
	bool ParseTables(std::span<const std::byte> md, ClrStream const& tables);
	friend struct ClrRowReader;
	friend struct ClrSignature;
	friend struct ClrAttributeReader;
	ClrMemberInfo MemberInfoOf(uint32_t token, ClrGenericContext const& context, std::vector<ClrTypeInfo> const* methodArgs) const;

	std::wstring String(uint32_t index) const;
	std::span<const std::byte> Blob(uint32_t index) const;
	uint32_t SimpleSize(int table) const;
	uint32_t CodedSize(int coded) const;
	uint32_t CodedToken(int coded, uint32_t value) const;
	size_t RowOffset(int table, uint32_t row) const;	// in m_TableData; row is 1-based
	std::wstring TypeRefName(uint32_t row, int depth) const;
	std::wstring TypeDefName(uint32_t row, bool withNamespace = true) const;
	// a signature of the #Blob heap as text: of a method ("void (int32, string)"), a field, a type spec
	std::wstring MethodRefText(uint32_t parent, std::wstring const& name, std::span<const std::byte> sig, std::wstring const& generic = {}) const;
	std::vector<std::wstring> const* GenericParams(uint32_t owner) const;
	uint32_t Indirect(int ptrTable, uint32_t row) const;

	IMAGE_COR20_HEADER m_Header{};
	bool m_HasHeader{ false }, m_HasMetadata{ false }, m_HasAssembly{ false };
	std::wstring m_Version, m_ModuleName;
	std::vector<ClrStream> m_Streams;
	std::vector<ClrTable> m_Tables;
	ClrAssemblyInfo m_Assembly;
	std::vector<ClrAssemblyInfo> m_Refs;

	// the heaps and the tables
	PEFile const* m_PE{ nullptr };
	uint32_t m_MetadataRva{ 0 };
	std::span<const std::byte> m_StringHeap, m_BlobHeap, m_UserStrings, m_TableData;
	uint32_t m_UserStringsOffset{ 0 };	// from the start of the metadata
	uint32_t m_Rows[64]{};
	uint32_t m_RowSize[64]{};
	size_t m_Start[64]{};
	uint32_t m_StrIdx{ 2 }, m_GuidIdx{ 2 }, m_BlobIdx{ 2 };

	std::vector<ClrTypeDef> m_Types;
	std::vector<ClrMethodDef> m_Methods;
	std::vector<ClrFieldDef> m_Fields;
	std::vector<ClrPropertyDef> m_Properties;
	std::vector<ClrEventDef> m_Events;
	std::vector<std::vector<uint32_t>> m_TypeProperties, m_TypeEvents;	// by the TypeDef row
	std::unordered_map<uint32_t, std::pair<uint32_t, ClrAccessor>> m_Accessors;	// by the MethodDef row
	std::vector<std::pair<uint32_t, uint32_t>> m_Attributes;	// the token an attribute is on, the CustomAttribute row (sorted)
	std::unordered_map<uint32_t, std::vector<std::wstring>> m_GenericParams;	// by the token of the type or method
	std::vector<std::pair<uint32_t, uint32_t>> m_Interfaces;	// type row, interface token (sorted by the row)
	std::unordered_map<uint32_t, uint32_t> m_Constants;	// the Constant row of a field, parameter or property (by its token)
};
