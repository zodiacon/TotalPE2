#pragma once

#include <GhidraDecompiler.h>
#include <ClrMetadata.h>
#include <PEFile.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

//
// The functions of Windows (their prototypes) and the types they use, from the Win32 metadata (Windows.Win32.winmd, which
// GetWin32Metadata.ps1 puts next to the executable), described as the decompiler takes them: by ids, of their own (the
// high bit is set: the ids of the symbols' types do not have it). There is one for each machine: the layouts of structs
// and the types of one machine differ from another's.
//
class Win32Api {
public:
	enum class Machine { X86, X64, Arm64 };
	static constexpr uint32_t IdBit = 0x80000000;
	static bool IsId(uint32_t id) {
		return (id & IdBit) != 0;
	}
	// the API of a machine; null if there is no metadata (it is read once, when it is first used)
	static Win32Api* Get(Machine machine);
	// where the metadata is looked for: next to the executable
	static std::wstring MetadataPath();

	// A function by its name, as a DLL exports it (of that DLL, if there are others of the name; of that DLL only, if
	// 'onlyOf'): the id of its type (a function) and the names of its parameters
	bool Function(std::string const& name, std::string const& module, uint32_t& type, std::vector<std::string>& names, bool onlyOf = false);
	bool Describe(uint32_t id, GhidraDecompiler::Type& type);
	// a type by its name (a struct, union, enum, typedef: "HANDLE", "SECURITY_ATTRIBUTES"); 0 if there is none
	uint32_t TypeByName(std::string const& name);
	// The methods of an interface by its name ("IPersistFile"), those of its bases first (IUnknown's), in the order of its
	// vtable: each one's name, the id of its type (a function: the interface pointer first, 0 if it is not known) and the
	// names of its parameters ("This" first); false if the metadata has no such interface
	struct Method {
		std::string Name;
		uint32_t Type{ 0 };
		std::vector<std::string> Parameters;
	};
	bool InterfaceMethods(std::string const& name, std::vector<Method>& methods);

	// What the metadata has, to look at: the functions (of the machine), the types (not those nested in others) and the
	// constants (the fields of the classes of the functions and constants: MAX_PATH, E_FAIL, FOLDERID_Desktop)
	struct Entry {
		enum class Kinds : uint8_t { Function, Enum, Struct, Union, Typedef, Callback, Interface, Constant };
		std::string Name;
		Kinds Kind;
		std::string Where;		// a function's DLL, a type's or constant's namespace (without "Windows.Win32.")
		std::string Namespace;	// all of it ("Windows.Win32.System.Threading"); a function's is that of its class
		uint32_t Row;			// MethodDef, TypeDef or Field
	};
	std::vector<Entry> const& Entries();
	static PCWSTR KindName(Entry::Kinds kind);
	// a function, type or constant by its name (null if there is none; a function or type before a constant of the name)
	Entry const* Find(std::string const& name);
	// The namespaces as a tree: the root (no name) > "Windows" > "Win32" > "System" > "Threading"; each with the entries
	// in it (indices into Entries(), by name) and the namespaces in it (by name)
	struct Namespace {
		std::string Name;		// the last part ("Threading")
		std::string FullName;	// "Windows.Win32.System.Threading"
		std::vector<Namespace> Children;
		std::vector<size_t> Entries;
	};
	Namespace const& Namespaces();
	// The C declaration of an entry; 'lines' gets, for each line, the type it refers to (empty for none)
	std::string Declaration(Entry const& entry, std::vector<std::string>* lines = nullptr);
	// the page of Microsoft's documentation of an entry (from its [Documentation] attribute); empty if it has none
	std::wstring DocumentationUrl(Entry const& entry);
	// a type as C writes it ("HANDLE", "WIN32_FIND_DATAW *", "wchar_t[260]")
	std::string TypeText(uint32_t id);

	// A constant of an enum (or of HRESULT, NTSTATUS) by its name: its type, its value, and the names of the value, the one
	// the decompiler shows first
	struct Constant {
		std::string Type;
		uint64_t Value;
		std::vector<std::string> Names;
	};
	std::optional<Constant> ConstantOf(std::string const& name);

	// A GUID the metadata names: an interface's (IID_IUnknown; with the interface, the id of its type), a class's
	// (CLSID_FileOpenDialog), a constant's (FOLDERID_Desktop); by its 16 bytes as they are in memory
	struct NamedGuid {
		std::string Name;
		uint32_t Interface{ 0 };
	};
	std::unordered_map<std::string, NamedGuid> const& Guids();
	// the type of a GUID (a struct of 16 bytes)
	uint32_t GuidType();
	// Of the names of a value, those shown (the user's choices; then some common ones: MB_ICONERROR rather than MB_ICONHAND):
	// for all machines. The enums described are described again.
	static void SetPreferredNames(std::vector<std::string> names);
	static std::vector<std::string> const& PreferredNames();

private:
	struct Data;
	Win32Api(Data& data, Machine machine);
	static Data* Shared();
	static std::unique_ptr<Win32Api>* Instances();
	// the place of a name among those of its value (lower first)
	static int NameRank(std::string const& name);
	static void OrderNames(std::vector<std::pair<std::string, uint64_t>>& values);

	uint32_t TypeId(ClrSigType const& type);
	uint32_t RowOf(uint32_t token);
	uint32_t ForMachine(uint32_t row);
	bool OfMachine(uint32_t token);
	uint32_t Made(std::string const& key, GhidraDecompiler::Type type);
	uint32_t Primitive(GhidraDecompiler::TypeKind kind, uint32_t size, bool isSigned, char const* name = nullptr);
	uint32_t Pointer(uint32_t target);
	bool DescribeRow(uint32_t row, GhidraDecompiler::Type& type);
	// the size and alignment of a type
	std::pair<uint32_t, uint32_t> Layout(uint32_t id, int depth = 0);
	// the vtable of an interface: a struct of pointers to its methods, those of its bases first ("this" first in each)
	uint32_t Vtable(uint32_t row);
	std::string Name(uint32_t row);
	// the type a line refers to: the first one of the metadata in a type
	std::string Referred(uint32_t id);
	// the declaration of a constant (a field of a class of the functions and constants)
	std::string ConstantDeclaration(Entry const& entry, std::vector<std::string>* lines);
	std::vector<Entry> m_Entries;
	std::unordered_map<std::string, size_t> m_EntryIndex;
	Namespace m_Namespaces;
	bool m_NamespacesBuilt{ false };

	Data& m_Data;
	Machine m_Machine;
	uint32_t m_Pointer;
	std::vector<GhidraDecompiler::Type> m_Made;	// the types made for signatures: pointers, arrays, functions, numbers
	std::map<std::string, uint32_t> m_MadeKeys;
	std::unordered_map<uint32_t, GhidraDecompiler::Type> m_Described;
	std::unordered_map<uint32_t, uint32_t> m_Alignment;	// of the structs and unions described
	std::set<uint32_t> m_Laying;	// the structs whose layout is being found (a struct does not hold itself)
	std::unordered_map<std::string, uint32_t> m_ConstantTypes;	// the enum (its id) each constant is of
	std::unordered_map<std::string, NamedGuid> m_Guids;
	bool m_GuidsRead{ false };
	static constexpr uint32_t MadeBit = 0x40000000;
};
