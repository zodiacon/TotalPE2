#pragma once

#include <GhidraDecompiler.h>
#include <ClrMetadata.h>
#include <PEFile.h>
#include <map>
#include <memory>
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

	// What the metadata has, to look at: the functions (of the machine) and the types (not those nested in others)
	struct Entry {
		enum class Kinds : uint8_t { Function, Enum, Struct, Union, Typedef, Callback, Interface };
		std::string Name;
		Kinds Kind;
		std::string Where;	// a function's DLL, a type's namespace (without "Windows.Win32.")
		uint32_t Row;		// MethodDef or TypeDef
	};
	std::vector<Entry> const& Entries();
	static PCWSTR KindName(Entry::Kinds kind);
	// a function or type by its name (null if there is none)
	Entry const* Find(std::string const& name);
	// The C declaration of an entry; 'lines' gets, for each line, the type it refers to (empty for none)
	std::string Declaration(Entry const& entry, std::vector<std::string>* lines = nullptr);
	// a type as C writes it ("HANDLE", "WIN32_FIND_DATAW *", "wchar_t[260]")
	std::string TypeText(uint32_t id);

private:
	struct Data;
	Win32Api(Data& data, Machine machine);
	static Data* Shared();

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
	std::string Name(uint32_t row);
	// the type a line refers to: the first one of the metadata in a type
	std::string Referred(uint32_t id);
	std::vector<Entry> m_Entries;
	std::unordered_map<std::string, size_t> m_EntryIndex;

	Data& m_Data;
	Machine m_Machine;
	uint32_t m_Pointer;
	std::vector<GhidraDecompiler::Type> m_Made;	// the types made for signatures: pointers, arrays, functions, numbers
	std::map<std::string, uint32_t> m_MadeKeys;
	std::unordered_map<uint32_t, GhidraDecompiler::Type> m_Described;
	std::unordered_map<uint32_t, uint32_t> m_Alignment;	// of the structs and unions described
	std::set<uint32_t> m_Laying;	// the structs whose layout is being found (a struct does not hold itself)
	static constexpr uint32_t MadeBit = 0x40000000;
};
