#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class PEFile;

//
// The RPC interfaces of an image, as MIDL's stubs describe them in its data: an RPC_SERVER_INTERFACE of each interface it
// serves, an RPC_CLIENT_INTERFACE of each it calls (their sizes and NDR's transfer syntax tell them). A server's
// MIDL_SERVER_INFO has its routines (in the order of their operation numbers) and the format strings of their procedures:
// the parameters of each, where they are on the stack, their directions and their types (a base type, or a type of the
// type format string).
//
struct RpcParameter {
	// PARAM_ATTRIBUTES
	enum : uint16_t {
		MustSize = 0x0001, MustFree = 0x0002, IsPipe = 0x0004, IsIn = 0x0008, IsOut = 0x0010, IsReturn = 0x0020,
		IsBasetype = 0x0040, IsByValue = 0x0080, IsSimpleRef = 0x0100,
	};
	uint16_t Attributes{};
	uint16_t StackOffset{};
	uint8_t BaseType{};			// FC_LONG... (IsBasetype)
	uint16_t TypeOffset{};		// in the type format string (otherwise)
	uint8_t TypeFormat{};		// its first byte (FC_RP, FC_STRUCT, FC_BIND_CONTEXT...); 0 if it is not known
	bool Handle{ false };		// an explicit primitive handle (handle_t): it is not in the format's list of parameters
	std::string Type;			// as the format describes it: "long", "wchar_t *", "struct *", "context handle"
};

struct RpcProcedure {
	uint32_t Opnum{};
	uint32_t Routine{};			// RVA of the server routine; 0 if it is not known
	bool Described{ false };	// its format string was read: what follows is known
	uint16_t StackSize{};
	uint8_t HandleType{};		// 0: an explicit handle (described by the format); otherwise FC_AUTO_HANDLE...
	bool Async{ false };		// asynchronous: its first parameter (not in the list) is its PRPC_ASYNC_STATE
	std::vector<RpcParameter> Parameters;	// in the order of the stack (the result is not one)
	bool HasResult{ false };
	RpcParameter Result;
};

struct RpcInterface {
	uint32_t Rva{};				// of the RPC_SERVER_INTERFACE (RPC_CLIENT_INTERFACE)
	bool Server{ false };
	uint8_t Uuid[16]{};			// as it is in memory
	uint16_t Major{}, Minor{};
	bool Ndr64{ false };		// its transfer syntax is NDR64 (otherwise NDR)
	uint32_t Flags{};
	std::vector<std::pair<std::string, std::string>> Endpoints;	// protocol sequence ("ncacn_np"), endpoint ("\pipe\lsass")
	uint32_t ServerInfo{};		// RVA of the MIDL_SERVER_INFO (of a client, MIDL_STUBLESS_PROXY_INFO); 0 if none
	uint32_t DispatchCount{};	// the procedures (RPC_DISPATCH_TABLE)
	std::vector<RpcProcedure> Procedures;	// of a server
};

// The interfaces, in the order of their addresses
std::vector<RpcInterface> FindRpcInterfaces(PEFile const& pe);

// "12345778-1234-ABCD-EF00-0123456789AC"
std::string RpcUuidText(uint8_t const* uuid);
// The name of an interface that Windows' protocols have ("samr", "lsarpc"); empty if it is not one of those
std::string KnownRpcInterface(uint8_t const* uuid);
// "long", "wchar_t" of a base type (FC_LONG, FC_WCHAR...); empty if it is not one
std::string RpcBaseTypeName(uint8_t type);
// A parameter as the RPC view shows it: "[in] long", "[out] long *", "[in, out] pointer", "[in] context handle"
std::string RpcParameterText(RpcParameter const& parameter);
// The declaration of a server routine of 64-bit code (each parameter is in a register or a slot of 8 bytes), for the
// decompiler: "long __cdecl Name(void * Binding, long Arg2, long * Arg3)"; empty if it is not known (a parameter's type
// whose size is not known)
std::string RpcRoutineDeclaration(RpcProcedure const& procedure, std::string const& name);
