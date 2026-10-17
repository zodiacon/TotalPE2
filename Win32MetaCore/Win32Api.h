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
	// the WDK's (the kernel's functions and types), used with it if it is there
	static std::wstring WdkMetadataPath();

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
	// The entries that use a type (indices into Entries(), by name): the functions with a parameter or result of it (or a
	// pointer to it, an array of it), the structs and unions with a field of it (in a nested one too), the callbacks and the
	// interfaces with a method that takes or returns it, the interfaces derived from it, the typedefs of it and the
	// constants of it. Empty for a function or a constant. (All of them are found when the first is asked for.)
	std::vector<size_t> const& UsedBy(size_t entry);

	// A constant (its entry), a value of an enum (its name and the enum's entry), a GUID's name (IID_IUnknown: the entry of
	// the interface, class or constant it is of)
	struct NamedValue {
		std::string Name;
		size_t Entry;
	};
	// Those of a value: as the metadata has it, or as an integer of 32, 16 or 8 bits (0xFFFFFFFF is -1 of an int, and -1
	// is 0xFFFFFFFF of a uint); by name
	std::vector<NamedValue> FindValue(uint64_t value);
	// What a GUID names (an interface, a class, a constant)
	std::optional<NamedValue> FindGuid(GUID const& guid);

	// The layout of a struct or union: its fields, and those of the structs and unions in them (an array's: of its first
	// element), each with its offset from the start and its size; the bit fields of a field; the padding between fields and
	// at the end. Depth is how far a field is in (0: the struct's own); Path names it from the struct ("Anonymous.dwOemId").
	struct LayoutItem {
		enum class Kinds : uint8_t { Field, Padding, Bitfield };
		Kinds Kind;
		int Depth;
		std::string Path;
		std::string Type;		// as C writes it (of an array: of its elements)
		std::string Name;		// with an array's count ("szCSDVersion[128]")
		uint32_t Offset;
		uint32_t Size;
		std::string Referred;	// the type it refers to (a field's)
		std::string Note;		// of a bit field: its bits; of an array's element: which one
	};
	struct StructLayout {
		uint32_t Size{ 0 }, Alignment{ 1 };
		std::vector<LayoutItem> Items;
	};
	// none if it is not a struct or union
	std::optional<StructLayout> LayoutOf(Entry const& entry);
	Machine GetMachine() const {
		return m_Machine;
	}
	// The C declaration of an entry; 'lines' gets, for each line, the type it refers to (empty for none)
	std::string Declaration(Entry const& entry, std::vector<std::string>* lines = nullptr);
	// the page of Microsoft's documentation of an entry (from its [Documentation] attribute); empty if it has none
	std::wstring DocumentationUrl(Entry const& entry);
	// the header a page of the documentation is of ("fileapi.h"; empty if it is not known)
	static std::string HeaderOf(std::wstring const& url);
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
	// The attributes of the metadata on what a token is: by their short names ("MemorySize", "Obsolete"), with their
	// arguments (a named one by its name, a constructor's by its position: "0", "1"), strings without their quotes
	struct Attribute {
		std::string Name;
		std::vector<std::pair<std::string, std::string>> Arguments;
		// an argument's value (empty if it has none)
		std::string operator[](std::string const& name) const;
	};
	std::vector<Attribute> AttributesOf(uint32_t token);
	// What the attributes say of a function, a type, a field or a parameter, as notes ("Windows XP+", "size:
	// nNumberOfBytesToRead"); method: of a parameter, for the names of those its sizes and counts are
	std::vector<std::string> Notes(std::vector<Attribute> const& attributes, uint32_t method = 0);
	std::vector<Entry> m_Entries;
	std::unordered_map<std::string, size_t> m_EntryIndex;
	Namespace m_Namespaces;
	bool m_NamespacesBuilt{ false };
	void BuildUsedBy();
	// the fields of a struct or union (described: type) that is at an offset (base), into a layout
	void AddLayout(uint32_t id, GhidraDecompiler::Type const& type, uint32_t base, int depth, std::string const& path,
		std::vector<LayoutItem>& items);
	std::vector<std::vector<size_t>> m_UsedBy;	// of each entry
	std::vector<std::pair<int64_t, NamedValue>> m_Values;	// by value
	bool m_ValuesBuilt{ false };

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
