#include "TestCommon.h"
#include <algorithm>
#include <CodeAnalysis.h>
#include <capstone/capstone.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	// a synthetic file with 'code' at the start of .text (the rest is INT3)
	Bytes WithCode(SyntheticPE spec, Bytes const& code, uint32_t at = 0) {
		auto file = spec.Build();
		std::copy(code.begin(), code.end(), file.begin() + SyntheticPE::TextOffset + at);
		return file;
	}

	XrefMap BuildMap(Bytes const& file, bool expected = true) {
		TempFile temp(file, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(temp.Path()));
		XrefMap map;
		CHECK(map.Build(pe) == expected);
		return map;
	}

	bool Has(XrefMap const& map, uint64_t target, uint64_t from, XrefKind kind) {
		for (auto& x : map.To(target))
			if (x.From == from && x.Kind == kind)
				return true;
		return false;
	}

	// x64 code at 0x140001000:
	//   1000 call 1020          1005 jmp 1030          100A jz 1030
	//   1010 lea rax,[2000]     1017 call [rip->30F0]  101D ret
	//   1020 ret                1021 call 9000 (outside the image)
	Bytes Code64() {
		Bytes code(0x40, 0xCC);
		auto put = [&](size_t at, Bytes const& b) { std::copy(b.begin(), b.end(), code.begin() + at); };
		put(0x00, { 0xE8, 0x1B, 0x00, 0x00, 0x00 });
		put(0x05, { 0xE9, 0x26, 0x00, 0x00, 0x00 });
		put(0x0A, { 0x0F, 0x84, 0x20, 0x00, 0x00, 0x00 });
		put(0x10, { 0x48, 0x8D, 0x05, 0xE9, 0x0F, 0x00, 0x00 });
		put(0x17, { 0xFF, 0x15, 0xD3, 0x20, 0x00, 0x00 });
		put(0x1D, { 0xC3 });
		put(0x20, { 0xC3 });
		put(0x21, { 0xE8, 0xDA, 0x7F, 0x00, 0x00 });
		put(0x30, { 0xC3 });
		return code;
	}
}

TEST_CASE("Calls, jumps and data references of x64 code", "[xref]") {
	SyntheticPE spec;
	auto map = BuildMap(WithCode(spec, Code64()));
	const uint64_t base = 0x140001000;

	CHECK(Has(map, base + 0x20, base + 0x00, XrefKind::Call));
	CHECK(Has(map, base + 0x30, base + 0x05, XrefKind::Jump));
	CHECK(Has(map, base + 0x30, base + 0x0A, XrefKind::ConditionalJump));
	CHECK(Has(map, 0x140002000, base + 0x10, XrefKind::Data));	// lea
	CHECK(Has(map, 0x1400030F0, base + 0x17, XrefKind::Data));	// the import slot
	CHECK(map.To(0x140009000).empty());							// outside the image

	SECTION("references are sorted and counted") {
		auto to = map.To(base + 0x30);
		REQUIRE(to.size() == 2);
		CHECK(to[0].From < to[1].From);
		CHECK(map.CallCount(base + 0x20) == 1);
		CHECK(map.CallCount(base + 0x30) == 0);
		CHECK(map.Count() == 5);
		CHECK(map.TargetCount() == 4);
		auto targets = map.Targets();
		CHECK(std::is_sorted(targets.begin(), targets.end()));
		CHECK(targets.size() == 4);
	}
	SECTION("what nothing refers to") {
		CHECK(map.To(base + 0x10).empty());
		CHECK(map.To(0).empty());
	}
}

TEST_CASE("Calls, jumps and data references of x86 code", "[xref]") {
	SyntheticPE spec;
	spec.Is64 = false;
	// 1000 push 0x403000   1005 mov eax,[0x402000]   100A call 1020   100F mov eax,5   1014 mov eax,fs:[0x30]   101A ret
	Bytes code(0x40, 0xCC);
	auto put = [&](size_t at, Bytes const& b) { std::copy(b.begin(), b.end(), code.begin() + at); };
	put(0x00, { 0x68, 0x00, 0x30, 0x40, 0x00 });
	put(0x05, { 0xA1, 0x00, 0x20, 0x40, 0x00 });
	put(0x0A, { 0xE8, 0x11, 0x00, 0x00, 0x00 });
	put(0x0F, { 0xB8, 0x05, 0x00, 0x00, 0x00 });
	put(0x14, { 0x64, 0xA1, 0x30, 0x00, 0x00, 0x00 });
	put(0x1A, { 0xC3 });
	put(0x20, { 0xC3 });

	auto map = BuildMap(WithCode(spec, code));
	const uint64_t base = 0x401000;
	CHECK(Has(map, 0x403000, base + 0x00, XrefKind::Data));		// a pointer pushed on the stack
	CHECK(Has(map, 0x402000, base + 0x05, XrefKind::Data));		// an absolute memory operand
	CHECK(Has(map, base + 0x20, base + 0x0A, XrefKind::Call));
	CHECK(map.To(5).empty());									// a number that is not an address
	CHECK(map.To(0x30).empty());								// an offset from fs: is not either
	CHECK(map.Count() == 3);
}

TEST_CASE("Only executable sections are scanned", "[xref]") {
	SyntheticPE spec;
	auto file = spec.Build();
	// a call instruction in .data
	file[SyntheticPE::DataOffset + 0x10] = 0xE8;
	file[SyntheticPE::DataOffset + 0x11] = 0x00;
	file[SyntheticPE::DataOffset + 0x12] = 0x10;
	file[SyntheticPE::DataOffset + 0x13] = 0x00;
	file[SyntheticPE::DataOffset + 0x14] = 0x00;
	auto map = BuildMap(file);
	CHECK(map.Count() == 0);
}

TEST_CASE("Bytes that are not instructions are skipped", "[xref]") {
	SyntheticPE spec;
	Bytes junk(SyntheticPE::SectionSize);
	for (size_t i = 0; i < junk.size(); i++)
		junk[i] = (i % 3 == 0) ? 0x06 : (i % 3 == 1) ? 0xFF : 0x0F;	// invalid in 64-bit mode, truncated and invalid opcodes
	auto map = BuildMap(WithCode(spec, junk));
	CHECK(map.Built());

	// an instruction cut off by the end of the section
	Bytes cut(SyntheticPE::SectionSize, 0xCC);
	cut[SyntheticPE::SectionSize - 1] = 0xE8;
	BuildMap(WithCode(spec, cut));
}

TEST_CASE("Files that cannot be analyzed", "[xref]") {
	XrefMap map;
	PEFile pe;
	CHECK_FALSE(map.Build(pe));		// nothing open
	CHECK_FALSE(map.Built());

	SECTION("a build replaces the previous contents") {
		SyntheticPE spec;
		TempFile temp(WithCode(spec, Code64()), L".dll");
		REQUIRE(pe.Open(temp.Path()));
		REQUIRE(map.Build(pe));
		CHECK(map.Count() > 0);
		PEFile other;
		CHECK_FALSE(map.Build(other));
		CHECK(map.Count() == 0);
		CHECK_FALSE(map.Built());
	}
}

TEST_CASE("Instruction references from Capstone details", "[xref]") {
	csh handle;
	REQUIRE(cs_open(CS_ARCH_X86, CS_MODE_64, &handle) == CS_ERR_OK);
	cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

	auto refsOf = [&](Bytes code, uint64_t address) {
		cs_insn* inst = nullptr;
		auto n = cs_disasm(handle, code.data(), code.size(), address, 1, &inst);
		REQUIRE(n == 1);
		auto refs = GetInstructionRefs(*inst, true);
		cs_free(inst, 1);
		return refs;
	};

	auto call = refsOf({ 0xE8, 0x1B, 0x00, 0x00, 0x00 }, 0x1000);
	CHECK(call.Branch == 0x1020);
	CHECK(call.BranchKind == XrefKind::Call);
	CHECK_FALSE(call.Memory);

	auto jump = refsOf({ 0xEB, 0xFE }, 0x1000);					// jmp $
	CHECK(jump.Branch == 0x1000);
	CHECK(jump.BranchKind == XrefKind::Jump);

	auto jcc = refsOf({ 0x75, 0x10 }, 0x1000);					// jne
	CHECK(jcc.Branch == 0x1012);
	CHECK(jcc.BranchKind == XrefKind::ConditionalJump);

	auto indirect = refsOf({ 0xFF, 0x25, 0x10, 0x00, 0x00, 0x00 }, 0x1000);	// jmp [rip+0x10]
	CHECK_FALSE(indirect.Branch);
	CHECK(indirect.Memory == 0x1016);

	auto reg = refsOf({ 0xFF, 0xD0 }, 0x1000);					// call rax
	CHECK_FALSE(reg.Branch);
	CHECK_FALSE(reg.Memory);

	auto indexed = refsOf({ 0x48, 0x8B, 0x04, 0xC8 }, 0x1000);	// mov rax,[rax+rcx*8]
	CHECK_FALSE(indexed.Memory);

	auto thread = refsOf({ 0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00 }, 0x1000);	// mov rax,gs:[0x60]
	CHECK_FALSE(thread.Memory);

	cs_close(&handle);
}

TEST_CASE("Cross-references of a system DLL", "[xref][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	PEFile pe;
	if (!pe.Open(std::wstring(dir) + L"\\kernel32.dll"))
		SKIP("kernel32.dll could not be opened");

	XrefMap map;
	REQUIRE(map.Build(pe));
	CHECK(map.Count() > 5000);
	CHECK(map.TargetCount() > 1000);
	CHECK(map.JumpTables() > 5);	// the switch statements of kernel32

	// every reference starts in an executable section
	auto imageBase = pe.GetImageBase();
	auto inCode = [&](uint64_t va) {
		for (auto& s : *pe.GetSecHeaders())
			if ((s.SecHdr.Characteristics & IMAGE_SCN_MEM_EXECUTE) && va >= imageBase + s.SecHdr.VirtualAddress &&
				va < imageBase + s.SecHdr.VirtualAddress + std::max(s.SecHdr.Misc.VirtualSize, s.SecHdr.SizeOfRawData))
				return true;
		return false;
	};
	size_t checked = 0;
	for (auto target : map.Targets()) {
		for (auto& x : map.To(target)) {
			REQUIRE(inCode(x.From));
			checked++;
		}
		if (checked > 20000)
			break;
	}

	// functions that call others are called themselves: some target has many callers
	size_t most = 0;
	for (auto target : map.Targets())
		most = std::max(most, map.CallCount(target));
	CHECK(most > 20);
}

namespace {
	void Put(Bytes& code, size_t at, Bytes const& b) {
		std::copy(b.begin(), b.end(), code.begin() + at);
	}
}

TEST_CASE("A jump table of an x64 switch gives jump targets, and its bytes are not code", "[xref][jumptable]") {
	SyntheticPE spec;
	Bytes code(0x100, 0xCC);
	// 1000 lea rdx,[image base]   1007 mov eax,[rdx+rcx*4+0x1030]   100E add rax,rdx   1011 jmp rax
	Put(code, 0x00, { 0x48, 0x8D, 0x15, 0xF9, 0xEF, 0xFF, 0xFF });
	Put(code, 0x07, { 0x8B, 0x84, 0x8A, 0x30, 0x10, 0x00, 0x00 });
	Put(code, 0x0E, { 0x48, 0x03, 0xC2 });
	Put(code, 0x11, { 0xFF, 0xE0 });
	// the table at 1030: the RVAs 0x10E8 and 0x1000. The first four bytes are also the instruction "call +0x10".
	Put(code, 0x30, { 0xE8, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 });
	Put(code, 0xE8, { 0xC3 });

	auto map = BuildMap(WithCode(spec, code));
	const uint64_t base = 0x140001000;
	CHECK(map.JumpTables() == 1);
	CHECK(Has(map, base + 0xE8, base + 0x11, XrefKind::Jump));
	CHECK(Has(map, base + 0x00, base + 0x11, XrefKind::Jump));
	CHECK(Has(map, base + 0x30, base + 0x11, XrefKind::Data));	// the table itself
	CHECK(map.To(base + 0x45).empty());							// the "call" in the table is not one
	CHECK(map.To(base + 0x108).empty());
}

TEST_CASE("A jump table of an x86 switch", "[xref][jumptable]") {
	SyntheticPE spec;
	spec.Is64 = false;

	SECTION("jmp [index*4 + table], bounded by the comparison") {
		Bytes code(0x100, 0xCC);
		// 1000 cmp eax,2   1003 ja 1030   1005 jmp dword ptr [eax*4+0x401020]
		Put(code, 0x00, { 0x83, 0xF8, 0x02 });
		Put(code, 0x03, { 0x77, 0x2B });
		Put(code, 0x05, { 0xFF, 0x24, 0x85, 0x20, 0x10, 0x40, 0x00 });
		// the table has four entries, but the comparison allows three
		Put(code, 0x20, { 0x40, 0x10, 0x40, 0x00, 0x50, 0x10, 0x40, 0x00, 0x60, 0x10, 0x40, 0x00, 0x70, 0x10, 0x40, 0x00 });
		for (size_t at : { 0x30, 0x40, 0x50, 0x60, 0x70 })
			code[at] = 0xC3;

		auto map = BuildMap(WithCode(spec, code));
		const uint64_t base = 0x401000;
		CHECK(map.JumpTables() == 1);
		CHECK(Has(map, base + 0x40, base + 0x05, XrefKind::Jump));
		CHECK(Has(map, base + 0x50, base + 0x05, XrefKind::Jump));
		CHECK(Has(map, base + 0x60, base + 0x05, XrefKind::Jump));
		CHECK_FALSE(Has(map, base + 0x70, base + 0x05, XrefKind::Jump));
		CHECK(Has(map, base + 0x20, base + 0x05, XrefKind::Data));
		CHECK(Has(map, base + 0x30, base + 0x03, XrefKind::ConditionalJump));
	}
	SECTION("mov eax, [index*4 + table] ... jmp eax") {
		Bytes code(0x100, 0xCC);
		Put(code, 0x00, { 0x8B, 0x04, 0x85, 0x20, 0x10, 0x40, 0x00 });
		Put(code, 0x07, { 0xFF, 0xE0 });
		Put(code, 0x20, { 0x40, 0x10, 0x40, 0x00, 0x50, 0x10, 0x40, 0x00 });
		code[0x40] = 0xC3;
		code[0x50] = 0xC3;

		auto map = BuildMap(WithCode(spec, code));
		CHECK(map.JumpTables() == 1);
		CHECK(Has(map, 0x401040, 0x401007, XrefKind::Jump));
		CHECK(Has(map, 0x401050, 0x401007, XrefKind::Jump));
	}
}

TEST_CASE("Indirect jumps that are not jump tables", "[xref][jumptable]") {
	SyntheticPE spec;
	Bytes code(0x100, 0xCC);
	Put(code, 0x00, { 0xFF, 0xE0 });										// jmp rax
	Put(code, 0x02, { 0xFF, 0x25, 0xE8, 0x20, 0x00, 0x00 });				// jmp [rip+x]: an import thunk
	Put(code, 0x08, { 0x8B, 0x04, 0x8A });									// mov eax,[rdx+rcx*4]: no table, no base
	Put(code, 0x0B, { 0xFF, 0xE0 });
	Put(code, 0x0D, { 0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00 });			// lea rdx,[rip]: not the image base
	Put(code, 0x14, { 0x8B, 0x84, 0x8A, 0x30, 0x10, 0x00, 0x00 });
	Put(code, 0x1B, { 0x48, 0x03, 0xC2 });
	Put(code, 0x1E, { 0xFF, 0xE0 });
	auto map = BuildMap(WithCode(spec, code));
	CHECK(map.JumpTables() == 0);
}

TEST_CASE("A jump table whose entries are not code is not one", "[xref][jumptable]") {
	SyntheticPE spec;
	spec.Is64 = false;
	Bytes code(0x100, 0xCC);
	Put(code, 0x00, { 0xFF, 0x24, 0x85, 0x20, 0x10, 0x40, 0x00 });
	Put(code, 0x20, { 0x00, 0x30, 0x40, 0x00 });	// 0x403000 is in .rdata
	auto map = BuildMap(WithCode(spec, code));
	CHECK(map.JumpTables() == 0);
}
