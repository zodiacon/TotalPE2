#include "pch.h"
#include "Driver.h"
#include <map>
#include <regex>
#include <set>

bool IsDriver(PEFile const& pe) {
	if (!pe || !pe.GetNTHeader())
		return false;
	auto const& nt = *pe.GetNTHeader();
	auto subsystem = pe.GetFileInfo()->IsPE64 ? nt.NTHdr64.OptionalHeader.Subsystem : nt.NTHdr32.OptionalHeader.Subsystem;
	if (subsystem != IMAGE_SUBSYSTEM_NATIVE)
		return false;
	if (auto imports = pe.GetImport())
		for (auto const& mod : *imports) {
			auto name = mod.ModuleName;
			for (auto& ch : name)
				ch = (char)tolower((unsigned char)ch);
			if (name == "ntoskrnl.exe" || name == "ntkrnlpa.exe" || name == "hal.dll" || name == "wdfldr.sys" || name == "fltmgr.sys")
				return true;
		}
	return false;
}

PCWSTR MajorFunctionName(int major) {
	static PCWSTR const names[DriverInfo::MajorCount] = {
		L"IRP_MJ_CREATE", L"IRP_MJ_CREATE_NAMED_PIPE", L"IRP_MJ_CLOSE", L"IRP_MJ_READ", L"IRP_MJ_WRITE",
		L"IRP_MJ_QUERY_INFORMATION", L"IRP_MJ_SET_INFORMATION", L"IRP_MJ_QUERY_EA", L"IRP_MJ_SET_EA", L"IRP_MJ_FLUSH_BUFFERS",
		L"IRP_MJ_QUERY_VOLUME_INFORMATION", L"IRP_MJ_SET_VOLUME_INFORMATION", L"IRP_MJ_DIRECTORY_CONTROL",
		L"IRP_MJ_FILE_SYSTEM_CONTROL", L"IRP_MJ_DEVICE_CONTROL", L"IRP_MJ_INTERNAL_DEVICE_CONTROL", L"IRP_MJ_SHUTDOWN",
		L"IRP_MJ_LOCK_CONTROL", L"IRP_MJ_CLEANUP", L"IRP_MJ_CREATE_MAILSLOT", L"IRP_MJ_QUERY_SECURITY", L"IRP_MJ_SET_SECURITY",
		L"IRP_MJ_POWER", L"IRP_MJ_SYSTEM_CONTROL", L"IRP_MJ_DEVICE_CHANGE", L"IRP_MJ_QUERY_QUOTA", L"IRP_MJ_SET_QUOTA",
		L"IRP_MJ_PNP",
	};
	return major >= 0 && major < DriverInfo::MajorCount ? names[major] : L"";
}

std::wstring DispatchName(std::vector<int> const& majors) {
	if (majors.empty())
		return {};
	if (majors.size() > 3)
		return L"DispatchMultiple";
	// IRP_MJ_DEVICE_CONTROL -> DeviceControl
	std::wstring name = L"Dispatch";
	for (auto major : majors) {
		std::wstring text = MajorFunctionName(major) + 7;
		bool upper = true;
		for (auto ch : text) {
			if (ch == L'_') {
				upper = true;
				continue;
			}
			name += upper ? ch : (wchar_t)towlower(ch);
			upper = false;
		}
	}
	return name;
}

std::wstring FilterOperationName(int operation) {
	if (operation >= 0 && operation < DriverInfo::MajorCount)
		return MajorFunctionName(operation);
	// (the filter manager's own: negative, as a UCHAR)
	static std::map<int, PCWSTR> const names = {
		{ 0xFF, L"IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION" }, { 0xFE, L"IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION" },
		{ 0xFD, L"IRP_MJ_ACQUIRE_FOR_MOD_WRITE" }, { 0xFC, L"IRP_MJ_RELEASE_FOR_MOD_WRITE" },
		{ 0xFB, L"IRP_MJ_ACQUIRE_FOR_CC_FLUSH" }, { 0xFA, L"IRP_MJ_RELEASE_FOR_CC_FLUSH" }, { 0xF9, L"IRP_MJ_QUERY_OPEN" },
		{ 0xF3, L"IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE" }, { 0xF2, L"IRP_MJ_NETWORK_QUERY_OPEN" }, { 0xF1, L"IRP_MJ_MDL_READ" },
		{ 0xF0, L"IRP_MJ_MDL_READ_COMPLETE" }, { 0xEF, L"IRP_MJ_PREPARE_MDL_WRITE" }, { 0xEE, L"IRP_MJ_MDL_WRITE_COMPLETE" },
		{ 0xED, L"IRP_MJ_VOLUME_MOUNT" }, { 0xEC, L"IRP_MJ_VOLUME_DISMOUNT" },
	};
	auto it = names.find(operation);
	return it != names.end() ? it->second : std::format(L"0x{:X}", operation);
}

std::wstring FilterCallbackName(int operation, bool post) {
	// IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION -> AcquireForSectionSynchronization
	auto text = FilterOperationName(operation);
	std::wstring name = post ? L"Post" : L"Pre";
	if (!text.starts_with(L"IRP_MJ_"))
		return name + L"Operation";
	bool upper = true;
	for (auto ch : text.substr(7)) {
		if (ch == L'_') {
			upper = true;
			continue;
		}
		name += upper ? ch : (wchar_t)towlower(ch);
		upper = false;
	}
	return name;
}

namespace {
	// FLT_REGISTRATION: Size, Version, Flags, ContextRegistration, OperationRegistration, then the callbacks
	PCWSTR const RegistrationCallbacks[] = {
		L"FilterUnloadCallback", L"InstanceSetupCallback", L"InstanceQueryTeardownCallback", L"InstanceTeardownStartCallback",
		L"InstanceTeardownCompleteCallback", L"GenerateFileNameCallback", L"NormalizeNameComponentCallback",
		L"NormalizeContextCleanupCallback", L"TransactionNotificationCallback", L"NormalizeNameComponentExCallback",
		L"SectionNotificationCallback",
	};
	uint32_t OperationsOffset(bool pe64) { return pe64 ? 0x10 : 0xC; }
	uint32_t CallbacksOffset(bool pe64) { return pe64 ? 0x18 : 0x10; }

	bool IsRegistrationHeader(uint16_t size, uint16_t version, bool pe64) {
		uint32_t pointer = pe64 ? 8 : 4;
		return (version >> 8) == 2 && size >= CallbacksOffset(pe64) + pointer && size <= CallbacksOffset(pe64) + 16 * pointer &&
			(size - CallbacksOffset(pe64)) % pointer == 0;
	}

	uint64_t PointerAt(std::vector<uint8_t> const& bytes, size_t offset, bool pe64) {
		uint64_t value = 0;
		if (offset + (pe64 ? 8 : 4) <= bytes.size())
			memcpy(&value, bytes.data() + offset, pe64 ? 8 : 4);
		return value;
	}

	// FLT_OPERATION_REGISTRATION[]: MajorFunction (a UCHAR), Flags, PreOperation, PostOperation, Reserved; up to
	// IRP_MJ_OPERATION_END (0x80). False if it is not one.
	bool ReadOperations(DriverImageReader const& read, uint64_t base, uint64_t va, bool pe64, std::vector<DriverInfo::Callback>& callbacks) {
		uint32_t pointer = pe64 ? 8 : 4, size = pe64 ? 0x20 : 0x14;
		std::vector<DriverInfo::Callback> found;
		for (int i = 0; i < 64; i++) {
			std::vector<uint8_t> entry(size);
			if (!read(va + i * size, entry.data(), size))
				return false;
			int operation = entry[0];
			if (operation == 0x80) {
				callbacks.insert(callbacks.end(), found.begin(), found.end());
				return true;
			}
			// (the flags are a few bits, the reserved pointer is null)
			if ((operation >= DriverInfo::MajorCount && operation < 0xEC) || entry[1] || entry[2] || entry[3] || entry[5] || entry[6] ||
				entry[7] || PointerAt(entry, 8 + 2 * pointer, pe64))
				return false;
			for (bool post : { false, true })
				if (auto callback = PointerAt(entry, 8 + (post ? pointer : 0), pe64)) {
					uint8_t code;
					if (callback <= base || !read(callback, &code, 1))
						return false;	// (not code of the image: not operations)
					found.push_back({ FilterOperationName(operation) + (post ? L" (post)" : L" (pre)"), (DWORD)(callback - base), operation, post });
				}
		}
		return false;
	}
}

bool ReadFilterRegistration(DriverImageReader const& read, uint64_t base, uint64_t va, bool pe64, DriverInfo& info) {
	uint8_t header[4];
	if (!read || !read(va, header, sizeof(header)))
		return false;
	uint16_t size = header[0] | header[1] << 8, version = header[2] | header[3] << 8;
	if (!IsRegistrationHeader(size, version, pe64))
		return false;
	std::vector<uint8_t> bytes(size);
	if (!read(va, bytes.data(), size))
		return false;
	std::vector<DriverInfo::Callback> callbacks, operations;
	uint32_t pointer = pe64 ? 8 : 4;
	for (size_t i = 0; i < _countof(RegistrationCallbacks) && CallbacksOffset(pe64) + (i + 1) * pointer <= size; i++)
		if (auto callback = PointerAt(bytes, CallbacksOffset(pe64) + i * pointer, pe64)) {
			uint8_t code;
			if (callback <= base || !read(callback, &code, 1))
				return false;
			callbacks.push_back({ RegistrationCallbacks[i], (DWORD)(callback - base) });
		}
	// (operations that are not there in the file, filled when it runs, are not known)
	if (auto array = PointerAt(bytes, OperationsOffset(pe64), pe64); array && !ReadOperations(read, base, array, pe64, operations))
		operations.clear();
	info.Callbacks.insert(info.Callbacks.end(), callbacks.begin(), callbacks.end());
	info.Callbacks.insert(info.Callbacks.end(), operations.begin(), operations.end());
	return true;
}

std::vector<uint64_t> FindFilterRegistrations(PEFile const& pe, DriverImageReader const& read) {
	std::vector<uint64_t> found;
	auto sections = pe.GetSecHeaders();
	if (!sections)
		return found;
	bool pe64 = pe.GetFileInfo()->IsPE64;
	auto base = pe.GetImageBase();
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if ((h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto size = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		auto offset = pe.GetOffsetFromRVA(h.VirtualAddress);
		if (!offset || offset + size > pe.GetFileSize())
			continue;
		auto data = pe.GetSpan((uint32_t)offset, size);
		auto bytes = (uint8_t const*)data.data();
		for (size_t at = 0; at + 4 <= data.size(); at += pe64 ? 8 : 4) {
			uint16_t length = bytes[at] | bytes[at + 1] << 8, version = bytes[at + 2] | bytes[at + 3] << 8;
			if (!IsRegistrationHeader(length, version, pe64))
				continue;
			// (one whose operations are there)
			DriverInfo probe;
			if (ReadFilterRegistration(read, base, base + h.VirtualAddress + at, pe64, probe) && !probe.Callbacks.empty())
				found.push_back(base + h.VirtualAddress + at);
		}
	}
	return found;
}

std::vector<uint64_t> FindFilterOperations(PEFile const& pe, DriverImageReader const& read) {
	std::vector<uint64_t> found;
	auto sections = pe.GetSecHeaders();
	if (!sections)
		return found;
	bool pe64 = pe.GetFileInfo()->IsPE64;
	auto base = pe.GetImageBase();
	uint32_t pointer = pe64 ? 8 : 4, size = pe64 ? 0x20 : 0x14;
	auto inCode = [&](uint64_t va) {
		for (auto const& s : *sections) {
			auto const& h = s.SecHdr;
			if (va >= base + h.VirtualAddress && va < base + h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData))
				return (h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) != 0;
		}
		return false;
	};
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if ((h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto length = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		for (uint32_t at = 0; at + size <= length; at += pointer) {
			auto va = base + h.VirtualAddress + at;
			std::vector<DriverInfo::Callback> callbacks;
			if (!ReadOperations(read, base, va, pe64, callbacks) || callbacks.empty())
				continue;
			if (!std::ranges::all_of(callbacks, [&](auto const& c) { return inCode(base + c.Rva); }))
				continue;
			found.push_back(va);
			// (past its end: its entries after the first are arrays too)
			uint32_t entries = 0;
			for (uint8_t major = 0; entries < 64 && read(va + entries * size, &major, 1) && major != 0x80; entries++)
				;
			at += entries * size;
		}
	}
	return found;
}

std::wstring IoctlDeviceType(uint32_t code) {
	static std::map<uint32_t, PCWSTR> const types = {
		{ 0x01, L"FILE_DEVICE_BEEP" }, { 0x02, L"FILE_DEVICE_CD_ROM" }, { 0x07, L"FILE_DEVICE_DISK" },
		{ 0x09, L"FILE_DEVICE_FILE_SYSTEM" }, { 0x0B, L"FILE_DEVICE_KEYBOARD" }, { 0x0F, L"FILE_DEVICE_MOUSE" },
		{ 0x12, L"FILE_DEVICE_NETWORK" }, { 0x15, L"FILE_DEVICE_NULL" }, { 0x1B, L"FILE_DEVICE_SERIAL_PORT" },
		{ 0x22, L"FILE_DEVICE_UNKNOWN" }, { 0x23, L"FILE_DEVICE_VIDEO" }, { 0x2D, L"FILE_DEVICE_MASS_STORAGE" },
	};
	auto type = code >> 16;
	auto it = types.find(type);
	return it != types.end() ? it->second : std::format(L"0x{:X}", type);
}

std::wstring IoctlMethod(uint32_t code) {
	static PCWSTR const methods[] = { L"METHOD_BUFFERED", L"METHOD_IN_DIRECT", L"METHOD_OUT_DIRECT", L"METHOD_NEITHER" };
	return methods[code & 3];
}

std::wstring IoctlAccess(uint32_t code) {
	static PCWSTR const access[] = { L"FILE_ANY_ACCESS", L"FILE_READ_ACCESS", L"FILE_WRITE_ACCESS", L"FILE_READ_ACCESS | FILE_WRITE_ACCESS" };
	return access[(code >> 14) & 3];
}

std::wstring IoctlText(uint32_t code) {
	return std::format(L"CTL_CODE({}, 0x{:X}, {}, {})", IoctlDeviceType(code), (code >> 2) & 0xFFF, IoctlMethod(code), IoctlAccess(code));
}

bool LooksLikeIoctl(uint32_t value) {
	auto type = value >> 16;
	if (type == 0 || (type > 0x7F && type < 0x8000))
		return false;	// Microsoft's device types are below 0x80, those of others from 0x8000
	// (an NTSTATUS of no facility: STATUS_INVALID_DEVICE_REQUEST is 0xC0000010)
	if ((value >> 30) != 0 && ((value >> 16) & 0xFFF) == 0)
		return false;
	return true;
}

std::vector<uint32_t> IoctlsIn(std::string const& code) {
	static std::regex const pattern(R"((?:case\s+|[=!]=\s*)(0x[0-9a-fA-F]{5,8})\b)");
	std::vector<uint32_t> codes;
	std::set<uint32_t> seen;
	for (std::sregex_iterator it(code.begin(), code.end(), pattern), end; it != end; ++it) {
		auto value = (uint32_t)std::stoull((*it)[1].str(), nullptr, 16);
		if (LooksLikeIoctl(value) && seen.insert(value).second)
			codes.push_back(value);
	}
	return codes;
}

void AnalyzeDriver(GhidraDecompiler& decompiler, uint64_t base, uint64_t imageSize, DWORD entry, bool pe64, DriverInfo& info,
	std::vector<uint64_t> const& registers, std::vector<uint64_t> const& registrations, DriverImageReader const& read,
	std::vector<uint64_t> const& operations) {
	// the DRIVER_OBJECT: DriverStartIo, DriverUnload, then MajorFunction (pointers)
	uint32_t pointer = pe64 ? 8 : 4;
	int64_t startIo = pe64 ? 0x60 : 0x30, unload = pe64 ? 0x68 : 0x34, majors = pe64 ? 0x70 : 0x38;
	std::vector<GhidraDecompiler::ParameterStore> stores;
	if (!decompiler.ParameterStores(base + entry, 3, stores, info.Error)) {
		info.State = DriverInfo::States::Failed;
		return;
	}
	auto inImage = [&](uint64_t va) { return va > base && va - base < imageSize; };
	for (auto const& s : stores) {
		if (!inImage(s.Value))
			continue;
		auto rva = (DWORD)(s.Value - base);
		bool known = true;
		if (s.Indexed) {
			if (s.Offset == majors)
				info.Default = rva;
			else
				known = false;
		}
		else if (s.Offset == unload)
			info.Unload = rva;
		else if (s.Offset == startIo)
			info.StartIo = rva;
		else if (s.Offset >= majors && s.Offset < majors + DriverInfo::MajorCount * pointer && (s.Offset - majors) % pointer == 0)
			info.Major[(s.Offset - majors) / pointer] = rva;
		else
			known = false;
		if (known && !info.DriverEntry)
			info.DriverEntry = (DWORD)(s.Function - base);
	}
	for (auto& major : info.Major)
		if (!major)
			major = info.Default;

	// a minifilter: what its functions pass to FltRegisterFilter (its second parameter, the FLT_REGISTRATION)
	for (auto function : registers) {
		std::vector<GhidraDecompiler::CallArgument> arguments;
		std::string error;
		if (!decompiler.CallArguments(function, "FltRegisterFilter", 2, arguments, error))
			continue;
		for (auto const& argument : arguments) {
			if (argument.Constant) {
				ReadFilterRegistration(read, base, argument.Value, pe64, info);
				continue;
			}
			// one it fills on its stack: the callbacks it gives constants, and the operations (constant, or on its stack too)
			auto field = [&](int64_t offset, uint32_t size = 0) -> GhidraDecompiler::CallArgument::Field const* {
				for (auto const& f : argument.Fields)
					if (f.Offset == offset && (!size || f.Size == size))
						return &f;
				return nullptr;
			};
			auto header = field(0);
			if (!header || header->OnStack || header->Size < 4 ||
				!IsRegistrationHeader(uint16_t(header->Value), uint16_t(header->Value >> 16), pe64))
				continue;
			auto code = [&](GhidraDecompiler::CallArgument::Field const* f) {
				return f && !f->OnStack && inImage(f->Value) ? (DWORD)(f->Value - base) : 0;
			};
			for (size_t i = 0; i < _countof(RegistrationCallbacks); i++)
				if (auto callback = code(field(CallbacksOffset(pe64) + i * pointer, pointer)))
					info.Callbacks.push_back({ RegistrationCallbacks[i], callback });
			// FLT_OPERATION_REGISTRATION[] on the stack: MajorFunction (the low byte of what is written there), the callbacks;
			// how many entries there are from an offset (with a callback each)
			uint32_t size = pe64 ? 0x20 : 0x14;
			auto stackOperations = [&](int64_t start, std::vector<DriverInfo::Callback>* callbacks) {
				int count = 0;
				for (int k = 0; k < 64; k++) {
					auto entry = start + k * size;
					auto major = field(entry);
					if (!major || major->OnStack || (major->Value & 0xFF) == 0x80)
						break;
					int operation = major->Value & 0xFF;
					if (operation >= DriverInfo::MajorCount && operation < 0xEC)
						break;
					bool any = false;
					for (bool post : { false, true })
						if (auto callback = code(field(entry + 8 + (post ? pointer : 0), pointer))) {
							any = true;
							if (callbacks)
								callbacks->push_back({ FilterOperationName(operation) + (post ? L" (post)" : L" (pre)"), callback, operation, post });
						}
					if (!any)
						break;
					count++;
				}
				return count;
			};
			auto operations = field(OperationsOffset(pe64), pointer);
			if (operations && !operations->OnStack && inImage(operations->Value))
				ReadOperations(read, base, operations->Value, pe64, info.Callbacks);
			else if (operations && operations->OnStack)
				stackOperations((int64_t)operations->Value, &info.Callbacks);
			else {
				// (where the registration points to is not known: the longest run of entries near it, of two or more)
				int64_t best = 0;
				int most = 1;
				for (auto const& f : argument.Fields)
					if (f.Size == 1 && f.Offset > 0)
						if (auto count = stackOperations(f.Offset, nullptr); count > most)
							most = count, best = f.Offset;
				if (best)
					stackOperations(best, &info.Callbacks);
			}
		}
	}
	// (constant ones of the data, if the decompiler found none; and the constant operations, if none were found: those it
	// copies into its own when it runs)
	if (info.Callbacks.empty())
		for (auto va : registrations)
			ReadFilterRegistration(read, base, va, pe64, info);
	if (std::ranges::none_of(info.Callbacks, [](auto const& c) { return c.Operation >= 0; }))
		for (auto va : operations)
			ReadOperations(read, base, va, pe64, info.Callbacks);
	// (each once)
	std::vector<DriverInfo::Callback> unique;
	for (auto const& c : info.Callbacks)
		if (std::ranges::none_of(unique, [&](auto const& u) { return u.What == c.What && u.Rva == c.Rva; }))
			unique.push_back(c);
	info.Callbacks = std::move(unique);

	// the control codes: in the device control routines, or (if they pass them on) in the functions they call
	std::set<DWORD> done;
	for (auto major : { 14, 15 }) {
		auto handler = info.Major[major];
		if (!handler || handler == info.Default || !done.insert(handler).second)
			continue;
		std::string code, error;
		GhidraDecompiler::FunctionRefs calls;
		if (!decompiler.Decompile(base + handler, {}, code, error, &calls))
			continue;
		auto codes = IoctlsIn(code);
		for (auto value : codes)
			info.Ioctls.push_back({ value, handler });
		if (!codes.empty())
			continue;
		int count = 0;
		for (auto const& [name, address] : calls) {
			if (!inImage(address) || address == base + handler || !done.insert((DWORD)(address - base)).second || ++count > 8)
				continue;
			if (!decompiler.Decompile(address, {}, code, error))
				continue;
			for (auto value : IoctlsIn(code))
				info.Ioctls.push_back({ value, (DWORD)(address - base) });
		}
	}
	info.State = DriverInfo::States::Done;
}
