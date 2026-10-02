#include "TestCommon.h"
#include <algorithm>
#include <CodeAnalysis.h>
#include <FlowGraph.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	void Put(Bytes& code, size_t at, Bytes const& b) {
		std::copy(b.begin(), b.end(), code.begin() + at);
	}

	// a file with 'code' at the start of .text
	struct Graph {
		TempFile File;
		PEFile PE;
		XrefMap Xrefs;

		Graph(SyntheticPE spec, Bytes const& code) : File(Make(spec, code), L".dll") {
			REQUIRE(PE.Open(File.Path()));
			REQUIRE(Xrefs.Build(PE));
		}

		FlowGraph Build(uint64_t entry) const { return BuildFlowGraph(PE, Xrefs, entry); }

		static Bytes Make(SyntheticPE spec, Bytes const& code) {
			auto file = spec.Build();
			std::copy(code.begin(), code.end(), file.begin() + SyntheticPE::TextOffset);
			return file;
		}
	};

	const FlowEdge* FindEdge(FlowGraph const& g, size_t from, size_t to) {
		for (auto const& e : g.Edges)
			if (e.From == from && e.To == to)
				return &e;
		return nullptr;
	}

	size_t BlockAt(FlowGraph const& g, uint64_t start) {
		for (size_t i = 0; i < g.Blocks.size(); i++)
			if (g.Blocks[i].Start == start)
				return i;
		FAIL("no block at " << start);
		return 0;
	}

	// x64, at 0x140001000: a loop that counts up to ecx
	//   00 xor eax,eax
	//   02 cmp eax,ecx        <- loop head
	//   04 jge 0E
	//   06 add eax,1
	//   09 jmp 02
	//   0E ret
	Bytes LoopCode() {
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0x31, 0xC0 });
		Put(code, 0x02, { 0x3B, 0xC1 });
		Put(code, 0x04, { 0x7D, 0x08 });
		Put(code, 0x06, { 0x83, 0xC0, 0x01 });
		Put(code, 0x09, { 0xEB, 0xF7 });
		Put(code, 0x0E, { 0xC3 });
		return code;
	}

	constexpr uint64_t Base64 = 0x140001000;
}

TEST_CASE("The flow graph of a loop", "[flowgraph]") {
	Graph g(SyntheticPE{}, LoopCode());
	auto graph = g.Build(Base64);

	REQUIRE(graph.Blocks.size() == 4);
	CHECK(graph.Entry == Base64);
	CHECK_FALSE(graph.Truncated);
	CHECK(graph.Blocks[graph.EntryBlock].Start == Base64);

	auto a = BlockAt(graph, Base64), head = BlockAt(graph, Base64 + 2), body = BlockAt(graph, Base64 + 6), exit = BlockAt(graph, Base64 + 0xE);
	CHECK(graph.EntryBlock == a);
	CHECK(std::is_sorted(graph.Blocks.begin(), graph.Blocks.end(), [](auto& x, auto& y) { return x.Start < y.Start; }));

	SECTION("blocks") {
		CHECK(graph.Blocks[a].Code.size() == 1);
		CHECK(graph.Blocks[a].Ending == FlowEnd::Fallthrough);
		CHECK(graph.Blocks[a].End == Base64 + 2);

		REQUIRE(graph.Blocks[head].Code.size() == 2);
		CHECK(graph.Blocks[head].Ending == FlowEnd::ConditionalJump);
		CHECK(graph.Blocks[head].Code[0].Mnemonic == "cmp");
		CHECK(graph.Blocks[head].Code[0].Operands == "eax, ecx");
		CHECK(graph.Blocks[head].Code[1].Mnemonic == "jge");
		CHECK(graph.Blocks[head].Code[1].Branch == Base64 + 0xE);

		CHECK(graph.Blocks[body].Code.size() == 2);
		CHECK(graph.Blocks[body].Ending == FlowEnd::Jump);
		CHECK(graph.Blocks[exit].Code.size() == 1);
		CHECK(graph.Blocks[exit].Ending == FlowEnd::Return);
	}
	SECTION("edges") {
		CHECK(graph.Edges.size() == 4);
		REQUIRE(FindEdge(graph, a, head));
		CHECK(FindEdge(graph, a, head)->Kind == FlowEdgeKind::Unconditional);
		REQUIRE(FindEdge(graph, head, exit));
		CHECK(FindEdge(graph, head, exit)->Kind == FlowEdgeKind::True);
		REQUIRE(FindEdge(graph, head, body));
		CHECK(FindEdge(graph, head, body)->Kind == FlowEdgeKind::False);
		REQUIRE(FindEdge(graph, body, head));
		CHECK(FindEdge(graph, body, head)->Kind == FlowEdgeKind::Unconditional);
	}
	SECTION("starting in the middle of the function") {
		auto partial = g.Build(Base64 + 2);
		REQUIRE(partial.Blocks.size() == 3);
		CHECK(partial.Blocks[partial.EntryBlock].Start == Base64 + 2);
	}
}

TEST_CASE("A conditional jump has both a true and a false edge, and the targets are in the right place", "[flowgraph]") {
	// 00 test ecx,ecx   02 jz 07   04 inc eax   06 ret   07 dec eax   09 ret
	Bytes code(0x40, 0xCC);
	Put(code, 0x00, { 0x85, 0xC9 });
	Put(code, 0x02, { 0x74, 0x03 });
	Put(code, 0x04, { 0xFF, 0xC0 });
	Put(code, 0x06, { 0xC3 });
	Put(code, 0x07, { 0xFF, 0xC8 });
	Put(code, 0x09, { 0xC3 });
	Graph g(SyntheticPE{}, code);
	auto graph = g.Build(Base64);

	REQUIRE(graph.Blocks.size() == 3);
	auto top = BlockAt(graph, Base64), no = BlockAt(graph, Base64 + 4), yes = BlockAt(graph, Base64 + 7);
	REQUIRE(FindEdge(graph, top, yes));
	CHECK(FindEdge(graph, top, yes)->Kind == FlowEdgeKind::True);
	REQUIRE(FindEdge(graph, top, no));
	CHECK(FindEdge(graph, top, no)->Kind == FlowEdgeKind::False);
	CHECK(graph.Blocks[no].Ending == FlowEnd::Return);
	CHECK(graph.Blocks[yes].Ending == FlowEnd::Return);
	CHECK(graph.Edges.size() == 2);
}

TEST_CASE("A jump to itself and calls", "[flowgraph]") {
	// 00 call 20   05 dec ecx   07 jnz 05   09 ret   20 ret
	Bytes code(0x40, 0xCC);
	Put(code, 0x00, { 0xE8, 0x1B, 0x00, 0x00, 0x00 });
	Put(code, 0x05, { 0xFF, 0xC9 });
	Put(code, 0x07, { 0x75, 0xFC });
	Put(code, 0x09, { 0xC3 });
	Put(code, 0x20, { 0xC3 });
	Graph g(SyntheticPE{}, code);
	auto graph = g.Build(Base64);

	// the call does not end a block, and the code at 20 is another function
	REQUIRE(graph.Blocks.size() == 3);
	auto start = BlockAt(graph, Base64), loop = BlockAt(graph, Base64 + 5), ret = BlockAt(graph, Base64 + 9);
	CHECK(graph.Blocks[start].Code.size() == 1);
	CHECK(graph.Blocks[start].Code[0].Branch == Base64 + 0x20);
	REQUIRE(FindEdge(graph, loop, loop));
	CHECK(FindEdge(graph, loop, loop)->Kind == FlowEdgeKind::True);
	REQUIRE(FindEdge(graph, loop, ret));
	CHECK(FindEdge(graph, loop, ret)->Kind == FlowEdgeKind::False);
}

TEST_CASE("The flow graph of a switch", "[flowgraph]") {
	SyntheticPE spec;
	Bytes code(0x100, 0xCC);
	// 1000 lea rdx,[image base]   1007 mov eax,[rdx+rcx*4+0x1030]   100E add rax,rdx   1011 jmp rax
	Put(code, 0x00, { 0x48, 0x8D, 0x15, 0xF9, 0xEF, 0xFF, 0xFF });
	Put(code, 0x07, { 0x8B, 0x84, 0x8A, 0x30, 0x10, 0x00, 0x00 });
	Put(code, 0x0E, { 0x48, 0x03, 0xC2 });
	Put(code, 0x11, { 0xFF, 0xE0 });
	// the table: three entries, two of which lead to the same case
	Put(code, 0x30, { 0x80, 0x10, 0x00, 0x00, 0x90, 0x10, 0x00, 0x00, 0x80, 0x10, 0x00, 0x00 });
	Put(code, 0x80, { 0xB0, 0x01, 0xC3 });
	Put(code, 0x90, { 0xB0, 0x02, 0xC3 });
	Graph g(spec, code);
	CHECK(g.Xrefs.JumpTables() == 1);
	CHECK(g.Xrefs.SwitchTargets(Base64 + 0x11).size() >= 3);

	auto graph = g.Build(Base64);
	auto sw = BlockAt(graph, Base64);
	CHECK(graph.Blocks[sw].Ending == FlowEnd::Switch);
	auto one = BlockAt(graph, Base64 + 0x80), two = BlockAt(graph, Base64 + 0x90);
	REQUIRE(FindEdge(graph, sw, one));
	CHECK(FindEdge(graph, sw, one)->Kind == FlowEdgeKind::Case);
	REQUIRE(FindEdge(graph, sw, two));

	// the same target twice is one edge
	size_t toOne = 0;
	for (auto const& e : graph.Edges)
		if (e.From == sw && e.To == one)
			toOne++;
	CHECK(toOne == 1);
}

TEST_CASE("Jumps that leave the function", "[flowgraph]") {
	SECTION("a tail call") {
		// 00 call 20   05 jmp 20   20 ret : 20 is a function that is called
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0xE8, 0x1B, 0x00, 0x00, 0x00 });
		Put(code, 0x05, { 0xE9, 0x16, 0x00, 0x00, 0x00 });
		Put(code, 0x20, { 0xC3 });
		Graph g(SyntheticPE{}, code);
		auto graph = g.Build(Base64);
		// the jump is a block of its own
		REQUIRE(graph.Blocks.size() == 2);
		CHECK(graph.Blocks[0].Ending == FlowEnd::Fallthrough);
		CHECK(graph.Blocks[0].Code.size() == 1);
		CHECK(graph.Blocks[1].Ending == FlowEnd::TailCall);
		CHECK(graph.Blocks[1].Code.size() == 1);
		REQUIRE(graph.Edges.size() == 1);
		CHECK(graph.Edges[0].Kind == FlowEdgeKind::Unconditional);
	}
	SECTION("a jump through an import slot, and one to an unknown register") {
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0xFF, 0x25, 0xFA, 0x1F, 0x00, 0x00 });	// jmp [rip+x]
		Put(code, 0x10, { 0xFF, 0xE0 });							// jmp rax
		Graph g(SyntheticPE{}, code);
		auto first = g.Build(Base64);
		REQUIRE(first.Blocks.size() == 1);
		CHECK(first.Blocks[0].Ending == FlowEnd::IndirectJump);
		auto second = g.Build(Base64 + 0x10);
		REQUIRE(second.Blocks.size() == 1);
		CHECK(second.Blocks[0].Ending == FlowEnd::IndirectJump);
	}
	SECTION("a jump through an import slot after other code is a block of its own") {
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0x48, 0x83, 0xC1, 0x08 });				// add rcx,8
		Put(code, 0x04, { 0xFF, 0x25, 0xF6, 0x1F, 0x00, 0x00 });	// jmp [rip+x]
		Graph g(SyntheticPE{}, code);
		auto graph = g.Build(Base64);
		REQUIRE(graph.Blocks.size() == 2);
		CHECK(graph.Blocks[1].Ending == FlowEnd::IndirectJump);
		CHECK(graph.Blocks[1].Code.size() == 1);
	}
	SECTION("a jump out of the image") {
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0xE9, 0x00, 0x80, 0x00, 0x00 });
		Graph g(SyntheticPE{}, code);
		auto graph = g.Build(Base64);
		REQUIRE(graph.Blocks.size() == 1);
		CHECK(graph.Blocks[0].Ending == FlowEnd::Invalid);
		CHECK(graph.Edges.empty());
	}
}

TEST_CASE("Code that does not return, and what is not code", "[flowgraph]") {
	SECTION("int3, ud2 and fast fail end the flow") {
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0x0F, 0x0B });			// ud2
		Put(code, 0x10, { 0xB9, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x29 });	// mov ecx,5 ; int 0x29
		Graph g(SyntheticPE{}, code);
		auto first = g.Build(Base64);
		REQUIRE(first.Blocks.size() == 1);
		CHECK(first.Blocks[0].Ending == FlowEnd::Trap);
		auto second = g.Build(Base64 + 0x10);
		REQUIRE(second.Blocks.size() == 1);
		CHECK(second.Blocks[0].Ending == FlowEnd::Trap);
		CHECK(second.Blocks[0].Code.size() == 2);
	}
	SECTION("no code at the entry") {
		Graph g(SyntheticPE{}, LoopCode());
		CHECK(g.Build(0x140009000).Blocks.empty());		// outside of the image
		CHECK(g.Build(0x140002000).Blocks.empty());		// not in a code section
		CHECK(g.Build(0).Blocks.empty());
	}
	SECTION("the code runs into bytes that are not an instruction") {
		Bytes code(0x40, 0xCC);
		Put(code, 0x00, { 0x31, 0xC0, 0x06 });		// xor eax,eax and a byte that is not valid in 64-bit code
		Graph g(SyntheticPE{}, code);
		auto graph = g.Build(Base64);
		REQUIRE(graph.Blocks.size() == 1);
		CHECK(graph.Blocks[0].Ending == FlowEnd::Invalid);
		CHECK(graph.Blocks[0].Code.size() == 1);
	}
}

TEST_CASE("The size of a flow graph is limited", "[flowgraph]") {
	Bytes code(0x1F0, 0x90);	// a run of nops
	code[0x1EF] = 0xC3;
	Graph g(SyntheticPE{}, code);
	auto limited = BuildFlowGraph(g.PE, g.Xrefs, Base64, 100);
	CHECK(limited.Truncated);
	CHECK(limited.Blocks.size() <= 1);
	auto whole = BuildFlowGraph(g.PE, g.Xrefs, Base64);
	CHECK_FALSE(whole.Truncated);
	REQUIRE(whole.Blocks.size() == 1);
	CHECK(whole.Blocks[0].Code.size() == 0x1F0);
}

TEST_CASE("x86 code and the function of an address", "[flowgraph]") {
	SyntheticPE spec;
	spec.Is64 = false;
	// 00 call 20   05 ret   20 push ebp   21 mov ebp,esp   23 cmp dword ptr [ebp+8],0   27 jz 2D   29..2C nop   2D pop ebp   2E ret
	Bytes code(0x80, 0xCC);
	Put(code, 0x00, { 0xE8, 0x1B, 0x00, 0x00, 0x00 });
	Put(code, 0x05, { 0xC3 });
	Put(code, 0x20, { 0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x04, 0x90, 0x90, 0x90, 0x90, 0x5D, 0xC3 });
	Graph g(spec, code);

	auto graph = g.Build(0x401020);
	CHECK(graph.Blocks.size() == 3);

	CHECK(FindFunctionStart(g.PE, g.Xrefs, 0x401029) == 0x401020);	// the closest function that is called
	CHECK(FindFunctionStart(g.PE, g.Xrefs, 0x401020) == 0x401020);
	CHECK(FindFunctionStart(g.PE, g.Xrefs, 0x401002) == 0x401002);	// before any function that is called
	CHECK(FindFunctionStart(g.PE, g.Xrefs, 0x10) == 0x10);			// not in the image
}

namespace {
	// a graph of 'n' blocks with the given edges, for the layout
	FlowGraph MakeGraph(size_t n, std::vector<std::pair<size_t, size_t>> const& edges) {
		FlowGraph g;
		g.Blocks.resize(n);
		for (size_t i = 0; i < n; i++)
			g.Blocks[i].Start = i * 16;
		for (auto [from, to] : edges)
			g.Edges.push_back({ from, to, FlowEdgeKind::Unconditional });
		return g;
	}

	bool Overlap(FlowPoint a, FlowSize sa, FlowPoint b, FlowSize sb) {
		return std::abs(a.X - b.X) < (sa.Width + sb.Width) / 2 && std::abs(a.Y - b.Y) < (sa.Height + sb.Height) / 2;
	}

	void CheckNoOverlap(std::vector<FlowPoint> const& pos, std::vector<FlowSize> const& sizes) {
		for (size_t i = 0; i < pos.size(); i++) {
			CHECK(pos[i].X - sizes[i].Width / 2 >= -0.01f);
			CHECK(pos[i].Y - sizes[i].Height / 2 >= -0.01f);
			for (size_t j = i + 1; j < pos.size(); j++)
				CHECK_FALSE(Overlap(pos[i], sizes[i], pos[j], sizes[j]));
		}
	}
}

TEST_CASE("Layout puts blocks below the blocks that lead to them", "[flowgraph][layout]") {
	// a diamond:  0 -> 1, 2;  1, 2 -> 3
	auto g = MakeGraph(4, { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 2, 3 } });
	std::vector<FlowSize> sizes{ { 100, 40 }, { 150, 60 }, { 90, 30 }, { 100, 40 } };
	auto pos = LayoutFlowGraph(g, sizes).Blocks;
	REQUIRE(pos.size() == 4);

	CHECK(pos[0].Y < pos[1].Y);
	CHECK(pos[0].Y < pos[2].Y);
	CHECK(pos[1].Y == Catch::Approx(pos[2].Y));
	CHECK(pos[1].Y < pos[3].Y);
	CHECK(pos[2].Y < pos[3].Y);
	CheckNoOverlap(pos, sizes);

	// the top block is above the middle of the two below it, and so is the bottom one
	CHECK(pos[0].X == Catch::Approx((pos[1].X + pos[2].X) / 2).margin(1.0));
	CHECK(pos[3].X == Catch::Approx((pos[1].X + pos[2].X) / 2).margin(1.0));

	SECTION("the gaps") {
		FlowLayoutOptions options;
		options.HorizontalGap = 100;
		options.VerticalGap = 80;
		auto wide = LayoutFlowGraph(g, sizes, options).Blocks;
		CHECK(std::abs(wide[1].X - wide[2].X) >= (150 + 90) / 2.0f + 100 - 0.01f);
		CHECK(wide[1].Y - wide[0].Y >= (40 + 60) / 2.0f + 80 - 0.01f);
		CheckNoOverlap(wide, sizes);
	}
}

namespace {
	bool Inside(FlowPoint p, FlowPoint center, FlowSize size) {
		return std::abs(p.X - center.X) < size.Width / 2 && std::abs(p.Y - center.Y) < size.Height / 2;
	}
}

TEST_CASE("Edges that skip layers have bends that go around the blocks in between", "[flowgraph][layout]") {
	SECTION("a long edge and a loop") {
		// 0 -> 1 -> 2 -> 3, the long edge 0 -> 3 and the loop 3 -> 1
		auto g = MakeGraph(4, { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 0, 3 }, { 3, 1 } });
		std::vector<FlowSize> sizes(4, FlowSize{ 100, 40 });
		auto layout = LayoutFlowGraph(g, sizes);
		auto& pos = layout.Blocks;
		REQUIRE(layout.Edges.size() == 5);

		CHECK(layout.Edges[0].empty());		// edges between neighboring layers are straight
		CHECK(layout.Edges[1].empty());
		CHECK(layout.Edges[2].empty());

		REQUIRE(layout.Edges[3].size() == 2);
		CHECK(layout.Edges[3][0].Y == Catch::Approx(pos[1].Y));
		CHECK(layout.Edges[3][1].Y == Catch::Approx(pos[2].Y));
		CHECK_FALSE(Inside(layout.Edges[3][0], pos[1], sizes[1]));
		CHECK_FALSE(Inside(layout.Edges[3][1], pos[2], sizes[2]));

		REQUIRE(layout.Edges[4].size() == 1);
		CHECK(layout.Edges[4][0].Y == Catch::Approx(pos[2].Y));
		CHECK_FALSE(Inside(layout.Edges[4][0], pos[2], sizes[2]));

		// the two bends in the same layer are apart
		CHECK(std::abs(layout.Edges[3][1].X - layout.Edges[4][0].X) > 5.0f);
		for (auto& edge : layout.Edges)
			for (auto& bend : edge) {
				CHECK(bend.X >= 0);
				CHECK(bend.Y >= 0);
			}
	}
	SECTION("the bends of a loop are in the direction of the edge: from the lower block up") {
		auto g = MakeGraph(5, { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 0 } });
		std::vector<FlowSize> sizes(5, FlowSize{ 100, 40 });
		auto layout = LayoutFlowGraph(g, sizes);
		REQUIRE(layout.Edges[4].size() == 3);
		CHECK(layout.Edges[4][0].Y > layout.Edges[4][1].Y);
		CHECK(layout.Edges[4][1].Y > layout.Edges[4][2].Y);
		CHECK(layout.Edges[4][0].Y < layout.Blocks[4].Y);
		CHECK(layout.Edges[4][2].Y > layout.Blocks[0].Y);
	}
	SECTION("a jump to itself has no bends") {
		auto g = MakeGraph(2, { { 0, 0 }, { 0, 1 } });
		std::vector<FlowSize> sizes(2, FlowSize{ 100, 40 });
		auto layout = LayoutFlowGraph(g, sizes);
		CHECK(layout.Edges[0].empty());
		CHECK(layout.Edges[1].empty());
	}
}

TEST_CASE("Layout of chains, fans and loops", "[flowgraph][layout]") {
	SECTION("a single block") {
		auto g = MakeGraph(1, {});
		std::vector<FlowSize> sizes{ { 100, 40 } };
		auto pos = LayoutFlowGraph(g, sizes).Blocks;
		REQUIRE(pos.size() == 1);
		CHECK(pos[0].X == Catch::Approx(50));
		CHECK(pos[0].Y == Catch::Approx(20));
	}
	SECTION("a block that jumps to itself") {
		auto g = MakeGraph(2, { { 0, 0 }, { 0, 1 } });
		std::vector<FlowSize> sizes{ { 100, 40 }, { 100, 40 } };
		auto pos = LayoutFlowGraph(g, sizes).Blocks;
		CHECK(pos[0].Y < pos[1].Y);
	}
	SECTION("a long edge puts its target below the blocks in between") {
		auto g = MakeGraph(4, { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 0, 3 } });
		std::vector<FlowSize> sizes(4, FlowSize{ 100, 40 });
		auto pos = LayoutFlowGraph(g, sizes).Blocks;
		CHECK(pos[0].Y < pos[1].Y);
		CHECK(pos[1].Y < pos[2].Y);
		CHECK(pos[2].Y < pos[3].Y);
		CheckNoOverlap(pos, sizes);
	}
	SECTION("a wide fan") {
		std::vector<std::pair<size_t, size_t>> edges;
		for (size_t i = 1; i <= 12; i++) {
			edges.push_back({ 0, i });
			edges.push_back({ i, 13 });
		}
		auto g = MakeGraph(14, edges);
		std::vector<FlowSize> sizes(14, FlowSize{ 120, 50 });
		auto pos = LayoutFlowGraph(g, sizes).Blocks;
		CheckNoOverlap(pos, sizes);
		for (size_t i = 1; i <= 12; i++)
			CHECK(pos[i].Y == Catch::Approx(pos[1].Y));
		CHECK(pos[0].X == Catch::Approx(pos[13].X).margin(1.0));
	}
	SECTION("blocks that cannot be reached from the entry are still placed") {
		auto g = MakeGraph(3, { { 0, 1 } });
		std::vector<FlowSize> sizes(3, FlowSize{ 100, 40 });
		auto pos = LayoutFlowGraph(g, sizes).Blocks;
		CheckNoOverlap(pos, sizes);
	}
	SECTION("nothing, or too few sizes") {
		CHECK(LayoutFlowGraph(FlowGraph{}, {}).Blocks.empty());
		auto g = MakeGraph(2, { { 0, 1 } });
		std::vector<FlowSize> sizes{ { 10, 10 } };
		CHECK(LayoutFlowGraph(g, sizes).Blocks.size() == 2);
	}
}

TEST_CASE("The flow graph of functions of a system DLL", "[flowgraph][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	PEFile pe;
	if (!pe.Open(std::wstring(dir) + L"\\kernel32.dll"))
		SKIP("kernel32.dll could not be opened");
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));

	// every function of the exception directory
	auto exceptions = pe.GetExceptions();
	if (!exceptions || exceptions->empty())
		SKIP("no exception directory");

	size_t graphs = 0, withBranches = 0, switches = 0, loops = 0;
	auto base = pe.GetImageBase();
	for (size_t i = 0; i < exceptions->size() && graphs < 400; i += std::max<size_t>(1, exceptions->size() / 400)) {
		auto entry = base + (*exceptions)[i].RuntimeFuncEntry.BeginAddress;
		auto graph = BuildFlowGraph(pe, xrefs, entry);
		if (graph.Blocks.empty())
			continue;
		graphs++;

		REQUIRE(graph.EntryBlock < graph.Blocks.size());
		CHECK(graph.Blocks[graph.EntryBlock].Start == entry);
		for (auto const& b : graph.Blocks) {
			REQUIRE_FALSE(b.Code.empty());
			CHECK(b.Code.front().Address == b.Start);
			CHECK(b.Code.back().Address + b.Code.back().Size == b.End);
			if (b.Ending == FlowEnd::Switch)
				switches++;
		}

		// conditional blocks lead to their true and false target, and nothing else has these kinds of edges
		std::vector<int> trues(graph.Blocks.size()), falses(graph.Blocks.size());
		for (auto const& e : graph.Edges) {
			REQUIRE(e.From < graph.Blocks.size());
			REQUIRE(e.To < graph.Blocks.size());
			if (e.Kind == FlowEdgeKind::True)
				trues[e.From]++;
			if (e.Kind == FlowEdgeKind::False)
				falses[e.From]++;
			if (graph.Blocks[e.To].Start <= graph.Blocks[e.From].Start)
				loops++;
		}
		for (size_t b = 0; b < graph.Blocks.size(); b++) {
			CHECK(trues[b] <= 1);
			CHECK(falses[b] <= 1);
			if (graph.Blocks[b].Ending != FlowEnd::ConditionalJump) {
				CHECK(trues[b] == 0);
				CHECK(falses[b] == 0);
			}
		}
		if (graph.Blocks.size() > 1)
			withBranches++;

		// the layout of a real function has no overlaps
		std::vector<FlowSize> sizes;
		for (auto const& b : graph.Blocks)
			sizes.push_back({ 60.0f + 7.0f * b.Code.size(), 20.0f + 14.0f * b.Code.size() });
		if (graph.Blocks.size() <= 150) {
			auto layout = LayoutFlowGraph(graph, sizes);
			auto& pos = layout.Blocks;
			for (size_t a = 0; a < pos.size(); a++)
				for (size_t c = a + 1; c < pos.size(); c++)
					REQUIRE_FALSE(Overlap(pos[a], sizes[a], pos[c], sizes[c]));
			// the bends of the edges are not in a block
			for (auto const& edge : layout.Edges)
				for (auto const& bend : edge)
					for (size_t a = 0; a < pos.size(); a++)
						REQUIRE_FALSE(Inside(bend, pos[a], sizes[a]));
		}
	}
	CHECK(graphs > 100);
	CHECK(withBranches > 50);
	CHECK(loops > 0);
	INFO("blocks ending in a switch: " << switches);
}
