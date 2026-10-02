#include "pch.h"
#include "CodeAnalysis.h"
#include <PEFile.h>
#include <capstone/capstone.h>

const wchar_t* XrefKindToString(XrefKind kind) {
	switch (kind) {
		case XrefKind::Call: return L"Call";
		case XrefKind::Jump: return L"Jump";
		case XrefKind::ConditionalJump: return L"Conditional Jump";
		case XrefKind::Data: return L"Data";
	}
	return L"";
}

InstructionRefs GetInstructionRefs(cs_insn const& inst, bool is64Bit) {
	InstructionRefs refs;
	if (inst.detail == nullptr)
		return refs;

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

namespace {
	struct Range {
		uint64_t Start, End;
		bool Contains(uint64_t va) const { return va >= Start && va < End; }
	};
}

void XrefMap::Clear() {
	m_Refs.clear();
	m_Count = 0;
	m_Built = false;
}

bool XrefMap::Build(PEFile const& pe) {
	Clear();
	auto info = pe.GetFileInfo();
	auto nt = pe.GetNTHeader();
	auto sections = pe.GetSecHeaders();
	if (!info || !nt || !sections)
		return false;

	auto machine = nt->NTHdr32.FileHeader.Machine;
	bool is64 = machine == IMAGE_FILE_MACHINE_AMD64;
	if (!is64 && machine != IMAGE_FILE_MACHINE_I386)
		return false;

	uint64_t imageBase = pe.GetImageBase();
	uint64_t imageSize = is64 ? nt->NTHdr64.OptionalHeader.SizeOfImage : nt->NTHdr32.OptionalHeader.SizeOfImage;
	Range image{ imageBase, imageBase + imageSize };

	csh handle;
	if (cs_open(CS_ARCH_X86, is64 ? CS_MODE_64 : CS_MODE_32, &handle) != CS_ERR_OK)
		return false;
	cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
	auto inst = cs_malloc(handle);

	auto add = [&](uint64_t target, uint64_t from, XrefKind kind) {
		if (!image.Contains(target))
			return;
		auto& v = m_Refs[target];
		if (!v.empty() && v.back().From == from && v.back().Kind == kind)
			return;
		v.push_back({ from, kind });
		m_Count++;
	};

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

		auto code = pe.GetSpan(h.PointerToRawData, (uint32_t)size);
		auto bytes = (const uint8_t*)code.data();
		size_t left = code.size();
		uint64_t address = imageBase + h.VirtualAddress;
		while (left > 0) {
			if (cs_disasm_iter(handle, &bytes, &left, &address, inst)) {
				auto refs = GetInstructionRefs(*inst, is64);
				if (refs.Branch)
					add(*refs.Branch, inst->address, refs.BranchKind);
				if (refs.Memory)
					add(*refs.Memory, inst->address, XrefKind::Data);
				if (refs.Pointer)
					add(*refs.Pointer, inst->address, XrefKind::Data);
			}
			else {
				// not an instruction: move on to the next byte
				bytes++;
				left--;
				address++;
			}
		}
	}
	cs_free(inst, 1);
	cs_close(&handle);

	for (auto& [target, v] : m_Refs)
		if (!std::is_sorted(v.begin(), v.end(), [](Xref const& a, Xref const& b) { return a.From < b.From; }))
			std::stable_sort(v.begin(), v.end(), [](Xref const& a, Xref const& b) { return a.From < b.From; });

	m_Built = any;
	return any;
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
