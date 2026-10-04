#include "pch.h"
#include "CodeAnalysis.h"
#include <PEFile.h>
#include <capstone/capstone.h>
#include <deque>
#include <map>
#include <optional>

const wchar_t* XrefKindToString(XrefKind kind) {
	switch (kind) {
		case XrefKind::Call: return L"Call";
		case XrefKind::Jump: return L"Jump";
		case XrefKind::ConditionalJump: return L"Conditional Jump";
		case XrefKind::Data: return L"Data";
	}
	return L"";
}

std::optional<CpuArch> ArchOfPeMachine(uint16_t machine) {
	switch (machine) {
		case IMAGE_FILE_MACHINE_I386: return CpuArch::X86;
		case IMAGE_FILE_MACHINE_AMD64: return CpuArch::X64;
		case IMAGE_FILE_MACHINE_ARM64:
		case 0xA641:	// ARM64EC
		case 0xA64E:	// ARM64X
			return CpuArch::Arm64;
	}
	return std::nullopt;
}

std::optional<CpuArch> ArchOfElfMachine(uint16_t machine) {
	switch (machine) {
		case 3: return CpuArch::X86;		// EM_386
		case 62: return CpuArch::X64;		// EM_X86_64
		case 183: return CpuArch::Arm64;	// EM_AARCH64
	}
	return std::nullopt;
}

std::optional<CpuArch> ArchOf(PEFile const& pe) {
	auto nt = pe.GetNTHeader();
	if (!pe.GetFileInfo() || !nt)
		return std::nullopt;
	return ArchOfPeMachine(nt->NTHdr32.FileHeader.Machine);
}

const wchar_t* CpuArchName(CpuArch arch) {
	switch (arch) {
		case CpuArch::X86: return L"x86";
		case CpuArch::X64: return L"x64";
		case CpuArch::Arm64: return L"ARM64";
	}
	return L"";
}

bool OpenDisassembler(CpuArch arch, size_t& handle) {
	csh h;
	auto err = arch == CpuArch::Arm64 ? cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &h) :
		cs_open(CS_ARCH_X86, arch == CpuArch::X64 ? CS_MODE_64 : CS_MODE_32, &h);
	if (err != CS_ERR_OK)
		return false;
	cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
	handle = h;
	return true;
}

size_t SkipSize(CpuArch arch) {
	return arch == CpuArch::Arm64 ? 4 : 1;
}

namespace {
	bool InGroup(cs_insn const& inst, uint8_t group) {
		for (int i = 0; i < inst.detail->groups_count; i++)
			if (inst.detail->groups[i] == group)
				return true;
		return false;
	}

	// The loads whose address is in the instruction (ldr x0, =label: the "literal" forms)
	bool IsLiteralLoad(unsigned id) {
		return id == ARM64_INS_LDR || id == ARM64_INS_LDRSW || id == ARM64_INS_PRFM;
	}

	InstructionRefs Arm64Refs(cs_insn const& inst) {
		InstructionRefs refs;
		auto const& a = inst.detail->arm64;
		bool call = InGroup(inst, CS_GRP_CALL), jump = InGroup(inst, CS_GRP_JUMP);
		if (call || jump) {
			// the target is the last immediate: b label, cbz x0, label, tbz x0, #bit, label
			for (int i = a.op_count; i-- > 0;) {
				if (a.operands[i].type == ARM64_OP_IMM) {
					refs.Branch = (uint64_t)a.operands[i].imm;
					bool always = inst.id == ARM64_INS_B && (a.cc == ARM64_CC_INVALID || a.cc == ARM64_CC_AL);
					refs.BranchKind = call ? XrefKind::Call : always ? XrefKind::Jump : XrefKind::ConditionalJump;
					break;
				}
			}
			return refs;
		}
		// adr x0, label; ldr x0, label
		if (a.op_count == 2 && a.operands[1].type == ARM64_OP_IMM && (inst.id == ARM64_INS_ADR || IsLiteralLoad(inst.id)))
			refs.Memory = (uint64_t)a.operands[1].imm;
		return refs;
	}
}

InstructionRefs GetInstructionRefs(cs_insn const& inst, CpuArch arch) {
	InstructionRefs refs;
	if (inst.detail == nullptr)
		return refs;
	if (arch == CpuArch::Arm64)
		return Arm64Refs(inst);
	bool is64Bit = arch == CpuArch::X64;

	auto const& detail = *inst.detail;
	bool call = false, jump = false;
	for (int i = 0; i < detail.groups_count; i++) {
		if (detail.groups[i] == CS_GRP_CALL)
			call = true;
		else if (detail.groups[i] == CS_GRP_JUMP)
			jump = true;
	}

	for (int i = 0; i < detail.x86.op_count; i++) {
		auto const& op = detail.x86.operands[i];
		if (op.type == X86_OP_IMM) {
			if (call || jump) {
				if (!refs.Branch) {
					refs.Branch = (uint64_t)op.imm;
					refs.BranchKind = call ? XrefKind::Call : inst.id == X86_INS_JMP ? XrefKind::Jump : XrefKind::ConditionalJump;
				}
			}
			else if (!is64Bit && !refs.Pointer && op.size == 4) {
				refs.Pointer = (uint32_t)op.imm;
			}
		}
		else if (op.type == X86_OP_MEM && !refs.Memory) {
			auto const& m = op.mem;
			if (m.segment == X86_REG_FS || m.segment == X86_REG_GS)
				continue;	// an offset into the TEB or another thread block, not an address of the image
			if (m.index != X86_REG_INVALID)
				continue;
			if (m.base == X86_REG_RIP)
				refs.Memory = inst.address + inst.size + m.disp;
			else if (m.base == X86_REG_INVALID && m.disp > 0)
				refs.Memory = (uint32_t)m.disp;
		}
	}
	return refs;
}

bool EndsFlow(cs_insn const& inst, CpuArch arch) {
	if (arch == CpuArch::Arm64) {
		switch (inst.id) {
			case ARM64_INS_RET: case ARM64_INS_RETAA: case ARM64_INS_RETAB: case ARM64_INS_BR:
				return true;
			case ARM64_INS_B:
				return inst.detail && (inst.detail->arm64.cc == ARM64_CC_INVALID || inst.detail->arm64.cc == ARM64_CC_AL);
		}
		return false;
	}
	switch (inst.id) {
		case X86_INS_RET: case X86_INS_RETF: case X86_INS_IRET: case X86_INS_IRETD: case X86_INS_IRETQ: case X86_INS_JMP:
			return true;
	}
	return false;
}

InstructionRefs AddressTracker::Refs(cs_insn const& inst) {
	auto refs = GetInstructionRefs(inst, m_Arch);
	if (m_Arch != CpuArch::Arm64 || !inst.detail)
		return refs;
	auto const& a = inst.detail->arm64;
	auto page = [&](unsigned reg) -> std::optional<uint64_t> {
		auto it = m_Pages.find(reg);
		return it == m_Pages.end() ? std::nullopt : std::optional(it->second);
	};

	// a call changes the registers that are not saved; the code after a branch may be reached from elsewhere
	if (InGroup(inst, CS_GRP_CALL) || InGroup(inst, CS_GRP_JUMP) || InGroup(inst, CS_GRP_RET)) {
		m_Pages.clear();
		return refs;
	}
	if (inst.id == ARM64_INS_ADRP && a.op_count == 2 && a.operands[0].type == ARM64_OP_REG && a.operands[1].type == ARM64_OP_IMM) {
		m_Pages[a.operands[0].reg] = (uint64_t)a.operands[1].imm;
		return refs;
	}

	// add x0, x1, #offset: the address, which x0 holds now
	if (inst.id == ARM64_INS_ADD && a.op_count == 3 && a.operands[0].type == ARM64_OP_REG && a.operands[1].type == ARM64_OP_REG &&
		a.operands[2].type == ARM64_OP_IMM && a.operands[2].shift.type == ARM64_SFT_INVALID) {
		if (auto p = page(a.operands[1].reg)) {
			refs.Memory = *p + a.operands[2].imm;
			m_Pages[a.operands[0].reg] = *refs.Memory;
			return refs;
		}
	}
	// ldr x0, [x1, #offset], str...: the address of the memory
	for (int i = 0; i < a.op_count; i++) {
		auto const& op = a.operands[i];
		if (op.type == ARM64_OP_MEM && op.mem.index == ARM64_REG_INVALID && !refs.Memory)
			if (auto p = page(op.mem.base))
				refs.Memory = *p + op.mem.disp;
	}
	// what is written to no longer holds an address (a base that is written back moves)
	for (int i = 0; i < a.op_count; i++) {
		auto const& op = a.operands[i];
		if (op.type == ARM64_OP_REG && (op.access & CS_AC_WRITE))
			m_Pages.erase(op.reg);
		else if (op.type == ARM64_OP_MEM && a.writeback)
			m_Pages.erase(op.mem.base);
	}
	return refs;
}

namespace {
	struct Range {
		uint64_t Start, End;
		bool Contains(uint64_t va) const { return va >= Start && va < End; }
	};
}

namespace {
	// what jump table detection needs of an instruction that was decoded earlier
	struct Recent {
		uint32_t Id{ 0 };
		uint64_t Address{ 0 };
		uint8_t Size{ 0 };
		uint8_t Count{ 0 };
		cs_x86_op Ops[3]{};
	};

	struct JumpTable {
		uint64_t Address{ 0 };
		uint32_t EntrySize{ 4 };
		bool Relative{ false };		// the entries are RVAs (64-bit code) and not addresses
		size_t MaxEntries{ 1024 };
	};

	constexpr size_t RecentCount = 16;

	// "cmp index, N" before the jump bounds the table: N+1 entries
	size_t EntryLimit(std::deque<Recent> const& recent) {
		for (auto i = recent.size(); i-- > 0;) {
			auto const& r = recent[i];
			if (r.Id == X86_INS_CMP && r.Count >= 2 && r.Ops[1].type == X86_OP_IMM && r.Ops[1].imm >= 0 && r.Ops[1].imm < 4096)
				return (size_t)r.Ops[1].imm + 1;
		}
		return 1024;
	}

	// A "switch" compiled to a jump through a table:
	//   x86:  jmp dword ptr [eax*4 + table]            or   mov eax, [eax*4 + table] ... jmp eax
	//   x64:  lea rdx, [image base] ... mov eax, [rdx + rcx*4 + table RVA] ... add rax, rdx ... jmp rax
	std::optional<JumpTable> FindJumpTable(std::deque<Recent> const& recent, cs_insn const& jmp, bool is64, uint64_t imageBase, uint64_t imageSize) {
		if (!jmp.detail || jmp.detail->x86.op_count < 1)
			return std::nullopt;
		auto const& target = jmp.detail->x86.operands[0];
		auto inImage = [&](uint64_t va) { return va >= imageBase && va < imageBase + imageSize; };

		if (target.type == X86_OP_MEM) {
			auto const& m = target.mem;
			if (m.index == X86_REG_INVALID || m.base != X86_REG_INVALID || m.disp <= 0 || (m.scale != 4 && m.scale != 8))
				return std::nullopt;
			if (m.segment == X86_REG_FS || m.segment == X86_REG_GS || !inImage((uint32_t)m.disp))
				return std::nullopt;
			return JumpTable{ (uint32_t)m.disp, (uint32_t)m.scale, false, EntryLimit(recent) };
		}
		if (target.type != X86_OP_REG)
			return std::nullopt;

		// jmp reg: the register was loaded from the table a few instructions before
		bool added = false;
		for (auto i = recent.size(); i-- > 0;) {
			auto const& r = recent[i];
			if (r.Id == X86_INS_ADD) {
				added = true;
				continue;
			}
			if (r.Id != X86_INS_MOV || r.Count < 2 || r.Ops[0].type != X86_OP_REG || r.Ops[1].type != X86_OP_MEM)
				continue;
			auto const& m = r.Ops[1].mem;
			if (m.index == X86_REG_INVALID || m.scale != 4 || m.disp <= 0 || m.segment == X86_REG_FS || m.segment == X86_REG_GS)
				continue;

			if (!is64) {
				// the entries are addresses: mov eax, [eax*4 + table]
				if (m.base == X86_REG_INVALID && inImage((uint32_t)m.disp))
					return JumpTable{ (uint32_t)m.disp, 4, false, EntryLimit(recent) };
				continue;
			}

			// the entries are RVAs: the base register holds the image base, and the loaded value is added to it
			if (!added || m.base == X86_REG_INVALID || m.base == X86_REG_RIP || (uint64_t)m.disp >= imageSize)
				continue;
			for (auto j = i; j-- > 0;) {
				auto const& l = recent[j];
				if (l.Id == X86_INS_LEA && l.Count >= 2 && l.Ops[0].type == X86_OP_REG && l.Ops[0].reg == m.base &&
					l.Ops[1].type == X86_OP_MEM && l.Ops[1].mem.base == X86_REG_RIP &&
					l.Address + l.Size + l.Ops[1].mem.disp == imageBase)
					return JumpTable{ imageBase + (uint32_t)m.disp, 4, true, EntryLimit(recent) };
			}
		}
		return std::nullopt;
	}
}

void XrefMap::Clear() {
	m_Refs.clear();
	m_Count = 0;
	m_JumpTables = 0;
	m_Switches.clear();
	m_Built = false;
}

bool XrefMap::Build(PEFile const& pe) {
	Clear();
	auto info = pe.GetFileInfo();
	auto nt = pe.GetNTHeader();
	auto sections = pe.GetSecHeaders();
	if (!info || !nt || !sections)
		return false;

	auto arch = ArchOfPeMachine(nt->NTHdr32.FileHeader.Machine);
	if (!arch)
		return false;
	bool is64 = *arch != CpuArch::X86;

	uint64_t imageBase = pe.GetImageBase();
	uint64_t imageSize = info->IsPE64 ? nt->NTHdr64.OptionalHeader.SizeOfImage : nt->NTHdr32.OptionalHeader.SizeOfImage;
	Range image{ imageBase, imageBase + imageSize };

	size_t h;
	if (!OpenDisassembler(*arch, h))
		return false;
	csh handle = h;
	auto inst = cs_malloc(handle);
	AddressTracker tracker(*arch);
	auto skip = SkipSize(*arch);

	auto add = [&](uint64_t target, uint64_t from, XrefKind kind) {
		if (!image.Contains(target))
			return;
		auto& v = m_Refs[target];
		if (!v.empty() && v.back().From == from && v.back().Kind == kind)
			return;
		v.push_back({ from, kind });
		m_Count++;
	};

	// where the code of the file is, for the targets of jump tables
	std::vector<Range> code;
	for (auto const& s : *sections)
		if (s.SecHdr.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))
			code.push_back({ imageBase + s.SecHdr.VirtualAddress, imageBase + s.SecHdr.VirtualAddress + std::max(s.SecHdr.Misc.VirtualSize, s.SecHdr.SizeOfRawData) });
	auto isCode = [&](uint64_t va) {
		return std::any_of(code.begin(), code.end(), [&](Range const& r) { return r.Contains(va); });
	};
	auto read = [&](uint64_t va, uint32_t size, uint64_t& value) {
		if (va < imageBase || va - imageBase > 0xFFFFFFFFULL)
			return false;
		auto offset = pe.GetOffsetFromRVA(va - imageBase);
		if (offset == 0 || offset + size > pe.GetFileSize())
			return false;
		value = 0;
		memcpy(&value, pe.GetData() + offset, size);
		return true;
	};

	// the tables of jump targets that were found: their bytes are data, even in a code section
	std::map<uint64_t, uint64_t> data;
	std::deque<Recent> recent;

	bool any = false;
	for (auto const& s : *sections) {
		auto const& h = s.SecHdr;
		if (!(h.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)))
			continue;
		uint64_t size = h.SizeOfRawData;
		if (h.Misc.VirtualSize && h.Misc.VirtualSize < size)
			size = h.Misc.VirtualSize;
		if (h.PointerToRawData >= pe.GetFileSize())
			continue;
		size = std::min<uint64_t>(size, pe.GetFileSize() - h.PointerToRawData);
		if (size == 0)
			continue;
		any = true;
		recent.clear();
		tracker.Reset();

		auto span = pe.GetSpan(h.PointerToRawData, (uint32_t)size);
		auto bytes = (const uint8_t*)span.data();
		size_t left = span.size();
		uint64_t address = imageBase + h.VirtualAddress;
		while (left > 0) {
			// a table found earlier: skip it
			if (auto it = data.upper_bound(address); it != data.begin()) {
				--it;
				if (address >= it->first && address < it->second) {
					auto skip = (size_t)std::min<uint64_t>(it->second - address, left);
					bytes += skip;
					left -= skip;
					address += skip;
					continue;
				}
			}

			if (cs_disasm_iter(handle, &bytes, &left, &address, inst)) {
				auto refs = tracker.Refs(*inst);
				if (refs.Branch)
					add(*refs.Branch, inst->address, refs.BranchKind);
				if (refs.Memory)
					add(*refs.Memory, inst->address, XrefKind::Data);
				if (refs.Pointer)
					add(*refs.Pointer, inst->address, XrefKind::Data);

				// the jump tables of x86 and x64 code
				if (*arch != CpuArch::Arm64 && inst->id == X86_INS_JMP)
					if (auto table = FindJumpTable(recent, *inst, is64, imageBase, imageSize)) {
						std::vector<uint64_t> targets;
						size_t entries = 0;
						for (size_t i = 0; i < table->MaxEntries; i++) {
							uint64_t value;
							if (!read(table->Address + i * table->EntrySize, table->EntrySize, value))
								break;
							auto target = table->Relative ? imageBase + (uint32_t)value : value;
							if (!isCode(target))
								break;
							add(target, inst->address, XrefKind::Jump);
							targets.push_back(target);
							entries++;
						}
						if (entries) {
							add(table->Address, inst->address, XrefKind::Data);
							data[table->Address] = table->Address + entries * table->EntrySize;
							m_JumpTables++;
							m_Switches[inst->address] = std::move(targets);
						}
					}

				Recent r;
				r.Id = inst->id;
				r.Address = inst->address;
				r.Size = (uint8_t)inst->size;
				if (inst->detail && *arch != CpuArch::Arm64) {
					r.Count = (uint8_t)std::min<int>(inst->detail->x86.op_count, 3);
					for (int i = 0; i < r.Count; i++)
						r.Ops[i] = inst->detail->x86.operands[i];
				}
				recent.push_back(r);
				if (recent.size() > RecentCount)
					recent.pop_front();
			}
			else {
				// not an instruction: move on to the next byte (the next instruction of ARM64)
				auto n = std::min(skip, left);
				bytes += n;
				left -= n;
				address += n;
				tracker.Reset();
			}
		}
	}
	cs_free(inst, 1);
	cs_close(&handle);

	// what was decoded from the bytes of a table that was found only after them is not code
	if (!data.empty()) {
		auto inTable = [&](uint64_t va) {
			auto it = data.upper_bound(va);
			return it != data.begin() && va < std::prev(it)->second;
		};
		m_Count = 0;
		for (auto it = m_Refs.begin(); it != m_Refs.end();) {
			std::erase_if(it->second, [&](Xref const& x) { return inTable(x.From); });
			m_Count += it->second.size();
			it = it->second.empty() ? m_Refs.erase(it) : std::next(it);
		}
	}

	for (auto& [target, v] : m_Refs)
		if (!std::is_sorted(v.begin(), v.end(), [](Xref const& a, Xref const& b) { return a.From < b.From; }))
			std::stable_sort(v.begin(), v.end(), [](Xref const& a, Xref const& b) { return a.From < b.From; });

	m_Built = any;
	return any;
}

std::span<const uint64_t> XrefMap::SwitchTargets(uint64_t jmp) const {
	auto it = m_Switches.find(jmp);
	if (it == m_Switches.end())
		return {};
	return it->second;
}

std::span<const Xref> XrefMap::To(uint64_t va) const {
	auto it = m_Refs.find(va);
	if (it == m_Refs.end())
		return {};
	return it->second;
}

size_t XrefMap::CallCount(uint64_t va) const {
	size_t count = 0;
	for (auto const& x : To(va))
		if (x.Kind == XrefKind::Call)
			count++;
	return count;
}

std::vector<uint64_t> XrefMap::Targets() const {
	std::vector<uint64_t> targets;
	targets.reserve(m_Refs.size());
	for (auto const& [target, v] : m_Refs)
		targets.push_back(target);
	std::sort(targets.begin(), targets.end());
	return targets;
}
