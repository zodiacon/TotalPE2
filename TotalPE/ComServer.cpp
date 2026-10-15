#include "pch.h"
#include "ComServer.h"
#include <FlowGraph.h>
#include <map>
#include <regex>
#include <set>

std::wstring GuidText(uint8_t const* b) {
	return std::format(L"{{{:02X}{:02X}{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
		b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

bool LooksLikeGuid(uint8_t const* b) {
	auto data1 = *(uint32_t const*)b;
	uint16_t data2 = b[4] | b[5] << 8, data3 = b[6] | b[7] << 8;
	// (Microsoft's old ones: {xxxxxxxx-xxxx-xxxx-C000-000000000046}; Microsoft's variant otherwise is mostly data that
	// looks like it)
	static uint8_t const ole[8] = { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 };
	if (data1 && memcmp(b + 8, ole, 8) == 0)
		return true;
	int version = data3 >> 12;
	bool rfc = (b[8] & 0xC0) == 0x80;
	if (!rfc || version < 1 || version > 5 || !data2)
		return false;
	// (not a run of the same byte, not mostly zeros: data that happens to have the bits)
	int zeros = 0;
	for (int i = 0; i < 16; i++)
		zeros += b[i] == 0;
	return zeros < 8 && data1 != 0xFFFFFFFF;
}

namespace {
	struct Image {
		PEFile const& Pe;
		uint64_t Base;
		bool Pe64;
		uint32_t Pointer;

		IMAGE_SECTION_HEADER const* SectionOf(DWORD rva) const {
			for (auto const& s : *Pe.GetSecHeaders()) {
				auto const& h = s.SecHdr;
				if (rva >= h.VirtualAddress && rva < h.VirtualAddress + std::max(h.Misc.VirtualSize, h.SizeOfRawData))
					return &h;
			}
			return nullptr;
		}
		bool InCode(DWORD rva) const {
			auto h = SectionOf(rva);
			return h && (h->Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE));
		}
		bool Writable(DWORD rva) const {
			auto h = SectionOf(rva);
			return h && (h->Characteristics & IMAGE_SCN_MEM_WRITE);
		}
		bool InData(DWORD rva) const {
			auto h = SectionOf(rva);
			return h && !(h->Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) && (h->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA);
		}
		bool Read(DWORD rva, void* buffer, size_t size) const {
			auto offset = Pe.GetOffsetFromRVA(rva);
			if (!offset || offset + size > Pe.GetFileSize())
				return false;
			auto span = Pe.GetSpan((uint32_t)offset, (uint32_t)size);
			if (span.size() != size)
				return false;
			memcpy(buffer, span.data(), size);
			return true;
		}
		// an RVA of a VA in the image (0 if it is not)
		DWORD RvaOf(uint64_t va) const {
			return va > Base && va - Base < 0x80000000ULL ? (DWORD)(va - Base) : 0;
		}
		bool IsGuid(DWORD rva) const {
			uint8_t bytes[16];
			return rva && InData(rva) && Read(rva, bytes, 16) && LooksLikeGuid(bytes);
		}
	};

}

void ComClassRegistry(std::wstring const& guid, std::wstring& name, std::wstring& server) {
	auto value = [](std::wstring const& key) {
		WCHAR text[1024];
		DWORD size = sizeof(text);
		if (::RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, text, &size) != ERROR_SUCCESS)
			return std::wstring();
		return std::wstring(text);
	};
	auto key = L"CLSID\\" + guid;
	name = value(key);
	server = value(key + L"\\InprocServer32");
	if (server.empty())
		server = value(key + L"\\LocalServer32");
}

namespace {
	// DllGetClassObject (its RVA), of a DLL server
	DWORD ClassObjectExport(PEFile const& pe) {
		if (auto exports = pe.GetExport())
			for (auto const& f : exports->Funcs)
				if (f.FuncName == "DllGetClassObject" && f.ForwarderName.empty())
					return f.FuncRVA;
		return 0;
	}

	// the slots of CoRegisterClassObject in the import address table (RVAs), of an EXE server
	std::vector<DWORD> RegisterSlots(PEFile const& pe) {
		std::vector<DWORD> slots;
		auto pointer = pe.GetFileInfo()->IsPE64 ? 8u : 4u;
		if (auto imports = pe.GetImport())
			for (auto const& mod : *imports) {
				DWORD slot = mod.ImportDesc.FirstThunk;
				for (auto const& fn : mod.ImportFunc) {
					if (fn.FuncName == "CoRegisterClassObject" && slot)
						slots.push_back(slot);
					slot += pointer;
				}
			}
		return slots;
	}
}

bool IsComServer(PEFile const& pe) {
	return ClassObjectExport(pe) || !RegisterSlots(pe).empty();
}

std::vector<ComServerClass> FindComServerClasses(PEFile const& pe, XrefMap const& xrefs, std::function<bool(DWORD rva)> const& isNotClass) {
	std::vector<ComServerClass> classes;
	auto sections = pe.GetSecHeaders();
	if (!sections)
		return classes;
	auto entry = ClassObjectExport(pe);
	auto registers = RegisterSlots(pe);
	if (!entry && registers.empty())
		return classes;
	Image image{ pe, pe.GetImageBase(), pe.GetFileInfo()->IsPE64, pe.GetFileInfo()->IsPE64 ? 8u : 4u };
	std::map<DWORD, size_t> found;		// the CLSIDs found: their entries
	auto add = [&](DWORD rva, PCWSTR source, DWORD creator) {
		if (!image.IsGuid(rva) || isNotClass(rva))
			return;
		// (a thunk of incremental linking: jmp function)
		uint8_t jmp[5];
		if (creator && image.Read(creator, jmp, 5) && jmp[0] == 0xE9)
			if (auto target = creator + 5 + *(int32_t const*)(jmp + 1); image.InCode(target))
				creator = target;
		// (once: the file may have copies of a CLSID, those of a debug build)
		uint8_t bytes[16], other[16];
		image.Read(rva, bytes, 16);
		for (auto& c : classes)
			if (image.Read(c.Guid, other, 16) && memcmp(bytes, other, 16) == 0) {
				if (!c.Creator && creator)
					c.Creator = creator;
				return;
			}
		auto [it, added] = found.try_emplace(rva, classes.size());
		if (added)
			classes.push_back({ rva, source, creator });
		else if (!classes[it->second].Creator && creator)
			classes[it->second].Creator = creator;
	};

	// what the code refers to: a GUID, or a table of its own that it looks the CLSID up in ({ &CLSID, ..., creator } each:
	// the GUIDs at a stride, each with a pointer to code: not a table of interfaces, QITAB's { &IID, offset })
	auto refer = [&](DWORD rva) {
		if (!rva || !image.InData(rva))
			return;
		if (image.IsGuid(rva)) {
			add(rva, L"DllGetClassObject", 0);
			return;
		}
		auto slot = [&](uint32_t index) -> DWORD {
			uint64_t value = 0;
			return image.Read(rva + index * image.Pointer, &value, image.Pointer) ? image.RvaOf(value) : 0;
		};
		auto isClass = [&](DWORD guid) { return guid && image.IsGuid(guid) && !isNotClass(guid); };
		if (!isClass(slot(0)))
			return;
		uint32_t stride = 0;
		for (uint32_t k = 1; k < 16 && !stride; k++)
			if (isClass(slot(k)))
				stride = k;
		if (!stride)
			stride = 16;	// (one entry)
		for (uint32_t entry = 0; entry < 512 * stride && isClass(slot(entry)); entry += stride) {
			// (the first function of the entry: the one that makes the class, before others: one that releases it)
			DWORD creator = 0;
			for (uint32_t k = 1; k < stride && k < 8 && !creator; k++)
				if (auto c = slot(entry + k); c && image.InCode(c))
					creator = c;
			if (!creator)
				break;
			add(slot(entry), L"class table (DllGetClassObject)", creator);
		}
	};

	// the code of DllGetClassObject and of the functions it calls (to a depth): the GUIDs it refers to (the operands of its
	// instructions: an address in memory, an immediate one)
	static std::wregex const immediate(LR"(0x[0-9a-fA-F]{5,16})");
	std::set<uint64_t> seen;
	std::vector<std::pair<uint64_t, int>> pending;
	if (entry)
		pending.emplace_back(image.Base + entry, 0);
	while (!pending.empty() && seen.size() < 64) {
		auto [va, depth] = pending.back();
		pending.pop_back();
		if (!seen.insert(va).second)
			continue;
		auto graph = BuildFlowGraph(pe, xrefs, va, 20000);
		std::set<uint64_t> starts;
		for (auto const& block : graph.Blocks)
			starts.insert(block.Start);
		for (auto const& block : graph.Blocks)
			for (auto const& ins : block.Code) {
				if (ins.Memory)
					refer(image.RvaOf(*ins.Memory));
				std::wstring operands(ins.Operands.begin(), ins.Operands.end());
				for (std::wsregex_iterator it(operands.begin(), operands.end(), immediate), end; it != end; ++it)
					refer(image.RvaOf(std::stoull(it->str(), nullptr, 16)));
				// (the functions it calls, and those it jumps to: a jump out of it, a tail call)
				bool call = ins.Mnemonic == "call" || ins.Mnemonic == "bl", jump = ins.Mnemonic == "jmp" || ins.Mnemonic == "b";
				if (ins.Branch && depth < 3 && (call || (jump && !starts.contains(*ins.Branch))))
					if (auto target = image.RvaOf(*ins.Branch); target && image.InCode(target))
						pending.emplace_back(*ins.Branch, depth + 1);
			}
	}

	// An EXE server registers its classes when it runs: the CLSIDs it passes CoRegisterClassObject (its first parameter, of
	// each call: rcx, or what x86 code pushes last; an address of the data, or of a register given one just before). Not the
	// other GUIDs of the function that calls it (those of the classes it uses: CoCreateInstance's).
	bool arm = pe.GetFileInfo()->IsPE64 && pe.GetNTHeader()->NTHdr64.FileHeader.Machine == IMAGE_FILE_MACHINE_ARM64;
	std::set<uint64_t> calls;
	for (auto slot : registers)
		for (auto const& ref : xrefs.To(image.Base + slot)) {
			// (through a thunk: jmp [slot])
			uint8_t code[2]{};
			if (image.Read(image.RvaOf(ref.From), code, 2) && code[0] == 0xFF && code[1] == 0x25)
				for (auto const& call : xrefs.To(ref.From))
					calls.insert(call.From);
			else
				calls.insert(ref.From);
		}
	std::set<uint64_t> functions;
	for (auto call : calls)
		if (auto start = FindFunctionStart(pe, xrefs, call); start && functions.size() < 32)
			functions.insert(start);
	auto immediateOf = [&](std::string const& operands) -> DWORD {
		std::smatch m;
		static std::regex const hex(R"(0x([0-9a-fA-F]{5,16}))");
		return std::regex_search(operands, m, hex) ? image.RvaOf(std::stoull(m[1].str(), nullptr, 16)) : 0;
	};
	// (a register's name in an operand; its 32-bit part names it too: ecx of rcx)
	auto registerOf = [](std::string text) {
		while (!text.empty() && text.front() == ' ')
			text.erase(0, 1);
		bool name = !text.empty() && text.find_first_of("[]+* ") == std::string::npos && !text.starts_with("0x") && !isdigit((unsigned char)text[0]);
		return name ? text : std::string();
	};
	auto sameRegister = [](std::string const& a, std::string const& b) {
		return a == b || (a.size() == 3 && b.size() == 3 && a[0] == 'r' && b[0] == 'e' && a.substr(1) == b.substr(1)) ||
			(a.size() == 3 && b.size() == 3 && a[0] == 'e' && b[0] == 'r' && a.substr(1) == b.substr(1));
	};
	for (auto function : functions) {
		auto graph = BuildFlowGraph(pe, xrefs, function, 20000);
		// what an instruction gives a register: a data address (lea, mov of an address, a pointer kept in the data), or the
		// register it copies; false if it does not write it
		auto gives = [&](FlowInstruction const& ins, std::string const& reg, DWORD& address, std::string& from) {
			auto comma = ins.Operands.find(',');
			if (comma == std::string::npos || !sameRegister(ins.Operands.substr(0, comma), reg))
				return false;
			address = 0;
			from.clear();
			if (ins.Memory && (ins.Mnemonic == "lea" || ins.Mnemonic == "add" || ins.Mnemonic == "adr"))
				address = image.RvaOf(*ins.Memory);
			else if (ins.Memory && ins.Mnemonic == "mov") {
				uint64_t value = 0;
				if (image.Read(image.RvaOf(*ins.Memory), &value, image.Pointer))
					address = image.RvaOf(value);
			}
			else if (ins.Mnemonic == "mov") {
				if (!(address = immediateOf(ins.Operands)))
					from = registerOf(ins.Operands.substr(comma + 1));
			}
			return true;
		};
		for (auto const& block : graph.Blocks)
			for (size_t i = 0; i < block.Code.size(); i++) {
				if (!calls.contains(block.Code[i].Address))
					continue;
				// the first parameter: the register (x64: rcx; ARM64: x0), or the last push (x86)
				std::string reg = image.Pe64 ? (arm ? "x0" : "rcx") : "";
				DWORD clsid = 0;
				bool known = false;
				for (size_t k = i; k-- > 0 && i - k <= 24 && !known; ) {
					auto const& ins = block.Code[k];
					if (reg.empty()) {
						if (ins.Mnemonic != "push")
							continue;
						clsid = immediateOf(ins.Operands);
						reg = clsid ? std::string() : registerOf(ins.Operands);
						known = clsid || reg.empty();
						continue;
					}
					std::string from;
					if (!gives(ins, reg, clsid, from))
						continue;
					if (from.empty())
						known = true;
					else
						reg = from;		// (a copy of another: what that one was given)
				}
				// (a register given before the block, as a loop's: what the function gives it, if it gives it one address;
				// moved on by 16 bytes each time, it goes through an array of CLSIDs)
				bool array = false;
				if (!known && !reg.empty()) {
					std::set<DWORD> given;
					for (auto const& b : graph.Blocks)
						for (auto const& ins : b.Code) {
							DWORD address;
							std::string from;
							if (gives(ins, reg, address, from) && address)
								given.insert(address);
							auto comma = ins.Operands.find(',');
							if (ins.Mnemonic == "add" && comma != std::string::npos && sameRegister(ins.Operands.substr(0, comma), reg) &&
								ins.Operands.substr(comma + 1).find("0x10") != std::string::npos)
								array = true;
						}
					if (given.size() == 1)
						clsid = *given.begin();
				}
				if (!clsid)
					continue;
				add(clsid, L"CoRegisterClassObject", 0);
				for (DWORD next = clsid + 16; array && next < clsid + 16 * 64 && image.IsGuid(next) && !isNotClass(next); next += 16)
					add(next, L"CoRegisterClassObject", 0);
			}
	}

	// the tables of classes in the data, by their layouts (from a pointer to a GUID):
	//   ATL's object map (_ATL_OBJMAP_ENTRY): pclsid, pfnUpdateRegistry, pfnGetClassObject, pfnCreateInstance (code), pCF
	//     (null in the file), dwRegister, pfnGetObjectDescription (code)
	//   WRL's creator map (CreatorMap): factoryCreator (code), activationId (the CLSID), getTrustLevel (code; null of a classic
	//     COM class), factoryCache (data)
	//   DirectShow's templates (CFactoryTemplate): m_Name (a wide string), m_ClsID, m_lpfnNew (code)
	auto isText = [&](DWORD rva) {
		wchar_t text[3]{};
		return rva && image.InData(rva) && image.Read(rva, text, sizeof(text)) && iswprint(text[0]) && iswprint(text[1]) && iswprint(text[2]);
	};
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if ((h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) || !(h.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
			continue;
		auto size = std::min(h.Misc.VirtualSize ? h.Misc.VirtualSize : h.SizeOfRawData, h.SizeOfRawData);
		std::vector<uint8_t> data(size);
		if (!size || !image.Read(h.VirtualAddress, data.data(), size))
			continue;
		auto slot = [&](int64_t at) -> DWORD {
			if (at < 0 || at + image.Pointer > size)
				return 0;
			uint64_t value = 0;
			memcpy(&value, data.data() + at, image.Pointer);
			return image.RvaOf(value);
		};
		for (int64_t at = 0; at + image.Pointer <= size; at += image.Pointer) {
			auto guid = slot(at);
			if (!guid || !image.InData(guid))
				continue;
			auto field = [&](int index) { return slot(at + index * (int64_t)image.Pointer); };
			auto raw = [&](int index) -> uint64_t {
				auto offset = at + index * (int64_t)image.Pointer;
				uint64_t value = 0;
				if (offset >= 0 && offset + image.Pointer <= size)
					memcpy(&value, data.data() + offset, image.Pointer);
				return value;
			};
			auto code = [&](int index) { auto rva = field(index); return rva && image.InCode(rva); };
			if (!image.IsGuid(guid))
				continue;
			// (ATL 3 to 10: pCF, null in the file, dwRegister, then the description's function; ATL 11 on, _ATL_OBJMAP_ENTRY110:
			// pCache, a pointer to the data it fills, then the description's function)
			bool atl = code(1) && code(2) && code(3) &&
				((raw(4) == 0 && code(6)) || (field(4) && image.InData(field(4)) && image.Writable(field(4)) && code(5)));
			if (atl)
				add(guid, L"object map (ATL)", field(3));		// (pfnCreateInstance)
			else if (code(-1) && (code(1) || !raw(1)) && field(2) && image.InData(field(2)) && image.Writable(field(2)))
				add(guid, L"creator map (WRL)", field(-1));		// (factoryCreator, the FactoryCache it fills)
			else if (isText(field(-1)) && code(1) && (!raw(2) || code(2)) && (!raw(3) || (field(3) && image.InData(field(3)))))
				add(guid, L"templates (DirectShow)", field(1));	// (m_lpfnNew; m_lpfnInit, m_pAMovieSetup_Filter)
		}
	}

	for (auto& c : classes) {
		uint8_t bytes[16];
		image.Read(c.Guid, bytes, 16);
		ComClassRegistry(GuidText(bytes), c.Registered, c.Server);
	}
	return classes;
}
