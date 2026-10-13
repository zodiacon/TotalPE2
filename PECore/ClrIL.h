#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class PEFile;
class ClrMetadata;

// What follows an IL opcode (ECMA-335 III.1.9)
enum class IlOperand : uint8_t {
	None,
	Int8,		// ldc.i4.s
	Int32,
	Int64,
	Float32,
	Float64,
	Branch8,	// a short branch: the target is relative to the next instruction
	Branch32,
	Switch,
	Argument8, Argument16,	// the number of an argument
	Local8, Local16,		// the number of a local
	UInt8,		// unaligned., no.
	Method, Field, Type, Token, String, Signature,	// a metadata token
};

struct IlOpcode {
	const wchar_t* Name;
	IlOperand Operand;
};

// The opcode (0xFE00 and above for the two-byte opcodes); null if there is no such opcode
IlOpcode const* GetIlOpcode(uint16_t opcode);

struct IlInstruction {
	uint32_t Offset;
	uint32_t Size;
	uint16_t Opcode;
	IlOperand Operand;
	int64_t Value{ 0 };				// an integer, a token, the number of an argument or local, a branch target (an offset)
	double Real{ 0 };
	std::vector<uint32_t> Targets;	// of a switch
};

// Decodes IL. It stops at the first byte that is not an opcode, or at an operand that the code does not hold; false then.
bool DecodeIl(std::span<const std::byte> code, std::vector<IlInstruction>& instructions);

// A clause of a method's exception handling (ECMA-335 II.25.4.6)
struct IlExceptionClause {
	uint32_t Flags;			// 0: catch, 1: filter, 2: finally, 4: fault
	uint32_t TryOffset, TryLength;
	uint32_t HandlerOffset, HandlerLength;
	uint32_t ClassToken;	// of a catch
	uint32_t FilterOffset;	// of a filter
};

struct IlMethodBody {
	uint32_t Rva{ 0 };
	uint32_t HeaderSize{ 0 };
	uint32_t MaxStack{ 8 };
	uint32_t LocalVarSigToken{ 0 };
	bool InitLocals{ false };
	std::span<const std::byte> Code;
	std::vector<IlExceptionClause> Clauses;
};

// Reads the body of a method (its header, code and exception clauses) at an RVA. False if it is not in the file or is not
// a valid body.
bool ReadIlMethodBody(PEFile const& pe, uint32_t rva, IlMethodBody& body);

// A method in IL assembler, as ildasm writes it: the declaration, the directives (.maxstack, .locals), the instructions
// with the names of what they refer to, and the try blocks and their handlers. 'tokens', if given, receives the token
// that each line refers to (0 for none): the method of a call, the field of ldfld, the type of a catch...
std::wstring DisassembleIlMethod(PEFile const& pe, ClrMetadata const& md, uint32_t method, std::vector<uint32_t>* tokens = nullptr);
// A type: its declaration, fields and the methods
std::wstring DisassembleIlType(PEFile const& pe, ClrMetadata const& md, uint32_t type, std::vector<uint32_t>* tokens = nullptr);

// The syntax of the IL that the functions above write, for coloring
enum class IlStyle : uint8_t {
	Default,
	Comment,	// "//" to the end of the line
	String,		// "..."
	Label,		// IL_0000
	Opcode,		// the word after a label
	Directive,	// .method, .locals...
	Keyword,	// public, static, class, catch...
	Type,		// int32, string...
	Number,
};
// The style of each character of one line
std::vector<IlStyle> IlLineStyles(std::string_view line);

// A string literal as IL shows it: in quotes, with escapes
std::wstring IlQuote(std::wstring_view text);

// The strings each method uses (ldstr): the RVA of the characters of a user string, and the MethodDef row
struct IlStringUse {
	uint32_t Rva;
	uint32_t Method;
};
std::vector<IlStringUse> FindIlStringUses(PEFile const& pe, ClrMetadata const& md);
