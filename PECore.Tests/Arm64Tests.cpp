#include "TestCommon.h"
#include <algorithm>
#include <CodeAnalysis.h>
#include <FlowGraph.h>
#include <capstone/capstone.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	// ARM64 instructions, encoded: 'offset' is the distance from the instruction to the target
	uint32_t B(int32_t offset) { return 0x14000000 | ((offset / 4) & 0x3FFFFFF); }
	uint32_t BL(int32_t offset) { return 0x94000000 | ((offset / 4) & 0x3FFFFFF); }
	uint32_t BCond(int32_t offset, uint32_t cond) { return 0x54000000 | (((offset / 4) & 0x7FFFF) << 5) | cond; }
	uint32_t Cbz(uint32_t rt, int32_t offset) { return 0xB4000000 | (((offset / 4) & 0x7FFFF) << 5) | rt; }
	uint32_t Tbz(uint32_t rt, uint32_t bit, int32_t offset) { return 0x36000000 | (bit << 19) | (((offset / 4) & 0x3FFF) << 5) | rt; }
	uint32_t Adrp(uint32_t rd, int32_t pages) { return 0x90000000 | ((pages & 3) << 29) | (((pages >> 2) & 0x7FFFF) << 5) | rd; }
	uint32_t Adr(uint32_t rd, int32_t offset) { return 0x10000000 | ((offset & 3) << 29) | (((offset >> 2) & 0x7FFFF) << 5) | rd; }
	uint32_t AddImm(uint32_t rd, uint32_t rn, uint32_t imm) { return 0x91000000 | (imm << 10) | (rn << 5) | rd; }
	uint32_t AddImm12(uint32_t rd, uint32_t rn, uint32_t imm) { return 0x91400000 | (imm << 10) | (rn << 5) | rd; }	// add rd, rn, #imm, lsl #12
	uint32_t Ldr(uint32_t rt, uint32_t rn, uint32_t offset) { return 0xF9400000 | ((offset / 8) << 10) | (rn << 5) | rt; }
	uint32_t LdrLiteral(uint32_t rt, int32_t offset) { return 0x58000000 | (((offset / 4) & 0x7FFFF) << 5) | rt; }
	uint32_t MovReg(uint32_t rd, uint32_t rm) { return 0xAA0003E0 | (rm << 16) | rd; }
	constexpr uint32_t Ret = 0xD65F03C0;
	constexpr uint32_t BrX16 = 0xD61F0200;
	constexpr uint32_t BlrX8 = 0xD63F0100;
	constexpr uint32_t Brk = 0xD4200000;
	constexpr uint32_t Nop = 0xD503201F;
	constexpr uint32_t CondEq = 0, CondNe = 1, CondGe = 10;

	Bytes Code(std::vector<uint32_t> const& words, size_t size = 0x80) {
		Bytes code(std::max(size, words.size() * 4), 0);
		for (size_t i = 0; i < words.size(); i++)
			memcpy(code.data() + i * 4, &words[i], 4);
		for (size_t i = words.size() * 4; i + 4 <= code.size(); i += 4)
			memcpy(code.data() + i, &Brk, 4);
		return code;
	}

	SyntheticPE Arm64Spec() {
		SyntheticPE spec;
		spec.Machine = IMAGE_FILE_MACHINE_ARM64;
		return spec;
	}

	Bytes WithCode(Bytes const& code) {
		auto file = Arm64Spec().Build();
		std::copy(code.begin(), code.end(), file.begin() + SyntheticPE::TextOffset);
		return file;
	}

	struct Disassembler {
		size_t Handle{ 0 };
		Disassembler() { REQUIRE(OpenDisassembler(CpuArch::Arm64, Handle)); }
		~Disassembler() { csh h = Handle; cs_close(&h); }

		// the instruction 'word' at 'address'
		template<typename F>
		void With(uint32_t word, uint64_t address, F&& f) {
			cs_insn* inst = nullptr;
			REQUIRE(cs_disasm((csh)Handle, (const uint8_t*)&word, 4, address, 1, &inst) == 1);
			f(*inst);
			cs_free(inst, 1);
		}

		InstructionRefs Refs(uint32_t word, uint64_t address) {
			InstructionRefs refs;
			With(word, address, [&](cs_insn const& inst) { refs = GetInstructionRefs(inst, CpuArch::Arm64); });
			return refs;
		}
	};

	bool Has(XrefMap const& map, uint64_t target, uint64_t from, XrefKind kind) {
		for (auto& x : map.To(target))
			if (x.From == from && x.Kind == kind)
				return true;
		return false;
	}

	constexpr uint64_t Text = 0x140001000, Data = 0x140002000, RData = 0x140003000;
}

TEST_CASE("The instruction set of a machine", "[arm64]") {
	CHECK(ArchOfPeMachine(IMAGE_FILE_MACHINE_I386) == CpuArch::X86);
	CHECK(ArchOfPeMachine(IMAGE_FILE_MACHINE_AMD64) == CpuArch::X64);
	CHECK(ArchOfPeMachine(IMAGE_FILE_MACHINE_ARM64) == CpuArch::Arm64);
	CHECK(ArchOfPeMachine(0xA641) == CpuArch::Arm64);		// ARM64EC
	CHECK_FALSE(ArchOfPeMachine(IMAGE_FILE_MACHINE_ARMNT));
	CHECK_FALSE(ArchOfPeMachine(IMAGE_FILE_MACHINE_IA64));

	CHECK(ArchOfElfMachine(3) == CpuArch::X86);
	CHECK(ArchOfElfMachine(62) == CpuArch::X64);
	CHECK(ArchOfElfMachine(183) == CpuArch::Arm64);
	CHECK_FALSE(ArchOfElfMachine(40));		// 32-bit ARM
	CHECK_FALSE(ArchOfElfMachine(243));		// RISC-V

	CHECK(SkipSize(CpuArch::Arm64) == 4);
	CHECK(SkipSize(CpuArch::X64) == 1);
	CHECK(std::wstring(CpuArchName(CpuArch::Arm64)) == L"ARM64");
}

TEST_CASE("Branches and addresses of ARM64 instructions", "[arm64][xref]") {
	Disassembler d;

	auto bl = d.Refs(BL(0x20), 0x1000);
	CHECK(bl.Branch == 0x1020);
	CHECK(bl.BranchKind == XrefKind::Call);

	auto b = d.Refs(B(-8), 0x1000);
	CHECK(b.Branch == 0xFF8);
	CHECK(b.BranchKind == XrefKind::Jump);

	auto beq = d.Refs(BCond(0x40, CondEq), 0x1000);
	CHECK(beq.Branch == 0x1040);
	CHECK(beq.BranchKind == XrefKind::ConditionalJump);

	auto cbz = d.Refs(Cbz(3, 0x10), 0x1000);
	CHECK(cbz.Branch == 0x1010);
	CHECK(cbz.BranchKind == XrefKind::ConditionalJump);

	auto tbz = d.Refs(Tbz(5, 3, 0x20), 0x1000);		// the target, not the number of the bit
	CHECK(tbz.Branch == 0x1020);
	CHECK(tbz.BranchKind == XrefKind::ConditionalJump);

	auto adr = d.Refs(Adr(0, 0x101), 0x1000);
	CHECK(adr.Memory == 0x1101);
	CHECK_FALSE(adr.Branch);

	auto literal = d.Refs(LdrLiteral(0, 0x40), 0x1000);
	CHECK(literal.Memory == 0x1040);

	// a register: where it goes is not known
	CHECK_FALSE(d.Refs(BrX16, 0x1000).Branch);
	CHECK_FALSE(d.Refs(BlrX8, 0x1000).Branch);
	CHECK_FALSE(d.Refs(Ret, 0x1000).Branch);

	// adrp alone is a page, not an address that is used
	auto adrp = d.Refs(Adrp(1, 1), 0x1000);
	CHECK_FALSE(adrp.Memory);
	CHECK_FALSE(adrp.Branch);
}

TEST_CASE("The end of the flow of ARM64 code", "[arm64]") {
	Disassembler d;
	auto ends = [&](uint32_t word) {
		bool end = false;
		d.With(word, 0x1000, [&](cs_insn const& inst) { end = EndsFlow(inst, CpuArch::Arm64); });
		return end;
	};
	CHECK(ends(Ret));
	CHECK(ends(B(0x10)));
	CHECK(ends(BrX16));
	CHECK_FALSE(ends(BCond(0x10, CondNe)));
	CHECK_FALSE(ends(Cbz(0, 0x10)));
	CHECK_FALSE(ends(BL(0x10)));
	CHECK_FALSE(ends(BlrX8));
	CHECK_FALSE(ends(Nop));
}

TEST_CASE("Addresses that adrp builds with add and ldr", "[arm64][xref]") {
	Disassembler d;
	AddressTracker tracker(CpuArch::Arm64);
	auto refs = [&](uint32_t word, uint64_t address) {
		InstructionRefs r;
		d.With(word, address, [&](cs_insn const& inst) { r = tracker.Refs(inst); });
		return r;
	};

	SECTION("adrp, add, ldr from the address") {
		CHECK_FALSE(refs(Adrp(1, 1), 0x140001000).Memory);		// x1 = 0x140002000
		CHECK(refs(AddImm(1, 1, 0x10), 0x140001004).Memory == 0x140002010);
		CHECK(refs(Ldr(2, 1, 8), 0x140001008).Memory == 0x140002018);
	}
	SECTION("adrp, then ldr of the page") {
		refs(Adrp(3, 2), 0x140001000);
		CHECK(refs(Ldr(4, 3, 0x40), 0x140001004).Memory == 0x140003040);
		// the register keeps the page: a second load
		CHECK(refs(Ldr(5, 3, 0x48), 0x140001008).Memory == 0x140003048);
	}
	SECTION("a register that is written to no longer holds the page") {
		refs(Adrp(1, 1), 0x140001000);
		refs(MovReg(1, 7), 0x140001004);
		CHECK_FALSE(refs(AddImm(2, 1, 0x10), 0x140001008).Memory);
	}
	SECTION("the loaded value is not an address") {
		refs(Adrp(1, 1), 0x140001000);
		refs(Ldr(1, 1, 0), 0x140001004);	// x1 = what is at the address
		CHECK_FALSE(refs(Ldr(2, 1, 8), 0x140001008).Memory);
	}
	SECTION("a call forgets the registers") {
		refs(Adrp(1, 1), 0x140001000);
		refs(BL(0x100), 0x140001004);
		CHECK_FALSE(refs(AddImm(1, 1, 0x10), 0x140001008).Memory);
	}
	SECTION("add with a shifted immediate is not an offset into the page") {
		refs(Adrp(1, 1), 0x140001000);
		CHECK_FALSE(refs(AddImm12(1, 1, 1), 0x140001004).Memory);
	}
	SECTION("a reset forgets the registers") {
		refs(Adrp(1, 1), 0x140001000);
		tracker.Reset();
		CHECK_FALSE(refs(AddImm(1, 1, 0x10), 0x140001004).Memory);
	}
}

namespace {
	// ARM64 code at 0x140001000:
	//   00 bl 20             04 b 30              08 b.eq 30           0C cbz x0, 30
	//   10 adrp x1, 2000     14 add x1, x1, #0x10 18 ldr x2, [x1, #8]  1C ret
	//   20 ret               24 adrp x3, 3000     28 ldr x4, [x3,#0x40] 2C ret
	//   30 ret
	Bytes XrefCode() {
		return Code({ BL(0x20), B(0x2C), BCond(0x28, CondEq), Cbz(0, 0x24),
			Adrp(1, 1), AddImm(1, 1, 0x10), Ldr(2, 1, 8), Ret,
			Ret, Adrp(3, 2), Ldr(4, 3, 0x40), Ret,
			Ret });
	}
}

TEST_CASE("Calls, jumps and data references of ARM64 code", "[arm64][xref]") {
	TempFile temp(WithCode(XrefCode()), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(temp.Path()));
	CHECK(ArchOf(pe) == CpuArch::Arm64);
	XrefMap map;
	REQUIRE(map.Build(pe));

	CHECK(Has(map, Text + 0x20, Text + 0x00, XrefKind::Call));
	CHECK(Has(map, Text + 0x30, Text + 0x04, XrefKind::Jump));
	CHECK(Has(map, Text + 0x30, Text + 0x08, XrefKind::ConditionalJump));
	CHECK(Has(map, Text + 0x30, Text + 0x0C, XrefKind::ConditionalJump));
	CHECK(Has(map, Data + 0x10, Text + 0x14, XrefKind::Data));
	CHECK(Has(map, Data + 0x18, Text + 0x18, XrefKind::Data));
	CHECK(Has(map, RData + 0x40, Text + 0x28, XrefKind::Data));
	CHECK(map.CallCount(Text + 0x20) == 1);
	// the pages themselves are not referred to
	CHECK(map.To(Data).empty());
	CHECK(map.To(RData).empty());
}

TEST_CASE("Bytes that are not ARM64 instructions are skipped by whole instructions", "[arm64][xref]") {
	// 00 an undefined word, 04 bl 20: the call is found at its own address, not within the word before it
	auto code = Code({ 0x00000000, BL(0x1C), Ret });
	TempFile temp(WithCode(code), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(temp.Path()));
	XrefMap map;
	REQUIRE(map.Build(pe));
	CHECK(Has(map, Text + 0x20, Text + 0x04, XrefKind::Call));
}

TEST_CASE("The flow graph of ARM64 code", "[arm64][flowgraph]") {
	// a loop that counts x0 up to x1:
	//   00 mov x0, xzr (orr x0, xzr, xzr)
	//   04 cmp x0, x1          <- loop head
	//   08 b.ge 14
	//   0C add x0, x0, #1
	//   10 b 04
	//   14 ret
	constexpr uint32_t MovZero = 0xAA1F03E0, CmpX0X1 = 0xEB01001F;
	auto code = Code({ MovZero, CmpX0X1, BCond(0x0C, CondGe), AddImm(0, 0, 1), B(-0x0C), Ret });
	TempFile temp(WithCode(code), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(temp.Path()));
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));

	auto g = BuildFlowGraph(pe, xrefs, Text);
	REQUIRE(g.Blocks.size() == 4);
	CHECK(g.Blocks[0].Start == Text);
	CHECK(g.Blocks[0].Ending == FlowEnd::Fallthrough);
	CHECK(g.Blocks[1].Start == Text + 0x04);
	CHECK(g.Blocks[1].Ending == FlowEnd::ConditionalJump);
	CHECK(g.Blocks[2].Start == Text + 0x0C);
	CHECK(g.Blocks[2].Ending == FlowEnd::Jump);
	CHECK(g.Blocks[3].Start == Text + 0x14);
	CHECK(g.Blocks[3].Ending == FlowEnd::Return);
	CHECK(g.Blocks[1].Code.size() == 2);
	CHECK(g.Blocks[1].Code[1].Mnemonic == "b.ge");
	CHECK(g.Blocks[1].Code[1].Branch == Text + 0x14);

	auto edge = [&](size_t from, size_t to) {
		return std::any_of(g.Edges.begin(), g.Edges.end(), [&](FlowEdge const& e) { return e.From == from && e.To == to; });
	};
	CHECK(edge(0, 1));
	CHECK(edge(1, 3));		// taken
	CHECK(edge(1, 2));		// not taken
	CHECK(edge(2, 1));		// back to the head
}

TEST_CASE("ARM64 code that does not go on", "[arm64][flowgraph]") {
	// 00 cbz x0, 0C   04 br x16   08 nop   0C brk #0
	auto code = Code({ Cbz(0, 0x0C), BrX16, Nop, Brk });
	TempFile temp(WithCode(code), L".dll");
	PEFile pe;
	REQUIRE(pe.Open(temp.Path()));
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));

	auto g = BuildFlowGraph(pe, xrefs, Text);
	REQUIRE(g.Blocks.size() == 3);
	CHECK(g.Blocks[0].Ending == FlowEnd::ConditionalJump);
	CHECK(g.Blocks[1].Start == Text + 0x04);
	CHECK(g.Blocks[1].Ending == FlowEnd::IndirectJump);
	CHECK(g.Blocks[2].Start == Text + 0x0C);
	CHECK(g.Blocks[2].Ending == FlowEnd::Trap);
}

// A real ARM64 file (TOTALPE_ARM64_FILE): the references, the functions of the exception directory, the flow graph of the entry point
TEST_CASE("The code of an ARM64 file", "[.arm64file]") {
	char path[MAX_PATH];
	if (!GetEnvironmentVariableA("TOTALPE_ARM64_FILE", path, MAX_PATH))
		SKIP("TOTALPE_ARM64_FILE is not set");
	PEFile pe;
	REQUIRE(pe.Open(std::wstring(path, path + strlen(path)).c_str()));
	REQUIRE(ArchOf(pe) == CpuArch::Arm64);
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));
	size_t calls = 0, data = 0, ends = 0;
	for (auto target : xrefs.Targets())
		for (auto const& x : xrefs.To(target))
			(x.Kind == XrefKind::Call ? calls : x.Kind == XrefKind::Data ? data : ends)++;
	size_t withEnd = 0;
	for (auto const& e : *pe.GetExceptions())
		withEnd += e.RuntimeFuncEntry.EndAddress > e.RuntimeFuncEntry.BeginAddress;
	auto entry = pe.GetImageBase() + pe.GetNTHeader()->NTHdr64.OptionalHeader.AddressOfEntryPoint;
	char va[32];
	if (GetEnvironmentVariableA("TOTALPE_ARM64_VA", va, sizeof(va)))
		entry = strtoull(va, nullptr, 16);
	auto g = BuildFlowGraph(pe, xrefs, entry);
	WARN("targets " << xrefs.Count() << ", calls " << calls << ", data " << data << ", jumps " << ends
		<< "; functions " << pe.GetExceptions()->size() << " (with an end: " << withEnd << ")"
		<< "; entry blocks " << g.Blocks.size() << ", edges " << g.Edges.size()
		<< "; function of entry+8 " << std::hex << FindFunctionStart(pe, xrefs, entry + 8) << " entry " << entry);
	for (auto const& b : g.Blocks)
		for (auto const& i : b.Code)
			WARN(std::hex << i.Address << " " << i.Mnemonic << " " << i.Operands << (i.Memory ? " -> " : "") << (i.Memory ? *i.Memory : 0));
}

// Every function of the exception directory of a real ARM64 file (TOTALPE_ARM64_FILE) has a flow graph
TEST_CASE("The flow graphs of the functions of an ARM64 file", "[.arm64graphs]") {
	char path[MAX_PATH];
	if (!GetEnvironmentVariableA("TOTALPE_ARM64_FILE", path, MAX_PATH))
		SKIP("TOTALPE_ARM64_FILE is not set");
	PEFile pe;
	REQUIRE(pe.Open(std::wstring(path, path + strlen(path)).c_str()));
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));
	size_t empty = 0, total = 0, notStart = 0;
	for (auto const& e : *pe.GetExceptions()) {
		auto va = pe.GetImageBase() + e.RuntimeFuncEntry.BeginAddress;
		total++;
		auto g = BuildFlowGraph(pe, xrefs, va);
		if (g.Blocks.empty() && empty++ < 5)
			WARN("no graph at " << std::hex << va);
		if (FindFunctionStart(pe, xrefs, va + 4) != va && notStart++ < 5)
			WARN("function of " << std::hex << va + 4 << " is " << FindFunctionStart(pe, xrefs, va + 4) << " end " << e.RuntimeFuncEntry.EndAddress);
	}
	WARN(total << " functions, " << empty << " without a graph, " << notStart << " with the wrong start");
}
