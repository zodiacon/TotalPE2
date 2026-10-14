#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class PEFile;

//
// The exception handling of an x64 function, from its unwind information: its __try regions (the scope table of
// __C_specific_handler) and its C++ try blocks (the FuncInfo of __CxxFrameHandler3, or the compressed one of
// __CxxFrameHandler4). What code each guards, and what handles its exceptions.
//
enum class ExceptionHandlerKind {
	None,
	Seh,		// __C_specific_handler, __GSHandlerCheck_SEH: a scope table
	Cxx3,		// __CxxFrameHandler3, __GSHandlerCheck_EH: FuncInfo
	Cxx4,		// __CxxFrameHandler4, __GSHandlerCheck_EH4: FuncInfo4 (compressed)
};

struct ExceptionScope {
	enum class Kinds { Except, Finally, Try };
	Kinds Kind{ Kinds::Except };
	// the code it guards: RVAs, [begin, end) (one region of the source may be in several pieces)
	std::vector<std::pair<uint32_t, uint32_t>> Ranges;
	// __except: the filter: a function, or (FilterRva 0) the constant EXCEPTION_EXECUTE_HANDLER
	uint32_t FilterRva{ 0 };
	// __except: the code of the handler (in the function); __finally: the termination handler (a function)
	uint32_t HandlerRva{ 0 };

	// a catch of a C++ try
	struct Catch {
		std::string Type;		// decorated, as the type descriptor has it (".?AVexception@std@@"); empty: catch (...)
		uint32_t Adjectives{ 0 };	// HT_IsConst (1), HT_IsVolatile (2), HT_IsReference (8)...
		uint32_t HandlerRva{ 0 };	// the catch funclet
	};
	std::vector<Catch> Catches;
};

struct ExceptionHandling {
	ExceptionHandlerKind Kind{ ExceptionHandlerKind::None };
	uint32_t HandlerRva{ 0 };
	std::vector<ExceptionScope> Scopes;	// inner ones first
	std::wstring Error;		// why the data could not be read (or was only partly read)
};

// The kind of the handler by its name ("__C_specific_handler", "__CxxFrameHandler4"...)
ExceptionHandlerKind ExceptionHandlerKindOf(std::string_view name);

// The exception handling of the function that begins at 'functionRva' (it has an entry of the exception directory).
// 'handlerName' names the handler (by its RVA: a symbol, or the import a thunk jumps to); without a name, a FuncInfo
// of __CxxFrameHandler3 is known by its magic number.
ExceptionHandling ReadExceptionHandling(PEFile const& pe, uint32_t functionRva,
	std::function<std::string(uint32_t rva)> const& handlerName);

// The same, from the data: the handler's data (at 'dataRva'), the function's range, and a reader of the image (by RVA)
using ImageReader = std::function<bool(uint32_t rva, void* buffer, size_t size)>;
ExceptionHandling ReadExceptionHandling(ImageReader const& read, ExceptionHandlerKind kind, uint32_t handlerRva,
	uint32_t dataRva, uint32_t functionBegin, uint32_t functionEnd);

// A value of the compressed encoding of __CxxFrameHandler4 (the length is in the low bits of the first byte); the bytes it
// took are added to 'used'. False if the data ends first.
bool ReadCompressedUnsigned(std::span<const uint8_t> data, size_t& used, uint32_t& value);
