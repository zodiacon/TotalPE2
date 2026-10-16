#include "TestCommon.h"
#include <RpcInterfaces.h>
#include <algorithm>
#include <format>
#include <iostream>

// The RPC interfaces of a file (TOTALPE_RPC_FILE)
TEST_CASE("The RPC interfaces of a file", "[.][rpc-file]") {
	char file[MAX_PATH]{};
	::GetEnvironmentVariableA("TOTALPE_RPC_FILE", file, sizeof(file));
	PEFile pe;
	REQUIRE(pe.Open(std::wstring(file, file + strlen(file))));
	// (TOTALPE_RPC_CSV: every procedure, "UUID,interface,opnum,routine RVA", for resolving the routines by the symbols)
	if (::GetEnvironmentVariableA("TOTALPE_RPC_CSV", nullptr, 0)) {
		for (auto const& rpc : FindRpcInterfaces(pe))
			for (auto const& p : rpc.Procedures)
				std::cout << std::format("{},{},{},{:X}\n", RpcUuidText(rpc.Uuid), KnownRpcInterface(rpc.Uuid), p.Opnum, p.Routine);
		return;
	}
	for (auto const& rpc : FindRpcInterfaces(pe)) {
		WARN(std::format("{:X} {} {} v{}.{} {} procedures {} {}{}", rpc.Rva, rpc.Server ? "server" : "client", RpcUuidText(rpc.Uuid),
			rpc.Major, rpc.Minor, KnownRpcInterface(rpc.Uuid), rpc.DispatchCount, rpc.Ndr64 ? "NDR64 " : "",
			rpc.Endpoints.empty() ? std::string() : rpc.Endpoints[0].first + ":" + rpc.Endpoints[0].second));
		int shown = 0;
		for (auto const& p : rpc.Procedures) {
			if (shown++ >= 6)
				break;
			std::string parameters;
			for (auto const& parameter : p.Parameters)
				parameters += std::format("{} @{}, ", RpcParameterText(parameter), parameter.StackOffset);
			WARN(std::format("  {} {:X} {} ({}) -> {} | {}", p.Opnum, p.Routine, p.Described ? "" : "?", parameters,
				p.HasResult ? p.Result.Type : "", RpcRoutineDeclaration(p, "F")));
		}
	}
}

TEST_CASE("The texts of RPC's UUIDs, base types and parameters", "[rpc]") {
	// (as a GUID is in memory: its first three fields little-endian)
	uint8_t const srvsvc[16] = { 0xC8, 0x4F, 0x32, 0x4B, 0x70, 0x16, 0xD3, 0x01, 0x12, 0x78, 0x5A, 0x47, 0xBF, 0x6E, 0xE1, 0x88 };
	CHECK(RpcUuidText(srvsvc) == "4B324FC8-1670-01D3-1278-5A47BF6EE188");
	CHECK(KnownRpcInterface(srvsvc) == "srvsvc");
	uint8_t const unknown[16]{};
	CHECK(KnownRpcInterface(unknown).empty());
	CHECK(RpcBaseTypeName(0x08) == "long");
	CHECK(RpcBaseTypeName(0x05) == "wchar_t");
	CHECK(RpcBaseTypeName(0x40).empty());
	RpcParameter in{ .Attributes = RpcParameter::IsIn | RpcParameter::IsBasetype, .BaseType = 0x08, .Type = "long" };
	RpcParameter inOut{ .Attributes = RpcParameter::IsIn | RpcParameter::IsOut, .Type = "struct *" };
	CHECK(RpcParameterText(in) == "[in] long");
	CHECK(RpcParameterText(inOut) == "[in, out] struct *");
}

TEST_CASE("The names of the procedures of Windows' protocols, by their operation numbers", "[rpc]") {
	// (as a GUID is in memory: its first three fields little-endian)
	auto uuid = [](char const* text) {
		unsigned long data1;
		unsigned data2, data3, rest[8];
		REQUIRE(sscanf_s(text, "%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &data1, &data2, &data3, &rest[0], &rest[1], &rest[2], &rest[3],
			&rest[4], &rest[5], &rest[6], &rest[7]) == 11);
		std::vector<uint8_t> bytes(16);
		memcpy(bytes.data(), &data1, 4);
		memcpy(bytes.data() + 4, &data2, 2);
		memcpy(bytes.data() + 6, &data3, 2);
		for (int i = 0; i < 8; i++)
			bytes[8 + i] = (uint8_t)rest[i];
		return bytes;
	};
	auto svcctl = uuid("367ABB81-9844-35F1-AD32-98F038001003");
	CHECK(KnownRpcProcedure(svcctl.data(), 0) == "RCloseServiceHandle");
	CHECK(KnownRpcProcedure(svcctl.data(), 15) == "ROpenSCManagerW");
	CHECK(KnownRpcProcedure(svcctl.data(), 1000).empty());
	// (the protocol's names: not those of the routines that serve them, which Windows' symbols have)
	auto eventlog = uuid("82273FDC-E32A-18C3-3F78-827929DC23EA");
	CHECK(KnownRpcProcedure(eventlog.data(), 0) == "ElfrClearELFW");
	auto srvsvc = uuid("4B324FC8-1670-01D3-1278-5A47BF6EE188");
	CHECK(KnownRpcProcedure(srvsvc.data(), 0) == "NetrCharDevEnum");
	CHECK(KnownRpcProcedure(srvsvc.data(), 15) == "NetrShareEnum");
	CHECK(KnownRpcProcedure(uuid("E3514235-4B06-11D1-AB04-00C04FC2DCD2").data(), 12) == "IDL_DRSCrackNames");
	// one that is not known, of a known interface; an interface that is not known
	auto wkssvc = uuid("6BFFD098-A112-3610-9833-46C3F87E345A");
	CHECK(KnownRpcProcedure(wkssvc.data(), 14).empty());
	CHECK(KnownRpcProcedure(wkssvc.data(), 0) == "NetrWkstaGetInfo");
	uint8_t const unknown[16]{};
	CHECK(KnownRpcProcedure(unknown, 0).empty());
}

TEST_CASE("The declaration of an RPC server routine, of its procedure's format", "[rpc]") {
	RpcProcedure p;
	p.Described = true;
	p.Parameters = {
		{ .Attributes = RpcParameter::IsIn, .StackOffset = 0, .Handle = true, .Type = "handle_t" },
		{ .Attributes = RpcParameter::IsIn | RpcParameter::IsBasetype, .StackOffset = 8, .BaseType = 0x08, .Type = "long" },
		{ .Attributes = RpcParameter::IsOut | RpcParameter::IsBasetype | RpcParameter::IsSimpleRef, .StackOffset = 16, .BaseType = 0x09, .Type = "unsigned long *" },
		{ .Attributes = RpcParameter::IsIn, .StackOffset = 32, .TypeFormat = 0x11, .Type = "struct *" },
	};
	p.HasResult = true;
	p.Result = { .Attributes = RpcParameter::IsOut | RpcParameter::IsReturn | RpcParameter::IsBasetype, .StackOffset = 40, .BaseType = 0x10 };
	// (a place the format does not have, Arg4, is a pointer's)
	CHECK(RpcRoutineDeclaration(p, "F") ==
		"unsigned long __cdecl F(void * Binding, long Arg2, unsigned long * Arg3, void * Arg4, void * Arg5)");
	SECTION("asynchronous: its PRPC_ASYNC_STATE first") {
		p.Async = true;
		for (auto& parameter : p.Parameters)
			parameter.StackOffset += 8;
		p.HasResult = false;
		CHECK(RpcRoutineDeclaration(p, "F").starts_with("void __cdecl F(void * AsyncHandle, void * Binding, long Arg3"));
	}
	SECTION("a base type of a range ([range] DWORD): by its type, not as a base type") {
		p.Parameters[1] = { .Attributes = RpcParameter::IsIn, .StackOffset = 8, .TypeFormat = 0xB7, .Type = "long" };
		CHECK(RpcRoutineDeclaration(p, "F").find("long Arg2,") != std::string::npos);
	}
	SECTION("not of 64-bit code, nor one whose format is not known") {
		p.Parameters[1].StackOffset = 4;
		CHECK(RpcRoutineDeclaration(p, "F").empty());
		p.Described = false;
		CHECK(RpcRoutineDeclaration(p, "F").empty());
	}
}

TEST_CASE("The RPC interfaces a service serves: srvsvc", "[rpc]") {
	PEFile pe;
	REQUIRE(pe.Open(LR"(C:\Windows\System32\srvsvc.dll)"));
	auto interfaces = FindRpcInterfaces(pe);
	REQUIRE(!interfaces.empty());
	CHECK(std::ranges::is_sorted(interfaces, {}, &RpcInterface::Rva));
	auto srvsvc = std::ranges::find_if(interfaces, [](auto const& rpc) { return KnownRpcInterface(rpc.Uuid) == "srvsvc"; });
	REQUIRE(srvsvc != interfaces.end());
	CHECK(srvsvc->Server);
	CHECK(srvsvc->Major == 3);
	CHECK(srvsvc->Minor == 0);
	CHECK(!srvsvc->Ndr64);
	REQUIRE(srvsvc->Procedures.size() == srvsvc->DispatchCount);
	REQUIRE(srvsvc->Procedures.size() > 50);
	// NetrShareEnum (opnum 15): [in, string, unique] ServerName, [in, out] InfoStruct, [in] PreferedMaximumLength,
	// [out] TotalEntries, [in, out, unique] ResumeHandle; NET_API_STATUS
	auto const& shareEnum = srvsvc->Procedures[15];
	CHECK(shareEnum.Opnum == 15);
	CHECK(shareEnum.Routine != 0);
	REQUIRE(shareEnum.Described);
	REQUIRE(shareEnum.Parameters.size() == 5);
	CHECK(RpcParameterText(shareEnum.Parameters[0]) == "[in] wchar_t * (string)");
	CHECK(RpcParameterText(shareEnum.Parameters[2]) == "[in] long");
	CHECK(RpcParameterText(shareEnum.Parameters[3]) == "[out] long *");
	CHECK(shareEnum.HasResult);
	CHECK(shareEnum.Result.Type == "long");
	CHECK(RpcRoutineDeclaration(shareEnum, "NetrShareEnum") ==
		"long __cdecl NetrShareEnum(void * Arg1, void * Arg2, long Arg3, long * Arg4, void * Arg5)");
	// (another of its interfaces is asynchronous: its procedures have their binding handles)
	auto async = std::ranges::find_if(interfaces, [](auto const& rpc) {
		return !rpc.Procedures.empty() && rpc.Procedures[0].Async; });
	REQUIRE(async != interfaces.end());
	REQUIRE(!async->Procedures[0].Parameters.empty());
	CHECK(async->Procedures[0].Parameters[0].Handle);
}

TEST_CASE("The RPC interfaces a DLL calls: srvcli (x64 and x86)", "[rpc]") {
	for (auto path : { LR"(C:\Windows\System32\srvcli.dll)", LR"(C:\Windows\SysWOW64\srvcli.dll)" }) {
		INFO(Narrow(path));
		PEFile pe;
		REQUIRE(pe.Open(path));
		auto interfaces = FindRpcInterfaces(pe);
		// (x86's is in its code section, with the rest of its constant data)
		auto srvsvc = std::ranges::find_if(interfaces, [](auto const& rpc) { return KnownRpcInterface(rpc.Uuid) == "srvsvc"; });
		REQUIRE(srvsvc != interfaces.end());
		CHECK(!srvsvc->Server);
		CHECK(srvsvc->Procedures.empty());
	}
}
