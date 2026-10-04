#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

class PEFile;
struct cs_insn;

// The instruction sets that are disassembled
enum class CpuArch : uint8_t {
	X86,
	X64,
	Arm64,
};

// The instruction set of a PE machine (IMAGE_FILE_MACHINE_*) or an ELF machine (EM_*); none for the others
std::optional<CpuArch> ArchOfPeMachine(uint16_t machine);
std::optional<CpuArch> ArchOfElfMachine(uint16_t machine);
// The instruction set of a PE file
std::optional<CpuArch> ArchOf(PEFile const& pe);
const wchar_t* CpuArchName(CpuArch arch);

// Opens Capstone for an instruction set, with the details of the instructions (the handle is a csh)
bool OpenDisassembler(CpuArch arch, size_t& handle);
// The bytes to skip when something is not an instruction: ARM64 instructions are 4 bytes, aligned
size_t SkipSize(CpuArch arch);

enum class XrefKind : uint8_t {
	Call,
	Jump,			// unconditional jump
	ConditionalJump,
	Data,			// an operand that refers to the address: lea, mov, a RIP relative access, a pointer pushed on the stack...
};

const wchar_t* XrefKindToString(XrefKind kind);

// What one instruction refers to. Addresses are virtual addresses (image base included).
struct InstructionRefs {
	std::optional<uint64_t> Branch;	// the target of a direct call or jump
	XrefKind BranchKind{ XrefKind::Jump };
	std::optional<uint64_t> Memory;	// the address of a memory operand with a fixed address ([rip+x], [0x403000])
	std::optional<uint64_t> Pointer;	// an immediate that may be an address (32-bit code: "push 0x403000")
};

// Decodes the references of an instruction that was disassembled with CS_OPT_DETAIL enabled.
// The address of an ARM64 instruction that is built in two (adrp, then add or ldr) needs the AddressTracker.
InstructionRefs GetInstructionRefs(cs_insn const& inst, CpuArch arch);

// An instruction after which the code does not go on: a return, an unconditional jump
bool EndsFlow(cs_insn const& inst, CpuArch arch);

// The references of a run of instructions: ARM64 code builds an address with two instructions (adrp x1, page; add x1, x1, #offset
// or ldr x2, [x1, #offset]), and the second one refers to the address. The tracker remembers the pages that registers hold.
class AddressTracker {
public:
	explicit AddressTracker(CpuArch arch) : m_Arch(arch) {}
	InstructionRefs Refs(cs_insn const& inst);
	void Reset() { m_Pages.clear(); }

private:
	CpuArch m_Arch;
	std::unordered_map<unsigned, uint64_t> m_Pages;		// register -> the address it holds
};

struct Xref {
	uint64_t From;	// the address of the instruction
	XrefKind Kind;
};

// All the places in the executable sections of a file that refer to an address, found by disassembling every executable
// section from start to end (a linear sweep). Data that sits between code (jump tables, for one) can produce references that
// do not exist, and code that is only reachable through computed jumps is still covered, as it is part of the sweep.
class XrefMap {
public:
	// Replaces the contents with the references of 'pe'. False for a file that is not x86, x64 or ARM64, or has no code.
	bool Build(PEFile const& pe);
	void Clear();

	bool Built() const { return m_Built; }
	size_t TargetCount() const { return m_Refs.size(); }
	size_t Count() const { return m_Count; }
	// How many jump tables (the tables behind a "switch") were found. Their entries are jump targets, and their bytes are not code.
	size_t JumpTables() const { return m_JumpTables; }

	// Where the jump table behind the "switch" at 'jmp' leads, in the order of the table (empty if there is no such table).
	std::span<const uint64_t> SwitchTargets(uint64_t jmp) const;

	// The references to 'va', sorted by address (empty if there are none).
	std::span<const Xref> To(uint64_t va) const;

	// How many of those are calls: a rough measure of how much a function is used.
	size_t CallCount(uint64_t va) const;

	// The 'va' that are a target of anything, sorted.
	std::vector<uint64_t> Targets() const;

private:
	std::unordered_map<uint64_t, std::vector<Xref>> m_Refs;
	size_t m_Count{ 0 };
	size_t m_JumpTables{ 0 };
	std::unordered_map<uint64_t, std::vector<uint64_t>> m_Switches;	// by the address of the jump
	bool m_Built{ false };
};
