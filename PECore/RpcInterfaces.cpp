#include "pch.h"
#include "RpcInterfaces.h"
#include "PEFile.h"
#include <algorithm>
#include <format>

namespace {
	// NDR's transfer syntax {8A885D04-1CEB-11C9-9FE8-08002B104860} 2.0, NDR64's {71710533-BEBA-4937-8319-B5DBEF9CCC36} 1.0
	constexpr uint8_t Ndr[16] = { 0x04, 0x5D, 0x88, 0x8A, 0xEB, 0x1C, 0xC9, 0x11, 0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60 };
	constexpr uint8_t Ndr64[16] = { 0x33, 0x05, 0x71, 0x71, 0xBA, 0xBE, 0x37, 0x49, 0x83, 0x19, 0xB5, 0xDB, 0xEF, 0x9C, 0xCC, 0x36 };

	struct Image {
		PEFile const& Pe;
		uint64_t Base;
		bool Pe64;
		uint32_t Pointer;

		template<typename T>
		bool Read(uint32_t rva, T& value) const {
			auto offset = Pe.GetOffsetFromRVA(rva);
			if (!rva || !offset || offset + sizeof(T) > Pe.GetFileSize())
				return false;
			auto span = Pe.GetSpan((uint32_t)offset, sizeof(T));
			if (span.size() != sizeof(T))
				return false;
			memcpy(&value, span.data(), sizeof(T));
			return true;
		}
		// a pointer of the image as an RVA (0 if it is null, or not in the image)
		uint32_t PointerAt(uint32_t rva) const {
			uint64_t va = 0;
			if (Pe64) {
				if (!Read(rva, va))
					return 0;
			}
			else {
				uint32_t v32;
				if (!Read(rva, v32))
					return 0;
				va = v32;
			}
			return va >= Base && va - Base < 0x100000000ULL ? (uint32_t)(va - Base) : 0;
		}
		std::string String(uint32_t rva, size_t max = 256) const {
			std::string text;
			char ch;
			while (text.size() < max && Read(rva++, ch) && ch)
				text += ch;
			return text;
		}
		bool InCode(uint32_t rva) const {
			for (auto const& s : *Pe.GetSecHeaders()) {
				auto const& h = s.SecHdr;
				if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData))
					return (h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) != 0;
			}
			return false;
		}
	};

	// a pointer to a type described ("wchar_t * (string)" of a string)
	std::string PointerTo(std::string const& type) {
		constexpr std::string_view string = "[] (string)";
		if (type.ends_with(string))
			return type.substr(0, type.size() - string.size()) + " * (string)";
		return type + " *";
	}

	// the type at an offset of the type format string, as the RPC view shows it
	std::string DescribeType(Image const& image, uint32_t types, uint32_t offset, int depth = 0) {
		uint8_t format = 0;
		if (!types || depth > 3 || !image.Read(types + offset, format))
			return "?";
		switch (format) {
			case 0x11: case 0x12: case 0x13: case 0x14:		// FC_RP, FC_UP, FC_OP, FC_FP
			{
				uint8_t flags = 0, simple = 0;
				int16_t relative = 0;
				image.Read(types + offset + 1, flags);
				if (flags & 0x08) {		// FC_SIMPLE_POINTER: the base type follows
					image.Read(types + offset + 2, simple);
					if (simple == 0x22 || simple == 0x25)		// FC_C_CSTRING, FC_C_WSTRING
						return simple == 0x22 ? "char * (string)" : "wchar_t * (string)";
					auto name = RpcBaseTypeName(simple);
					return (name.empty() ? std::string("?") : name) + " *";
				}
				if (!image.Read(types + offset + 2, relative))
					return "pointer";
				return PointerTo(DescribeType(image, types, offset + 2 + relative, depth + 1));
			}
			case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0xB1:
				return "struct";
			case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20: case 0x21:
				return "array";
			case 0x22: case 0x26:
				return "char[] (string)";
			case 0x25: case 0x29:
				return "wchar_t[] (string)";
			case 0x2A: case 0x2B:
				return "union";
			case 0x2D: case 0x2E:
				return "transmitted type";
			case 0x2F:
				return "interface pointer";
			case 0x30:
				return "context handle";
			case 0xB4:
				return "user marshal";
			case 0xB5:
				return "pipe";
			case 0xB7:		// FC_RANGE: of a base type
			{
				uint8_t base = 0;
				image.Read(types + offset + 1, base);
				auto name = RpcBaseTypeName(base & 0x0F);
				return name.empty() ? "range" : name;
			}
			default:
			{
				auto name = RpcBaseTypeName(format);
				return name.empty() ? std::format("type 0x{:02X}", format) : name;
			}
		}
	}

	// A procedure of the format string (Oicf): its header, its explicit handle, its parameters. False if it is not one.
	bool ReadProcedure(Image const& image, uint32_t at, uint32_t types, uint32_t opnum, RpcProcedure& procedure) {
		uint8_t handleType = 0, oiFlags = 0;
		uint16_t number = 0, stackSize = 0;
		if (!image.Read(at, handleType) || !image.Read(at + 1, oiFlags))
			return false;
		uint32_t pos = at + 2;
		if (oiFlags & 0x08)		// Oi_HAS_RPCFLAGS
			pos += 4;
		if (!image.Read(pos, number) || !image.Read(pos + 2, stackSize) || number != opnum)
			return false;
		pos += 4;
		std::vector<RpcParameter> parameters;
		if (handleType == 0) {
			// an explicit handle: a primitive one is a parameter the list does not have; a generic one or a context
			// handle is in the list
			uint8_t kind = 0;
			uint16_t offset = 0;
			image.Read(pos, kind);
			image.Read(pos + 2, offset);
			if (kind == 0x32) {		// FC_BIND_PRIMITIVE
				RpcParameter handle;
				handle.Handle = true;
				handle.Attributes = RpcParameter::IsIn;
				handle.StackOffset = offset;
				handle.Type = "handle_t";
				parameters.push_back(handle);
				pos += 4;
			}
			else if (kind == 0x31 || kind == 0x30)	// FC_BIND_GENERIC, FC_BIND_CONTEXT
				pos += 6;
			else
				return false;
		}
		// Oif: the sizes of the buffers, the interpreter's flags, the number of parameters (and the extensions)
		uint8_t flags = 0, count = 0, extension = 0;
		if (!image.Read(pos + 4, flags) || !image.Read(pos + 5, count) || count > 64)
			return false;
		pos += 6;
		if (flags & 0x40) {		// HasExtensions: its size first
			if (!image.Read(pos, extension) || extension < 2)
				return false;
			pos += extension;
		}
		procedure.HandleType = handleType;
		procedure.Async = (flags & 0x80) != 0;		// HasAsyncHandle
		procedure.StackSize = stackSize;
		for (uint8_t i = 0; i < count; i++, pos += 6) {
			RpcParameter p;
			uint16_t type = 0;
			if (!image.Read(pos, p.Attributes) || !image.Read(pos + 2, p.StackOffset) || !image.Read(pos + 4, type))
				return false;
			if (p.StackOffset >= stackSize && !(p.Attributes & RpcParameter::IsReturn))
				return false;
			if (p.Attributes & RpcParameter::IsBasetype) {
				p.BaseType = (uint8_t)type;
				p.Type = RpcBaseTypeName(p.BaseType);
				if (p.Type.empty())
					p.Type = std::format("type 0x{:02X}", p.BaseType);
				if (p.Attributes & RpcParameter::IsSimpleRef)
					p.Type += " *";
			}
			else {
				p.TypeOffset = type;
				if (types)
					image.Read(types + type, p.TypeFormat);
				p.Type = DescribeType(image, types, type);
				// (a simple reference: the pointer's target is described)
				if ((p.Attributes & RpcParameter::IsSimpleRef) && p.TypeFormat != 0x30)
					p.Type = PointerTo(p.Type);
			}
			if (p.Attributes & RpcParameter::IsReturn) {
				procedure.HasResult = true;
				procedure.Result = p;
			}
			else
				parameters.push_back(p);
		}
		std::ranges::stable_sort(parameters, {}, &RpcParameter::StackOffset);
		procedure.Parameters = std::move(parameters);
		procedure.Described = true;
		return true;
	}

	// the parts of a server's MIDL_SERVER_INFO: its routines, the procedures' format strings
	void ReadServer(Image const& image, RpcInterface& rpc, uint32_t dispatch, uint32_t epv) {
		auto P = image.Pointer;
		auto info = rpc.ServerInfo;
		auto stubDesc = image.PointerAt(info);
		auto routines = image.PointerAt(info + P);
		auto procString = image.PointerAt(info + 2 * P);
		auto offsets = image.PointerAt(info + 3 * P);
		auto types = stubDesc ? image.PointerAt(stubDesc + 8 * P) : 0;	// MIDL_STUB_DESC::pFormatTypes
		if (!procString) {
			// (only NDR64's in the MIDL_SERVER_INFO: NDR's is among its MIDL_SYNTAX_INFOs)
			uint64_t syntaxes = 0;
			if (image.Pe64)
				image.Read(info + 6 * P, syntaxes);
			else {
				uint32_t s32 = 0;
				image.Read(info + 6 * P, s32);
				syntaxes = s32;
			}
			auto syntaxInfo = image.PointerAt(info + 7 * P);
			auto size = (image.Pe64 ? 24 : 20) + 7 * P;
			for (uint32_t i = 0; syntaxInfo && i < syntaxes && i < 4; i++) {
				auto entry = syntaxInfo + i * size;
				uint8_t guid[16]{};
				if (!image.Read(entry, guid) || memcmp(guid, Ndr, 16))
					continue;
				auto first = entry + (image.Pe64 ? 24 : 20);
				procString = image.PointerAt(first + P);
				offsets = image.PointerAt(first + 2 * P);
				if (auto typeString = image.PointerAt(first + 3 * P))
					types = typeString;
				break;
			}
		}
		if (!routines)
			routines = epv;
		uint32_t count = 0;
		if (dispatch)
			image.Read(dispatch, count);
		rpc.DispatchCount = count;
		for (uint32_t i = 0; i < count && i < 1024; i++) {
			RpcProcedure procedure;
			procedure.Opnum = i;
			if (routines) {
				auto routine = image.PointerAt(routines + i * P);
				if (routine && image.InCode(routine))
					procedure.Routine = routine;
			}
			uint16_t offset = 0;
			if (procString && offsets && image.Read(offsets + i * 2, offset))
				if (!ReadProcedure(image, procString + offset, types, i, procedure))
					procedure = { i, procedure.Routine };
			rpc.Procedures.push_back(std::move(procedure));
		}
	}
}

std::vector<RpcInterface> FindRpcInterfaces(PEFile const& pe) {
	std::vector<RpcInterface> interfaces;
	auto sections = pe.GetSecHeaders();
	if (!sections)
		return interfaces;
	bool pe64 = pe.GetFileInfo()->IsPE64;
	Image image{ pe, pe.GetImageBase(), pe64, pe64 ? 8u : 4u };
	auto P = image.Pointer;
	// RPC_SERVER_INTERFACE (RPC_CLIENT_INTERFACE): its length, its interface, its transfer syntax, then pointers. In any
	// section: x86 DLLs of Windows have their constant data in their code's.
	uint32_t const length = pe64 ? 0x60 : 0x44;
	uint32_t const pointers = pe64 ? 48 : 44;
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		auto size = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		auto offset = pe.GetOffsetFromRVA(h.VirtualAddress);
		if (!offset || offset + size > pe.GetFileSize())
			continue;
		auto data = pe.GetSpan((uint32_t)offset, size);
		auto bytes = (uint8_t const*)data.data();
		for (uint32_t at = 0; at + length <= data.size(); at += 4) {
			uint32_t value;
			memcpy(&value, bytes + at, 4);
			if (value != length)
				continue;
			auto syntax = bytes + at + 24;
			uint16_t major, minor;
			memcpy(&major, syntax + 16, 2);
			memcpy(&minor, syntax + 18, 2);
			bool ndr = !memcmp(syntax, Ndr, 16) && major == 2 && minor == 0;
			bool ndr64 = !memcmp(syntax, Ndr64, 16) && major == 1 && minor == 0;
			if (!ndr && !ndr64)
				continue;
			RpcInterface rpc;
			rpc.Rva = h.VirtualAddress + at;
			memcpy(rpc.Uuid, bytes + at + 4, 16);
			memcpy(&rpc.Major, bytes + at + 20, 2);
			memcpy(&rpc.Minor, bytes + at + 22, 2);
			rpc.Ndr64 = ndr64;
			auto field = rpc.Rva + pointers;
			auto dispatch = image.PointerAt(field);
			uint32_t endpoints = 0;
			image.Read(field + P, endpoints);
			auto endpoint = image.PointerAt(field + 2 * P);
			auto epv = image.PointerAt(field + 3 * P);
			rpc.ServerInfo = image.PointerAt(field + 4 * P);
			image.Read(field + 5 * P, rpc.Flags);
			for (uint32_t i = 0; endpoint && i < endpoints && i < 16; i++)
				rpc.Endpoints.emplace_back(image.String(image.PointerAt(endpoint + i * 2 * P)), image.String(image.PointerAt(endpoint + i * 2 * P + P)));
			// A server: its dispatch table (the runtime's functions that unmarshal each call, NdrServerCall2), and the routines
			// of its MIDL_SERVER_INFO, in the code. A client has neither (the MIDL_STUBLESS_PROXY_INFO it has points to its
			// stub's descriptor, then to a format string).
			uint32_t first = 0;
			if (dispatch) {
				auto table = image.PointerAt(dispatch + P);
				first = table ? image.PointerAt(table) : 0;
			}
			auto infoRoutines = rpc.ServerInfo ? image.PointerAt(rpc.ServerInfo + P) : 0;
			auto routine = infoRoutines ? image.PointerAt(infoRoutines) : 0;
			rpc.Server = dispatch && (!first || image.InCode(first)) && (!rpc.ServerInfo || (routine && image.InCode(routine)) || epv);
			if (rpc.Server && rpc.ServerInfo)
				ReadServer(image, rpc, dispatch, epv);
			else if (rpc.Server) {
				uint32_t count = 0;
				image.Read(dispatch, count);
				rpc.DispatchCount = count;
			}
			interfaces.push_back(std::move(rpc));
		}
	}
	return interfaces;
}

std::string RpcUuidText(uint8_t const* u) {
	return std::format("{:02X}{:02X}{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
		u[3], u[2], u[1], u[0], u[5], u[4], u[7], u[6], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

std::string KnownRpcInterface(uint8_t const* uuid) {
	// the interfaces of Windows' protocols ([MS-...]) and of its runtime
	static std::pair<char const*, char const*> const known[] = {
		{ "12345778-1234-ABCD-EF00-0123456789AC", "samr" },
		{ "12345778-1234-ABCD-EF00-0123456789AB", "lsarpc" },
		{ "12345678-1234-ABCD-EF00-01234567CFFB", "netlogon" },
		{ "12345678-1234-ABCD-EF00-0123456789AB", "spoolss" },
		{ "76F03F96-CDFD-44FC-A22C-64950A001209", "IRemoteWinspool" },
		{ "367ABB81-9844-35F1-AD32-98F038001003", "svcctl" },
		{ "338CD001-2244-31F1-AAAA-900038001003", "winreg" },
		{ "4B324FC8-1670-01D3-1278-5A47BF6EE188", "srvsvc" },
		{ "6BFFD098-A112-3610-9833-46C3F87E345A", "wkssvc" },
		{ "E1AF8308-5D1F-11C9-91A4-08002B14A0FA", "epmapper" },
		{ "1FF70682-0A51-30E8-076D-740BE8CEE98B", "atsvc" },
		{ "86D35949-83C9-4044-B424-DB363231FD0C", "ITaskSchedulerService" },
		{ "C681D488-D850-11D0-8C52-00C04FD90F7E", "efsrpc" },
		{ "DF1941C5-FE89-4E79-BF10-463657ACF44D", "efsrpc" },
		{ "82273FDC-E32A-18C3-3F78-827929DC23EA", "eventlog" },
		{ "F6BEAFF7-1E19-4FBB-9F8F-B89E2018337C", "IEventService" },
		{ "E3514235-4B06-11D1-AB04-00C04FC2DCD2", "drsuapi" },
		{ "50ABC2A4-574D-40B3-9D66-EE4FD5FBA076", "DnsServer" },
		{ "3919286A-B10C-11D0-9BA8-00C04FD92EF5", "dssetup" },
		{ "000001A0-0000-0000-C000-000000000046", "IRemoteSCMActivator" },
		{ "99FCFEC4-5260-101B-BBCB-00AA0021347A", "IObjectExporter" },
		{ "AFA8BD80-7D8A-11C9-BEF4-08002B102989", "mgmt" },
	};
	auto text = RpcUuidText(uuid);
	for (auto const& [guid, name] : known)
		if (text == guid)
			return name;
	return {};
}

std::string KnownRpcProcedure(uint8_t const* uuid, uint32_t opnum) {
	// the names of the procedures of each interface, by their operation numbers ("-": one that is not known: not used on the
	// wire, or of a routine that serves several, whose name is not its own); those of the protocols' documents, and of Windows'
	// public symbols of those that Windows has and the documents do not
	static std::pair<char const*, char const*> const procedures[] = {
		{ "atsvc",
			"NetrJobAdd NetrJobDel NetrJobEnum NetrJobGetInfo" },
		{ "DnsServer",
			"R_DnssrvOperation R_DnssrvQuery R_DnssrvComplexOperation R_DnssrvEnumRecords R_DnssrvUpdateRecord "
			"R_DnssrvOperation2 R_DnssrvQuery2 R_DnssrvComplexOperation2 R_DnssrvEnumRecords2 R_DnssrvUpdateRecord2" },
		{ "drsuapi",
			"IDL_DRSBind IDL_DRSUnbind IDL_DRSReplicaSync IDL_DRSGetNCChanges IDL_DRSUpdateRefs IDL_DRSReplicaAdd "
			"IDL_DRSReplicaDel IDL_DRSReplicaModify IDL_DRSVerifyNames IDL_DRSGetMemberships IDL_DRSInterDomainMove "
			"IDL_DRSGetNT4ChangeLog IDL_DRSCrackNames IDL_DRSWriteSPN IDL_DRSRemoveDsServer IDL_DRSRemoveDsDomain "
			"IDL_DRSDomainControllerInfo IDL_DRSAddEntry IDL_DRSExecuteKCC IDL_DRSGetReplInfo IDL_DRSAddSidHistory "
			"IDL_DRSGetMemberships2 IDL_DRSReplicaVerifyObjects IDL_DRSGetObjectExistence IDL_DRSQuerySitesByCost "
			"IDL_DRSInitDemotion IDL_DRSReplicaDemotion IDL_DRSFinishDemotion IDL_DRSAddCloneDC IDL_DRSWriteNgcKey "
			"IDL_DRSReadNgcKey" },
		{ "dssetup",
			"DsRolerGetPrimaryDomainInformation" },
		{ "efsrpc",
			"EfsRpcOpenFileRaw EfsRpcReadFileRaw EfsRpcWriteFileRaw EfsRpcCloseRaw EfsRpcEncryptFileSrv "
			"EfsRpcDecryptFileSrv EfsRpcQueryUsersOnFile EfsRpcQueryRecoveryAgents EfsRpcRemoveUsersFromFile "
			"EfsRpcAddUsersToFile - - EfsRpcFileKeyInfo EfsRpcDuplicateEncryptionInfoFile - EfsRpcAddUsersToFileEx "
			"EfsRpcFileKeyInfoEx - EfsRpcGetEncryptedFileMetadata EfsRpcSetEncryptedFileMetadata EfsRpcFlushEfsCache" },
		{ "epmapper",
			"ept_insert ept_delete ept_lookup ept_map ept_lookup_handle_free ept_inq_object ept_mgmt_delete" },
		{ "eventlog",
			"ElfrClearELFW ElfrBackupELFW ElfrCloseEL ElfrDeregisterEventSource ElfrNumberOfRecords ElfrOldestRecord "
			"ElfrChangeNotify ElfrOpenELW ElfrRegisterEventSourceW ElfrOpenBELW ElfrReadELW ElfrReportEventW ElfrClearELFA "
			"ElfrBackupELFA ElfrOpenELA ElfrRegisterEventSourceA ElfrOpenBELA ElfrReadELA ElfrReportEventA "
			"ElfrRegisterClusterSvc ElfrDeregisterClusterSvc ElfrWriteClusterEvents ElfrGetLogInformation ElfrFlushEL "
			"ElfrReportEventAndSourceW ElfrReportEventExW ElfrReportEventExA" },
		{ "IEventService",
			"EvtRpcRegisterRemoteSubscription EvtRpcRemoteSubscriptionNextAsync EvtRpcRemoteSubscriptionNext "
			"EvtRpcRemoteSubscriptionWaitAsync EvtRpcRegisterControllableOperation EvtRpcRegisterLogQuery EvtRpcClearLog "
			"EvtRpcExportLog EvtRpcLocalizeExportLog EvtRpcMessageRender EvtRpcMessageRenderDefault EvtRpcQueryNext "
			"EvtRpcQuerySeek EvtRpcClose EvtRpcCancel EvtRpcAssertConfig EvtRpcRetractConfig EvtRpcOpenLogHandle "
			"EvtRpcGetLogFileInfo EvtRpcGetChannelList EvtRpcGetChannelConfig EvtRpcPutChannelConfig "
			"EvtRpcGetPublisherList EvtRpcGetPublisherListForChannel EvtRpcGetPublisherMetadata "
			"EvtRpcGetPublisherResourceMetadata EvtRpcGetEventMetadataEnum EvtRpcGetNextEventMetadata "
			"EvtRpcGetClassicLogDisplayName" },
		{ "IObjectExporter",
			"ResolveOxid SimplePing ComplexPing ServerAlive ResolveOxid2 ServerAlive2" },
		{ "IRemoteSCMActivator",
			"Opnum0NotUsedOnWire Opnum1NotUsedOnWire Opnum2NotUsedOnWire RemoteGetClassObject RemoteCreateInstance" },
		{ "IRemoteWinspool",
			"RpcAsyncOpenPrinter RpcAsyncAddPrinter RpcAsyncSetJob RpcAsyncGetJob RpcAsyncEnumJobs RpcAsyncAddJob "
			"RpcAsyncScheduleJob RpcAsyncDeletePrinter RpcAsyncSetPrinter RpcAsyncGetPrinter RpcAsyncStartDocPrinter "
			"RpcAsyncStartPagePrinter RpcAsyncWritePrinter RpcAsyncEndPagePrinter RpcAsyncEndDocPrinter "
			"RpcAsyncAbortPrinter RpcAsyncGetPrinterData RpcAsyncGetPrinterDataEx RpcAsyncSetPrinterData "
			"RpcAsyncSetPrinterDataEx RpcAsyncClosePrinter RpcAsyncAddForm RpcAsyncDeleteForm RpcAsyncGetForm "
			"RpcAsyncSetForm RpcAsyncEnumForms RpcAsyncGetPrinterDriver RpcAsyncEnumPrinterData RpcAsyncEnumPrinterDataEx "
			"RpcAsyncEnumPrinterKey RpcAsyncDeletePrinterData RpcAsyncDeletePrinterDataEx RpcAsyncDeletePrinterKey "
			"RpcAsyncXcvData RpcAsyncSendRecvBidiData RpcAsyncCreatePrinterIC RpcAsyncPlayGdiScriptOnPrinterIC "
			"RpcAsyncDeletePrinterIC RpcAsyncEnumPrinters RpcAsyncAddPrinterDriver RpcAsyncEnumPrinterDrivers "
			"RpcAsyncGetPrinterDriverDirectory RpcAsyncDeletePrinterDriver RpcAsyncDeletePrinterDriverEx "
			"RpcAsyncAddPrintProcessor RpcAsyncEnumPrintProcessors RpcAsyncGetPrintProcessorDirectory RpcAsyncEnumPorts "
			"RpcAsyncEnumMonitors RpcAsyncAddPort RpcAsyncSetPort RpcAsyncAddMonitor RpcAsyncDeleteMonitor "
			"RpcAsyncDeletePrintProcessor RpcAsyncEnumPrintProcessorDatatypes RpcAsyncAddPerMachineConnection "
			"RpcAsyncDeletePerMachineConnection RpcAsyncEnumPerMachineConnections RpcSyncRegisterForRemoteNotifications "
			"RpcSyncUnRegisterForRemoteNotifications RpcSyncRefreshRemoteNotifications RpcAsyncGetRemoteNotifications "
			"RpcAsyncInstallPrinterDriverFromPackage RpcAsyncUploadPrinterDriverPackage RpcAsyncGetCorePrinterDrivers "
			"RpcAsyncCorePrinterDriverInstalled RpcAsyncGetPrinterDriverPackagePath RpcAsyncDeletePrinterDriverPackage "
			"RpcAsyncReadPrinter RpcAsyncResetPrinter RpcAsyncGetJobNamedPropertyValue RpcAsyncSetJobNamedProperty "
			"RpcAsyncDeleteJobNamedProperty RpcAsyncEnumJobNamedProperties RpcAsyncLogJobInfoForBranchOffice "
			"RpcAsyncIppCreateJobOnPrinter RpcAsyncIppGetJobAttributes RpcAsyncIppSetJobAttributes "
			"RpcAsyncIppGetPrinterAttributes RpcAsyncIppSetPrinterAttributes RpcAsyncRegeneratePrintDeviceCapabilities" },
		{ "ITaskSchedulerService",
			"SchRpcHighestVersion SchRpcRegisterTask SchRpcRetrieveTask SchRpcCreateFolder SchRpcSetSecurity "
			"SchRpcGetSecurity SchRpcEnumFolders SchRpcEnumTasks SchRpcEnumInstances SchRpcGetInstanceInfo "
			"SchRpcStopInstance SchRpcStop SchRpcRun SchRpcDelete SchRpcRename SchRpcScheduledRuntimes "
			"SchRpcGetLastRunInfo SchRpcGetTaskInfo SchRpcGetNumberOfMissedRuns SchRpcEnableTask" },
		{ "lsarpc",
			"LsarClose LsarDelete LsarEnumeratePrivileges LsarQuerySecurityObject LsarSetSecurityObject LsarChangePassword "
			"LsarOpenPolicyRPC LsarQueryInformationPolicy LsarSetInformationPolicy - LsarCreateAccount "
			"LsarEnumerateAccounts LsarCreateTrustedDomain LsarEnumerateTrustedDomains LsarLookupNames LsarLookupSids "
			"LsarCreateSecret LsarOpenAccount LsarEnumeratePrivilegesAccount LsarAddPrivilegesToAccount "
			"LsarRemovePrivilegesFromAccount LsarGetQuotasForAccount LsarSetQuotasForAccount LsarGetSystemAccessAccount "
			"LsarSetSystemAccessAccount LsarOpenTrustedDomain LsarQueryInfoTrustedDomain LsarSetInformationTrustedDomain "
			"LsarOpenSecret LsarSetSecret LsarQuerySecret LsarLookupPrivilegeValue LsarLookupPrivilegeName "
			"LsarLookupPrivilegeDisplayName LsarDeleteObject LsarEnumerateAccountsWithUserRight LsarEnumerateAccountRights "
			"LsarAddAccountRights LsarRemoveAccountRights LsarQueryTrustedDomainInfo LsarSetTrustedDomainInfo "
			"LsarDeleteTrustedDomain LsarStorePrivateData LsarRetrievePrivateData LsarOpenPolicy2 LsarGetUserName "
			"LsarQueryInformationPolicy2 LsarSetInformationPolicy2 LsarQueryTrustedDomainInfoByName "
			"LsarSetTrustedDomainInfoByName LsarEnumerateTrustedDomainsEx LsarCreateTrustedDomainEx - "
			"LsarQueryDomainInformationPolicy LsarSetDomainInformationPolicy LsarOpenTrustedDomainByName - LsarLookupSids2 "
			"LsarLookupNames2 LsarCreateTrustedDomainEx2 CredrWrite CredrRead CredrEnumerate CredrWriteDomainCredentials "
			"CredrReadDomainCredentials CredrDelete CredrGetTargetInfo CredrProfileLoaded LsarLookupNames3 "
			"CredrGetSessionTypes LsarRegisterAuditEvent LsarGenAuditEvent LsarUnregisterAuditEvent "
			"LsarQueryForestTrustInformation LsarSetForestTrustInformation - LsarLookupSids3 LsarLookupNames4 "
			"LsarOpenPolicySce LsarAdtRegisterSecurityEventSource LsarAdtUnregisterSecurityEventSource "
			"LsarAdtReportSecurityEvent CredrFindBestCredential LsarSetAuditPolicy LsarQueryAuditPolicy "
			"LsarEnumerateAuditPolicy LsarEnumerateAuditCategories LsarEnumerateAuditSubCategories "
			"LsarLookupAuditCategoryName LsarLookupAuditSubCategoryName LsarSetAuditSecurity LsarQueryAuditSecurity "
			"CredrReadByTokenHandle CredrRestoreCredentials CredrBackupCredentials LsarManageSidNameMapping "
			"CredrProfileUnloaded - - - - - LsarEfsGetSmartcardCredentials LsarAuditSetGlobalSacl LsarAuditQueryGlobalSacl "
			"CredrProfileLoadedEx LsarInteractiveSessionIsLoggedOff LsarConfigureAutoLogonCredentials "
			"LsarGetDeviceRegistrationInfo - LsarProfileDeleted - - LsarValidateProcUniqueLuid LsarIsArsoAllowedByPolicy "
			"LsarIsArsoAllowedByConsent LsarEnableArsoConsent LsarDisableArsoConsent LsarIsUserArsoAllowed "
			"LsarIsUserArsoEnabled LsarEnableUserArso LsarDisableUserArso LsarConfigureUserArso LsarGetInprocDispatchTable "
			"LsarSetSharedUserSession LsarClearSharedUserSession - - LsarUpdateUserTokenSessionId "
			"LsarCreateTrustedDomainEx3 LsarOpenPolicy3 LsarInvokeTrustScanner LsarQueryForestTrustInformation2 "
			"LsarSetForestTrustInformation2 LsarIsCredentialGuardRunning LsarOpenPolicyWithCreds LsarOpenSecret2 "
			"LsarCreateSecret2 LsarSetSecret2 LsarQuerySecret2 LsarStorePrivateData2 LsarRetrievePrivateData2 "
			"LsarSetLocalSystemAccess LsarQueryLocalSystemAccess LsarQueryLocalSystemAccessAll "
			"LsarPurgeLocalSystemAccessTable LsarCreateAgentAccount LsarRetrieveAgentLogonCredential "
			"LsarRetrieveCloudAgentLogonCredential LsarEnumerateAgentAccounts LsarDeleteAgentAccount LsarGetAgentOwner "
			"LsarUpdateEnvironmentBlock LsarRetrieveCloudAgentTokenBindingKeys" },
		{ "mgmt",
			"rpc_mgmt_inq_if_ids rpc_mgmt_inq_stats rpc_mgmt_is_server_listening rpc_mgmt_stop_server_listening "
			"rpc_mgmt_inq_princ_name" },
		{ "netlogon",
			"NetrLogonUasLogon NetrLogonUasLogoff NetrLogonSamLogon NetrLogonSamLogoff NetrServerReqChallenge "
			"NetrServerAuthenticate NetrServerPasswordSet NetrDatabaseDeltas NetrDatabaseSync NetrAccountDeltas "
			"NetrAccountSync NetrGetDCName NetrLogonControl NetrGetAnyDCName NetrLogonControl2 NetrServerAuthenticate2 "
			"NetrDatabaseSync2 NetrDatabaseRedo NetrLogonControl2Ex NetrEnumerateTrustedDomains DsrGetDcName "
			"NetrLogonGetCapabilities NetrLogonSetServiceBits NetrLogonGetTrustRid NetrLogonComputeServerDigest "
			"NetrLogonComputeClientDigest NetrServerAuthenticate3 DsrGetDcNameEx DsrGetSiteName NetrLogonGetDomainInfo "
			"NetrServerPasswordSet2 NetrServerPasswordGet NetrLogonSendToSam DsrAddressToSiteNamesW DsrGetDcNameEx2 "
			"NetrLogonGetTimeServiceParentDomain NetrEnumerateTrustedDomainsEx DsrAddressToSiteNamesExW "
			"DsrGetDcSiteCoverageW NetrLogonSamLogonEx DsrEnumerateDomainTrusts DsrDeregisterDnsHostRecords "
			"NetrServerTrustPasswordsGet DsrGetForestTrustInformation NetrGetForestTrustInformation "
			"NetrLogonSamLogonWithFlags NetrServerGetTrustInfo NetrCollectPerfData DsrUpdateReadOnlyServerDnsRecords "
			"NetrChainSetClientAttributes NetrAddServiceAccount NetrRemoveServiceAccount NetrEnumerateServiceAccounts "
			"NetrQueryServiceAccount NetrChainSetClientAttributes2 NetrLogonComputeClientSignature "
			"NetrLogonComputeServerSignature NetrExtendMachinePasswordExpirationTimeout NetrQuerySecureChannelDCInfo "
			"NetrServerAuthenticateKerberos NetrServerRequestLocatorNameMappingsUpdate NetrServerQueryLocatorNameMappings" },
		{ "samr",
			"SamrConnect SamrCloseHandle SamrSetSecurityObject SamrQuerySecurityObject SamrShutdownSamServer "
			"SamrLookupDomainInSamServer SamrEnumerateDomainsInSamServer SamrOpenDomain SamrQueryInformationDomain "
			"SamrSetInformationDomain SamrCreateGroupInDomain SamrEnumerateGroupsInDomain SamrCreateUserInDomain "
			"SamrEnumerateUsersInDomain SamrCreateAliasInDomain SamrEnumerateAliasesInDomain SamrGetAliasMembership "
			"SamrLookupNamesInDomain SamrLookupIdsInDomain SamrOpenGroup SamrQueryInformationGroup SamrSetInformationGroup "
			"SamrAddMemberToGroup SamrDeleteGroup SamrRemoveMemberFromGroup SamrGetMembersInGroup "
			"SamrSetMemberAttributesOfGroup SamrOpenAlias SamrQueryInformationAlias SamrSetInformationAlias "
			"SamrDeleteAlias SamrAddMemberToAlias SamrRemoveMemberFromAlias SamrGetMembersInAlias SamrOpenUser "
			"SamrDeleteUser SamrQueryInformationUser SamrSetInformationUser SamrChangePasswordUser SamrGetGroupsForUser "
			"SamrQueryDisplayInformation SamrGetDisplayEnumerationIndex SamrTestPrivateFunctionsDomain "
			"SamrTestPrivateFunctionsUser SamrGetUserDomainPasswordInformation SamrRemoveMemberFromForeignDomain "
			"SamrQueryInformationDomain2 SamrQueryInformationUser2 SamrQueryDisplayInformation2 "
			"SamrGetDisplayEnumerationIndex2 SamrCreateUser2InDomain SamrQueryDisplayInformation3 "
			"SamrAddMultipleMembersToAlias SamrRemoveMultipleMembersFromAlias SamrOemChangePasswordUser2 "
			"SamrUnicodeChangePasswordUser2 SamrGetDomainPasswordInformation SamrConnect2 SamrSetInformationUser2 "
			"SamrSetBootKeyInformation SamrGetBootKeyInformation SamrConnect3 SamrConnect4 SamrUnicodeChangePasswordUser3 "
			"SamrConnect5 SamrRidToSid SamrSetDSRMPassword SamrValidatePassword SamrQueryLocalizableAccountsInDomain "
			"SamrPerformGenericOperation SamrSyncDSRMPasswordFromAccount SamrLookupNamesInDomain2 "
			"SamrEnumerateUsersInDomain2 SamrUnicodeChangePasswordUser4 SamrValidateComputerAccountReuseAttempt "
			"SamrQueryLapsManagedAccount SamrLapsSysprepCleanup SamrAccountIsDelegatedManagedServiceAccount "
			"SamrFindOrCreateShadowAdminAccount SamrIsShadowAdminAccount SamrQueryInformationUserCloud "
			"SamrQueryDisplayInformationCloud" },
		{ "spoolss",
			"RpcEnumPrinters RpcOpenPrinter RpcSetJob RpcGetJob RpcEnumJobs RpcAddPrinter RpcDeletePrinter RpcSetPrinter "
			"RpcGetPrinter RpcAddPrinterDriver RpcEnumPrinterDrivers RpcGetPrinterDriver RpcGetPrinterDriverDirectory "
			"RpcDeletePrinterDriver RpcAddPrintProcessor RpcEnumPrintProcessors RpcGetPrintProcessorDirectory "
			"RpcStartDocPrinter RpcStartPagePrinter RpcWritePrinter RpcEndPagePrinter RpcAbortPrinter RpcReadPrinter "
			"RpcEndDocPrinter RpcAddJob RpcScheduleJob RpcGetPrinterData RpcSetPrinterData RpcWaitForPrinterChange "
			"RpcClosePrinter RpcAddForm RpcDeleteForm RpcGetForm RpcSetForm RpcEnumForms RpcEnumPorts RpcEnumMonitors "
			"RpcAddPort RpcConfigurePort RpcDeletePort RpcCreatePrinterIC RpcPlayGdiScriptOnPrinterIC RpcDeletePrinterIC "
			"RpcAddPrinterConnection RpcDeletePrinterConnection RpcPrinterMessageBox RpcAddMonitor RpcDeleteMonitor "
			"RpcDeletePrintProcessor RpcAddPrintProvidor RpcDeletePrintProvidor RpcEnumPrintProcessorDatatypes "
			"RpcResetPrinter RpcGetPrinterDriver2 RpcClientFindFirstPrinterChangeNotification "
			"RpcFindNextPrinterChangeNotification RpcFindClosePrinterChangeNotification "
			"RpcRouterFindFirstPrinterChangeNotificationOld RpcReplyOpenPrinter RpcRouterReplyPrinter RpcReplyClosePrinter "
			"RpcAddPortEx RpcRemoteFindFirstPrinterChangeNotification RpcSetAllocFailCount RpcResetPrinterEx "
			"RpcRemoteFindFirstPrinterChangeNotificationEx RpcRouterReplyPrinterEx "
			"RpcRouterRefreshPrinterChangeNotification RpcSpoolerInit RpcOpenPrinterEx RpcAddPrinterEx RpcSetPort "
			"RpcEnumPrinterData RpcDeletePrinterData RpcClusterSplOpen RpcClusterSplClose RpcClusterSplIsAlive "
			"RpcSetPrinterDataEx RpcGetPrinterDataEx RpcEnumPrinterDataEx RpcEnumPrinterKey RpcDeletePrinterDataEx "
			"RpcDeletePrinterKey RpcSeekPrinter RpcDeletePrinterDriverEx RpcAddPerMachineConnection "
			"RpcDeletePerMachineConnection RpcEnumPerMachineConnections RpcXcvData RpcAddPrinterDriverEx RpcSplOpenPrinter "
			"RpcGetSpoolFileInfo RpcCommitSpoolData RpcGetSpoolFileInfo2 RpcCommitSpoolData2 RpcCloseSpoolFileHandle "
			"RpcFlushPrinter RpcSendRecvBidiData RpcAddDriverCatalog RpcAddPrinterConnection2 "
			"RpcInstallPrinterDriverFromPackage RpcUploadPrinterDriverPackage RpcGetCorePrinterDrivers "
			"RpcCorePrinterDriverInstalled RpcGetPrinterDriverPackagePath RpcDeletePrinterDriverPackage "
			"RpcFindCompatibleDriver RpcReportJobProcessingProgress RpcSetSpoolerPolicy RpcInternalGetPrinterDriver "
			"RpcGetJobNamedPropertyValue RpcSetJobNamedProperty RpcDeleteJobNamedProperty RpcEnumJobNamedProperties "
			"RpcCreateAppSandbox RpcGetUserPropertyBag RpcLogJobInfoForBranchOffice RpcRegeneratePrintDeviceCapabilities "
			"RpcPrintSupportOperation RpcIppCreateJobOnPrinter RpcIppGetJobAttributes RpcIppSetJobAttributes "
			"RpcIppGetPrinterAttributes RpcIppSetPrinterAttributes RpcIppCreateJobOnPrinterWithAttributes "
			"RpcInstallPrinterDriverPackageFromConnection" },
		{ "srvsvc",
			"NetrCharDevEnum NetrCharDevGetInfo NetrCharDevControl NetrCharDevQEnum NetrCharDevQGetInfo "
			"NetrCharDevQSetInfo NetrCharDevQPurge NetrCharDevQPurgeSelf NetrConnectionEnum NetrFileEnum NetrFileGetInfo "
			"NetrFileClose NetrSessionEnum NetrSessionDel NetrShareAdd NetrShareEnum NetrShareGetInfo NetrShareSetInfo "
			"NetrShareDel NetrShareDelSticky NetrShareCheck NetrServerGetInfo NetrServerSetInfo NetrServerDiskEnum "
			"NetrServerStatisticsGet NetrServerTransportAdd NetrServerTransportEnum NetrServerTransportDel NetrRemoteTOD "
			"I_NetrServerSetServiceBits NetprPathType NetprPathCanonicalize NetprPathCompare NetprNameValidate "
			"NetprNameCanonicalize NetprNameCompare NetrShareEnumSticky NetrShareDelStart NetrShareDelCommit "
			"NetrpGetFileSecurity NetrpSetFileSecurity NetrServerTransportAddEx I_NetrServerSetServiceBitsEx "
			"NetrDfsGetVersion NetrDfsCreateLocalPartition NetrDfsDeleteLocalPartition NetrDfsSetLocalVolumeState "
			"Opnum47NotUsedOnWire NetrDfsCreateExitPoint NetrDfsDeleteExitPoint NetrDfsModifyPrefix NetrDfsFixLocalVolume "
			"NetrDfsManagerReportSiteInfo NetrServerTransportDelEx NetrServerAliasAdd NetrServerAliasEnum "
			"NetrServerAliasDel NetrShareDelEx LocalrSessionEnum LocalrSessionGetInfo LocalrSessionDel LocalrFileEnum "
			"LocalrFileGetInfo LocalrFileClose LocalrShareEnum LocalrShareGetInfo LocalrShareSetInfo LocalrShareAdd "
			"LocalrShareDelEx LocalrAliasGet LocalrServerCertificateMappingGet LocalrServerCertificateMappingAdd "
			"LocalrServerCertificateMappingEnum LocalrServerCertificateMappingRemove LocalrServerCertificateMappingModify "
			"- - LocalrServerSmbDirectModuleControl LocalrServerCertificateMappingAceAdd "
			"LocalrServerCertificateMappingAceRemove LocalrServerAlternativePortUpdate LocalrServerAlternativePortEnum" },
		{ "svcctl",
			"RCloseServiceHandle RControlService RDeleteService RLockServiceDatabase RQueryServiceObjectSecurity "
			"RSetServiceObjectSecurity RQueryServiceStatus RSetServiceStatus RUnlockServiceDatabase "
			"RNotifyBootConfigStatus RI_ScSetServiceBitsW RChangeServiceConfigW RCreateServiceW REnumDependentServicesW "
			"REnumServicesStatusW ROpenSCManagerW ROpenServiceW RQueryServiceConfigW RQueryServiceLockStatusW "
			"RStartServiceW RGetServiceDisplayNameW RGetServiceKeyNameW RI_ScSetServiceBitsA RChangeServiceConfigA "
			"RCreateServiceA REnumDependentServicesA REnumServicesStatusA ROpenSCManagerA ROpenServiceA "
			"RQueryServiceConfigA RQueryServiceLockStatusA RStartServiceA RGetServiceDisplayNameA RGetServiceKeyNameA "
			"RI_ScGetCurrentGroupStateW REnumServiceGroupW RChangeServiceConfig2A RChangeServiceConfig2W "
			"RQueryServiceConfig2A RQueryServiceConfig2W RQueryServiceStatusEx REnumServicesStatusExA "
			"REnumServicesStatusExW RI_ScBroadcastServiceControlMessage RCreateServiceWOW64A RCreateServiceWOW64W "
			"RI_ScQueryServiceTagInfo RNotifyServiceStatusChange RGetNotifyResults RCloseNotifyHandle RControlServiceExA "
			"RControlServiceExW RI_ScSendPnPMessage RI_ScValidatePnPService RI_ScOpenServiceStatusHandle "
			"RI_ScQueryServiceConfig RQueryServiceConfigEx RI_ScRegisterPreshutdownRestart RI_ScReparseServiceDatabase "
			"RQueryUserServiceName RCreateWowService RGetServiceRegistryStateKey RGetServiceDirectory "
			"RGetServiceProcessToken ROpenSCManager2 RGetSharedServiceRegistryStateKey RGetSharedServiceDirectory "
			"RReparseServiceConfig RScSendSynchronousPowerMessage" },
		{ "winreg",
			"OpenClassesRoot OpenCurrentUser OpenLocalMachine OpenPerformanceData OpenUsers BaseRegCloseKey "
			"BaseRegCreateKey BaseRegDeleteKey BaseRegDeleteValue BaseRegEnumKey BaseRegEnumValue BaseRegFlushKey "
			"BaseRegGetKeySecurity BaseRegLoadKey BaseRegNotifyChangeKeyValue BaseRegOpenKey BaseRegQueryInfoKey "
			"BaseRegQueryValue BaseRegReplaceKey BaseRegRestoreKey BaseRegSaveKey BaseRegSetKeySecurity BaseRegSetValue "
			"BaseRegUnLoadKey BaseInitiateSystemShutdown BaseAbortSystemShutdown BaseRegGetVersion OpenCurrentConfig "
			"BaseInitiateSystemShutdownEx BaseRegQueryMultipleValues - BaseRegSaveKeyEx OpenPerformanceText "
			"OpenPerformanceNlsText BaseRegQueryMultipleValues2 BaseRegDeleteKeyEx" },
		{ "wkssvc",
			"NetrWkstaGetInfo NetrWkstaSetInfo NetrWkstaUserEnum NetrWkstaUserGetInfo NetrWkstaUserSetInfo "
			"NetrWkstaTransportEnum NetrWkstaTransportAdd NetrWkstaTransportDel NetrUseAdd NetrUseGetInfo NetrUseDel "
			"NetrUseEnum - NetrWorkstationStatisticsGet - - - - - - NetrGetJoinInformation - NetrJoinDomain2 "
			"NetrUnjoinDomain2 NetrRenameMachineInDomain2 NetrValidateName2 NetrGetJoinableOUs2 "
			"NetrAddAlternateComputerName NetrRemoveAlternateComputerName NetrSetPrimaryComputerName "
			"NetrEnumerateComputerNames NetrJoinDomain3 NetrUnjoinDomain3 NetrRenameMachineInDomain3 NetrValidateName3 "
			"NetrGetJoinableOUs3 NetrAddAlternateComputerName2 NetrRemoveAlternateComputerName2 "
			"NetrSetPrimaryComputerName2 NetpWkstarClientCertificateMappingGet NetpWkstarClientCertificateMappingAdd "
			"NetpWkstarClientCertificateMappingEnum NetpWkstarClientCertificateMappingRemove "
			"NetpWkstarClientCertificateMappingModify NetpWkstarClientSmbDirectModuleControl "
			"NetpWkstarClientGetSmbClientCapabilities" },
	};
	auto name = KnownRpcInterface(uuid);
	if (name.empty())
		return {};
	for (auto const& [iface, names] : procedures) {
		if (name != iface)
			continue;
		std::string_view text = names;
		for (uint32_t i = 0; !text.empty(); i++) {
			auto end = text.find(' ');
			auto word = text.substr(0, end);
			if (i == opnum)
				return word == "-" ? std::string() : std::string(word);
			text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
		}
	}
	return {};
}
std::string RpcBaseTypeName(uint8_t type) {
	switch (type) {
		case 0x01: return "unsigned char";		// FC_BYTE
		case 0x02: return "char";				// FC_CHAR
		case 0x03: return "small";				// FC_SMALL
		case 0x04: return "unsigned small";		// FC_USMALL
		case 0x05: return "wchar_t";			// FC_WCHAR
		case 0x06: return "short";
		case 0x07: return "unsigned short";
		case 0x08: return "long";
		case 0x09: return "unsigned long";
		case 0x0A: return "float";
		case 0x0B: return "hyper";
		case 0x0C: return "double";
		case 0x0D: return "enum16";
		case 0x0E: return "enum32";
		case 0x0F: return "ignore";
		case 0x10: return "error_status_t";
		case 0xB8: return "__int3264";
		case 0xB9: return "unsigned __int3264";
	}
	return {};
}

std::string RpcParameterText(RpcParameter const& p) {
	bool in = (p.Attributes & RpcParameter::IsIn) != 0, out = (p.Attributes & RpcParameter::IsOut) != 0;
	return std::string(in && out ? "[in, out] " : out ? "[out] " : in ? "[in] " : "") + p.Type;
}

std::string RpcRoutineDeclaration(RpcProcedure const& procedure, std::string const& name) {
	if (!procedure.Described)
		return {};
	// a base type as C declares it (of its size in memory: an enum16 is an int)
	auto cType = [](uint8_t base) -> std::string {
		switch (base) {
			case 0x01: case 0x04: return "unsigned char";
			case 0x02: case 0x03: return "char";
			case 0x05: return "wchar_t";
			case 0x06: return "short";
			case 0x07: return "unsigned short";
			case 0x08: case 0x0D: case 0x0E: return "long";
			case 0x09: case 0x10: return "unsigned long";
			case 0x0A: return "float";
			case 0x0B: case 0xB8: return "__int64";
			case 0xB9: return "unsigned __int64";
			case 0x0C: return "double";
			case 0x0F: return "void *";
		}
		return {};
	};
	auto typeOf = [&](RpcParameter const& p) -> std::string {
		if (p.Handle)
			return "void *";
		if (!(p.Attributes & RpcParameter::IsBasetype)) {
			// (a base type of a range, [range(0, 256K)] DWORD, is described by its type, not as a base type)
			for (uint8_t base = 0x01; base <= 0x10; base++)
				if (p.Type == RpcBaseTypeName(base) && !cType(base).empty())
					return cType(base);
			return "void *";	// (a pointer, a handle; a structure by value is in one register or slot, as a pointer is)
		}
		auto type = cType(p.BaseType);
		if (type.empty())
			return {};
		return p.Attributes & RpcParameter::IsSimpleRef ? type + " *" : type;
	};
	// the parameters by their places (8 bytes each): a place without one (that the format does not say) is a pointer's
	std::vector<std::string> places;
	for (auto const& p : procedure.Parameters) {
		auto index = p.StackOffset / 8;
		if (p.StackOffset % 8 || index > 64)
			return {};
		auto type = typeOf(p);
		if (type.empty())
			return {};
		if (places.size() <= index)
			places.resize(index + 1);
		places[index] = type + " " + (p.Handle ? std::string("Binding") : "Arg" + std::to_string(index + 1));
	}
	// (an asynchronous one's first: its PRPC_ASYNC_STATE)
	if (procedure.Async && !places.empty() && places[0].empty())
		places[0] = "void * AsyncHandle";
	std::string result = "void";
	if (procedure.HasResult) {
		result = typeOf(procedure.Result);
		if (result.empty())
			return {};
	}
	std::string text = result + " __cdecl " + name + "(";
	for (size_t i = 0; i < places.size(); i++)
		text += (i ? ", " : "") + (places[i].empty() ? "void * Arg" + std::to_string(i + 1) : places[i]);
	return text + ")";
}
