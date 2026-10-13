#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

//
// Functions of an executable image as C, by Ghidra's decompiler. Nothing of Ghidra is visible here.
//
// The image is read through a callback (virtual addresses), so the caller decides how it is mapped. Names of functions
// that are known (exports, symbols) can be given before decompiling: calls to them are shown by name. Types, the
// prototypes of functions and global variables come from the caller as well (from symbols), described by the caller's
// ids through callbacks: the decompiler asks for what it uses, when it uses it.
// Not thread safe: one decompiler is used by one thread at a time.
//
class GhidraDecompiler {
public:
	// Fills 'buffer' with 'size' bytes at a virtual address; false if the image has no data there
	using ReadMemory = std::function<bool(uint64_t address, uint8_t* buffer, size_t size)>;

	enum class TypeKind {
		Unknown,	// bytes of a size
		Void,
		Integer,
		Boolean,
		Char,		// a character of its size (char, wchar_t...)
		Float,
		Pointer,
		Array,
		Struct,
		Union,
		Enum,
		Typedef,
		Function,
	};

	// A type as the caller describes it. Types refer to each other by the caller's ids (any number but 0, which is
	// "no type": void), so that types that point to each other (a list's node) can be described.
	struct Type {
		struct Field {
			std::string Name;
			uint32_t Offset;		// in bytes (of a bit field: of the integer it is in)
			uint32_t Type;
			uint32_t Bits{ 0 };		// of a bit field: its size in bits (0: not a bit field)
			uint32_t BitPosition{ 0 };	// of a bit field: its first bit, from the least significant of the integer at the offset
		};
		TypeKind Kind{ TypeKind::Unknown };
		std::string Name;			// of a struct, union, enum or typedef (empty: one is made up)
		uint32_t Size{ 0 };			// in bytes
		bool Signed{ true };		// of an integer or an enum
		uint32_t Target{ 0 };		// what a pointer points to, the element of an array, what a typedef names, the return type of a function
		uint32_t Count{ 0 };		// the elements of an array
		std::vector<Field> Fields;	// of a struct or union
		std::vector<std::pair<std::string, uint64_t>> Values;	// of an enum
		std::vector<uint32_t> Parameters;	// of a function (an object pointer, "this", first)
		bool Variadic{ false };		// a function with "..." after its parameters
	};
	// Describes the type of an id; false if there is none
	using TypeSource = std::function<bool(uint32_t id, Type& type)>;
	// The prototype of the function at an address: the id of its type (a function) and the names of its parameters;
	// false if it is not known
	using PrototypeSource = std::function<bool(uint64_t address, uint32_t& type, std::vector<std::string>& parameterNames)>;
	// The name of the function at an address (a C identifier, or a qualified C++ name); empty if it has none
	using NameSource = std::function<std::string(uint64_t address)>;
	// A local variable of a function, on the stack: its name, the id of its type and its offset from the stack pointer as
	// it is when the function is entered (on x86 and x64 the return address is at 0, the parameters on the stack after it)
	struct Local {
		std::string Name;
		uint32_t Type;
		int64_t Offset;
	};
	// The local variables of the function at an address; false if they are not known
	using LocalSource = std::function<bool(uint64_t address, std::vector<Local>& locals)>;

	// Once per process: the folder of the language definitions (.ldefs, .sla, .pspec, .cspec), in it or in its
	// subfolders (one per processor)
	static bool Initialize(std::wstring const& languagesFolder, std::string& error);

	// 'target': the language and compiler, for example "x86:LE:64:default:windows"
	GhidraDecompiler(std::string const& target, ReadMemory read);
	~GhidraDecompiler();

	// The decompiler is made on first use; false (with the reason) if it cannot be
	bool Open(std::string& error);
	// a function at an address gets a name (a C identifier, or a qualified C++ name)
	void AddFunction(uint64_t address, std::string const& name);
	// Memory that does not change (constant data, code): what is read there is known, and a pointer into it at a string is
	// shown as the string
	void AddReadOnly(uint64_t address, uint64_t size);
	// Imports: a call through the pointer at 'slot' (an entry of the import address table) is a call of the function
	// 'name', which is outside the image. Each such function gets an address in an area that the image does not use,
	// which is set first. 'module' (the DLL, as it is to be shown) is told of a function that only jumps to the import.
	void SetExternalArea(uint64_t address);
	void AddImport(uint64_t slot, std::string const& name, std::string const& module = {});

	// Types, prototypes, names and local variables (set before decompiling). The name and prototype of the decompiled function, and
	// those of the functions it calls, are asked for when it is decompiled: a large program's symbols are not all given
	// ahead.
	void SetTypeSource(TypeSource source);
	void SetPrototypeSource(PrototypeSource source);
	void SetNameSource(NameSource source);
	void SetLocalSource(LocalSource source);
	// a global variable: its name and the id of its type
	void AddGlobal(uint64_t address, std::string const& name, uint32_t type);

	// The function at an address as C; 'name' is used if the function has none yet. 'functions' (if given) gets the
	// functions the code refers to (the function itself and those it calls), by the names it shows, and their addresses.
	// 'lines' (if given) gets, for each line of the code, the addresses of the instructions it was made from (none for
	// a line of declarations or braces).
	using FunctionRefs = std::vector<std::pair<std::string, uint64_t>>;
	using LineAddresses = std::vector<std::vector<uint64_t>>;
	bool Decompile(uint64_t address, std::string const& name, std::string& code, std::string& error, FunctionRefs* functions = nullptr,
		LineAddresses* lines = nullptr);

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};
