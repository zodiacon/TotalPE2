#include "pch.h"
#include "FlowGraph.h"
#include "CodeAnalysis.h"
#include <PEFile.h>
#include <capstone/capstone.h>
#include <algorithm>
#include <map>
#include <set>

const wchar_t* FlowEdgeKindToString(FlowEdgeKind kind) {
	switch (kind) {
		case FlowEdgeKind::Unconditional: return L"Unconditional";
		case FlowEdgeKind::True: return L"True";
		case FlowEdgeKind::False: return L"False";
		case FlowEdgeKind::Case: return L"Case";
	}
	return L"";
}

namespace {
	enum class Kind : uint8_t {
		Normal,
		Call,
		Jump,			// jmp to an address
		Conditional,	// jcc, loop, jrcxz...
		Indirect,		// jmp reg / jmp [mem]
		Return,
		Trap,
	};

	struct Decoded {
		FlowInstruction Inst;
		Kind Kind{ Kind::Normal };
		uint64_t Next() const { return Inst.Address + Inst.Size; }
	};

	struct Range {
		uint64_t Start, End;
		bool Contains(uint64_t va) const { return va >= Start && va < End; }
	};

	class Decoder {
	public:
		Decoder(PEFile const& pe) : m_PE(pe) {
			auto info = pe.GetFileInfo();
			auto nt = pe.GetNTHeader();
			auto sections = pe.GetSecHeaders();
			if (!info || !nt || !sections)
				return;
			auto machine = nt->NTHdr32.FileHeader.Machine;
			m_Is64 = machine == IMAGE_FILE_MACHINE_AMD64;
			if (!m_Is64 && machine != IMAGE_FILE_MACHINE_I386)
				return;
			m_Base = pe.GetImageBase();
			for (auto const& s : *sections)
				if (s.SecHdr.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))
					m_Code.push_back({ m_Base + s.SecHdr.VirtualAddress, m_Base + s.SecHdr.VirtualAddress + std::max(s.SecHdr.Misc.VirtualSize, s.SecHdr.SizeOfRawData) });
			if (cs_open(CS_ARCH_X86, m_Is64 ? CS_MODE_64 : CS_MODE_32, &m_Handle) != CS_ERR_OK)
				return;
			cs_option(m_Handle, CS_OPT_DETAIL, CS_OPT_ON);
			m_Inst = cs_malloc(m_Handle);
		}

		~Decoder() {
			if (m_Inst)
				cs_free(m_Inst, 1);
			if (m_Handle)
				cs_close(&m_Handle);
		}

		bool Valid() const { return m_Inst != nullptr; }

		bool IsCode(uint64_t va) const {
			return std::any_of(m_Code.begin(), m_Code.end(), [&](Range const& r) { return r.Contains(va); });
		}

		bool Decode(uint64_t va, Decoded& d) {
			if (!IsCode(va))
				return false;
			auto offset = m_PE.GetOffsetFromRVA(va - m_Base);
			if (offset == 0 || offset >= m_PE.GetFileSize())
				return false;
			auto span = m_PE.GetSpan((uint32_t)offset, std::min<uint32_t>(16, m_PE.GetFileSize() - (uint32_t)offset));
			auto bytes = (const uint8_t*)span.data();
			size_t left = span.size();
			uint64_t address = va;
			if (!cs_disasm_iter(m_Handle, &bytes, &left, &address, m_Inst))
				return false;

			auto const& inst = *m_Inst;
			d.Inst.Address = inst.address;
			d.Inst.Size = (uint8_t)inst.size;
			d.Inst.Mnemonic = inst.mnemonic;
			d.Inst.Operands = inst.op_str;
			auto refs = GetInstructionRefs(inst, m_Is64);
			d.Inst.Branch = refs.Branch;
			d.Inst.Memory = refs.Memory;
			d.Kind = Classify(inst);
			return true;
		}

	private:
		static Kind Classify(cs_insn const& inst) {
			switch (inst.id) {
				case X86_INS_RET: case X86_INS_RETF: case X86_INS_IRET: case X86_INS_IRETD: case X86_INS_IRETQ:
					return Kind::Return;
				case X86_INS_UD2: case X86_INS_HLT: case X86_INS_INT3:
					return Kind::Trap;
				case X86_INS_INT:
					// int 0x29 is a fast fail: it does not return
					if (inst.detail && inst.detail->x86.op_count == 1 && inst.detail->x86.operands[0].type == X86_OP_IMM && inst.detail->x86.operands[0].imm == 0x29)
						return Kind::Trap;
					return Kind::Normal;
			}
			if (!inst.detail)
				return Kind::Normal;

			bool call = false, jump = false;
			for (int i = 0; i < inst.detail->groups_count; i++) {
				if (inst.detail->groups[i] == CS_GRP_CALL)
					call = true;
				else if (inst.detail->groups[i] == CS_GRP_JUMP)
					jump = true;
			}
			if (call)
				return Kind::Call;
			if (!jump)
				return Kind::Normal;

			bool direct = inst.detail->x86.op_count >= 1 && inst.detail->x86.operands[0].type == X86_OP_IMM;
			if (inst.id == X86_INS_JMP)
				return direct ? Kind::Jump : Kind::Indirect;
			return direct ? Kind::Conditional : Kind::Indirect;
		}

		PEFile const& m_PE;
		bool m_Is64{ false };
		uint64_t m_Base{ 0 };
		std::vector<Range> m_Code;
		csh m_Handle{ 0 };
		cs_insn* m_Inst{ nullptr };
	};
}

FlowGraph BuildFlowGraph(PEFile const& pe, XrefMap const& xrefs, uint64_t entry, size_t maxInstructions) {
	FlowGraph graph;
	graph.Entry = entry;

	Decoder decoder(pe);
	if (!decoder.Valid())
		return graph;

	// 1. follow the code from the entry: every instruction that can be reached, and where the blocks start
	std::map<uint64_t, Decoded> code;
	std::set<uint64_t> leaders;
	std::vector<uint64_t> work;
	auto lead = [&](uint64_t va) {
		if (leaders.insert(va).second)
			work.push_back(va);
	};
	lead(entry);

	while (!work.empty()) {
		auto address = work.back();
		work.pop_back();

		while (!code.contains(address)) {
			if (code.size() >= maxInstructions) {
				graph.Truncated = true;
				break;
			}
			Decoded d;
			if (!decoder.Decode(address, d))
				break;
			code.emplace(address, d);
			auto next = d.Next();

			if (d.Kind == Kind::Conditional) {
				if (d.Inst.Branch && decoder.IsCode(*d.Inst.Branch))
					lead(*d.Inst.Branch);
				leaders.insert(next);
				address = next;
				continue;
			}
			if (d.Kind == Kind::Jump) {
				auto target = *d.Inst.Branch;
				bool tailCall = !(decoder.IsCode(target) && !(target != entry && xrefs.CallCount(target) > 0));
				if (!tailCall)
					lead(target);
				else
					leaders.insert(address);	// a jump to another function is a block of its own
				break;
			}
			if (d.Kind == Kind::Indirect) {
				for (auto target : xrefs.SwitchTargets(address))
					if (decoder.IsCode(target))
						lead(target);
				if (xrefs.SwitchTargets(address).empty() && d.Inst.Memory)
					leaders.insert(address);	// jmp [import slot]: also a jump to another function
				break;
			}
			if (d.Kind == Kind::Return || d.Kind == Kind::Trap)
				break;
			address = next;
		}
		if (graph.Truncated)
			break;
	}

	// 2. cut the code into blocks: from a leader to an instruction that ends the flow or to the next leader
	std::vector<uint64_t> starts;
	for (auto va : leaders)
		if (code.contains(va))
			starts.push_back(va);
	std::map<uint64_t, size_t> blockOf;
	for (auto va : starts)
		blockOf.emplace(va, blockOf.size());

	auto edge = [&](size_t from, uint64_t target, FlowEdgeKind kind) {
		auto it = blockOf.find(target);
		if (it == blockOf.end())
			return;
		for (auto const& e : graph.Edges)
			if (e.From == from && e.To == it->second && e.Kind == kind)
				return;
		graph.Edges.push_back({ from, it->second, kind });
	};

	graph.Blocks.resize(starts.size());
	for (size_t i = 0; i < starts.size(); i++) {
		auto& block = graph.Blocks[i];
		block.Start = starts[i];
		auto address = starts[i];
		for (;;) {
			auto it = code.find(address);
			if (it == code.end()) {
				block.Ending = FlowEnd::Invalid;
				break;
			}
			auto const& d = it->second;
			block.Code.push_back(d.Inst);
			block.End = d.Next();

			bool done = true;
			switch (d.Kind) {
				case Kind::Return:
					block.Ending = FlowEnd::Return;
					break;
				case Kind::Trap:
					block.Ending = FlowEnd::Trap;
					break;
				case Kind::Jump: {
					auto target = *d.Inst.Branch;
					if (blockOf.contains(target)) {
						block.Ending = FlowEnd::Jump;
						edge(i, target, FlowEdgeKind::Unconditional);
					}
					else {
						block.Ending = decoder.IsCode(target) ? FlowEnd::TailCall : FlowEnd::Invalid;
					}
					break;
				}
				case Kind::Conditional:
					block.Ending = FlowEnd::ConditionalJump;
					if (d.Inst.Branch)
						edge(i, *d.Inst.Branch, FlowEdgeKind::True);
					edge(i, d.Next(), FlowEdgeKind::False);
					break;
				case Kind::Indirect: {
					auto cases = xrefs.SwitchTargets(address);
					block.Ending = cases.empty() ? FlowEnd::IndirectJump : FlowEnd::Switch;
					for (auto target : cases)
						edge(i, target, FlowEdgeKind::Case);
					break;
				}
				default:
					done = false;
					break;
			}
			if (done)
				break;

			if (leaders.contains(d.Next())) {
				block.Ending = FlowEnd::Fallthrough;
				if (blockOf.contains(d.Next()))
					edge(i, d.Next(), FlowEdgeKind::Unconditional);
				else
					block.Ending = FlowEnd::Invalid;
				break;
			}
			address = d.Next();
		}
	}

	if (auto it = blockOf.find(entry); it != blockOf.end())
		graph.EntryBlock = it->second;
	else
		graph.Blocks.clear(), graph.Edges.clear();
	return graph;
}

namespace {
	std::optional<IMAGE_RUNTIME_FUNCTION_ENTRY> FunctionEntry(PEFile const& pe, uint32_t rva) {
		auto exceptions = pe.GetExceptions();
		if (!exceptions)
			return std::nullopt;
		for (auto const& e : *exceptions) {
			auto const& f = e.RuntimeFuncEntry;
			if (rva >= f.BeginAddress && rva < f.EndAddress)
				return f;
		}
		return std::nullopt;
	}

	// a piece of a function that was split off (cold code) refers to the entry that it belongs to in its unwind information
	std::optional<IMAGE_RUNTIME_FUNCTION_ENTRY> ParentEntry(PEFile const& pe, IMAGE_RUNTIME_FUNCTION_ENTRY const& f) {
		constexpr uint8_t ChainInfo = 4;
		if (f.UnwindData & 1)
			return std::nullopt;
		auto offset = pe.GetOffsetFromRVA(f.UnwindData);
		if (offset == 0 || offset + 4 > pe.GetFileSize())
			return std::nullopt;
		auto info = pe.GetSpan((uint32_t)offset, 4);
		auto flags = (uint8_t)info[0] >> 3;
		if (!(flags & ChainInfo))
			return std::nullopt;
		auto codes = (uint8_t)info[2];
		auto chained = offset + 4 + ((codes + 1) & ~1u) * 2;
		if (chained + sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY) > pe.GetFileSize())
			return std::nullopt;
		IMAGE_RUNTIME_FUNCTION_ENTRY parent;
		memcpy(&parent, pe.GetData() + chained, sizeof(parent));
		return parent;
	}
}

uint64_t FindFunctionStart(PEFile const& pe, XrefMap const& xrefs, uint64_t va) {
	auto base = pe.GetImageBase();
	if (va < base || va - base > 0xFFFFFFFFULL)
		return va;
	auto rva = (uint32_t)(va - base);

	if (auto f = FunctionEntry(pe, rva)) {
		for (int i = 0; i < 16; i++) {
			auto parent = ParentEntry(pe, *f);
			if (!parent)
				break;
			f = parent;
		}
		return base + f->BeginAddress;
	}

	// no exception directory (32-bit code): the closest function that is called
	uint64_t best = 0;
	for (auto target : xrefs.Targets()) {
		if (target > va)
			break;
		if (xrefs.CallCount(target) > 0)
			best = target;
	}
	return best && va - best < 0x10000 ? best : va;
}

namespace {
	// Depth first from the entry, true branches first. An edge to a block that is still being visited closes a loop: the pairs
	// of blocks that are connected by such an edge are returned in 'back'. 'order' is the order in which the blocks were found.
	void FindLoops(FlowGraph const& g, std::vector<std::vector<size_t>>& succ, std::set<std::pair<size_t, size_t>>& back, std::vector<float>& order) {
		auto n = g.Blocks.size();
		std::vector<std::vector<size_t>> all(n);
		for (auto const& e : g.Edges)
			if (e.From != e.To && e.From < n && e.To < n && std::find(all[e.From].begin(), all[e.From].end(), e.To) == all[e.From].end())
				all[e.From].push_back(e.To);

		succ.assign(n, {});
		order.assign(n, 0.0f);
		std::vector<uint8_t> state(n, 0);	// 1: on the stack, 2: done
		size_t counter = 0;

		auto visit = [&](size_t root) {
			std::vector<std::pair<size_t, size_t>> stack;	// block, next successor to look at
			state[root] = 1;
			order[root] = (float)counter++;
			stack.push_back({ root, 0 });
			while (!stack.empty()) {
				auto& [v, next] = stack.back();
				if (next == all[v].size()) {
					state[v] = 2;
					stack.pop_back();
					continue;
				}
				auto w = all[v][next++];
				if (state[w] == 1) {
					back.insert({ v, w });
					continue;
				}
				succ[v].push_back(w);
				if (state[w] == 0) {
					state[w] = 1;
					order[w] = (float)counter++;
					stack.push_back({ w, 0 });
				}
			}
		};
		if (n)
			visit(std::min(g.EntryBlock, n - 1));
		for (size_t i = 0; i < n; i++)
			if (state[i] == 0)
				visit(i);
	}

	// Places the nodes of one layer, in their order, as close to the wanted positions as the distance between them allows:
	// the least squares fit to the wanted positions with a minimum distance, found by pooling neighbors that are out of order.
	template<typename Distance>
	void Place(std::vector<size_t> const& layer, std::vector<float> const& wanted, Distance distance, std::vector<float>& x) {
		auto k = layer.size();
		std::vector<float> offset(k, 0.0f);
		for (size_t i = 1; i < k; i++)
			offset[i] = offset[i - 1] + distance(layer[i - 1], layer[i]);

		struct Pool {
			float Sum;
			int Count;
			size_t First;
			float Mean() const { return Sum / Count; }
		};
		std::vector<Pool> pools;
		for (size_t i = 0; i < k; i++) {
			pools.push_back({ wanted[layer[i]] - offset[i], 1, i });
			while (pools.size() > 1 && pools[pools.size() - 2].Mean() > pools.back().Mean()) {
				auto last = pools.back();
				pools.pop_back();
				pools.back().Sum += last.Sum;
				pools.back().Count += last.Count;
			}
		}
		for (size_t p = 0; p < pools.size(); p++) {
			auto end = p + 1 < pools.size() ? pools[p + 1].First : k;
			for (auto i = pools[p].First; i < end; i++)
				x[layer[i]] = pools[p].Mean() + offset[i];
		}
	}
}

FlowLayout LayoutFlowGraph(FlowGraph const& graph, std::span<const FlowSize> sizes, FlowLayoutOptions options) {
	auto n = graph.Blocks.size();
	FlowLayout result;
	result.Blocks.resize(n);
	result.Edges.resize(graph.Edges.size());
	if (n == 0 || sizes.size() < n)
		return result;

	std::vector<std::vector<size_t>> forward;
	std::set<std::pair<size_t, size_t>> back;
	std::vector<float> order;
	FindLoops(graph, forward, back, order);

	// layers of the blocks: one below the lowest of the blocks that lead to the block, not counting the edges that close loops
	std::vector<size_t> layerOf(n, 0), pending(n, 0);
	for (size_t v = 0; v < n; v++)
		for (auto w : forward[v])
			pending[w]++;
	std::vector<size_t> ready;
	for (size_t i = 0; i < n; i++)
		if (pending[i] == 0)
			ready.push_back(i);
	size_t layers = 1;
	while (!ready.empty()) {
		auto v = ready.back();
		ready.pop_back();
		for (auto w : forward[v]) {
			layerOf[w] = std::max(layerOf[w], layerOf[v] + 1);
			layers = std::max(layers, layerOf[w] + 1);
			if (--pending[w] == 0)
				ready.push_back(w);
		}
	}

	// the graph that is laid out: the blocks, and a node in every layer that an edge crosses, from the upper block to the lower.
	// An edge that closes a loop is reversed, so that it goes down too.
	std::vector<std::vector<size_t>> succ(n), pred(n);
	std::vector<float> width(n), height(n);
	for (size_t i = 0; i < n; i++) {
		width[i] = sizes[i].Width;
		height[i] = sizes[i].Height;
	}
	constexpr float BendWidth = 8.0f;
	std::vector<std::vector<size_t>> chains(graph.Edges.size());	// the bends of the edges, from the upper block to the lower
	std::vector<bool> reversed(graph.Edges.size(), false);

	auto link = [&](size_t upper, size_t lower) {
		if (std::find(succ[upper].begin(), succ[upper].end(), lower) == succ[upper].end()) {
			succ[upper].push_back(lower);
			pred[lower].push_back(upper);
		}
	};
	for (size_t i = 0; i < graph.Edges.size(); i++) {
		auto const& e = graph.Edges[i];
		if (e.From == e.To || e.From >= n || e.To >= n)
			continue;
		reversed[i] = back.contains({ e.From, e.To });
		auto upper = reversed[i] ? e.To : e.From;
		auto lower = reversed[i] ? e.From : e.To;
		if (layerOf[lower] <= layerOf[upper])
			continue;

		auto previous = upper;
		for (auto l = layerOf[upper] + 1; l < layerOf[lower]; l++) {
			auto bend = width.size();
			width.push_back(BendWidth);
			height.push_back(0.0f);
			layerOf.push_back(l);
			order.push_back(order[upper] + 0.5f);
			succ.emplace_back();
			pred.emplace_back();
			link(previous, bend);
			chains[i].push_back(bend);
			previous = bend;
		}
		link(previous, lower);
	}

	auto total = width.size();
	std::vector<std::vector<size_t>> layer(layers);
	for (size_t i = 0; i < total; i++)
		layer[layerOf[i]].push_back(i);
	for (auto& l : layer)
		std::stable_sort(l.begin(), l.end(), [&](size_t a, size_t b) { return order[a] < order[b]; });

	// the order in the layers: sweep down and up, putting each node at the average place of its neighbors
	std::vector<float> index(total), key(total);
	auto reindex = [&](std::vector<size_t> const& l) {
		for (size_t i = 0; i < l.size(); i++)
			index[l[i]] = (float)i;
	};
	for (auto const& l : layer)
		reindex(l);
	auto sweep = [&](size_t l, std::vector<std::vector<size_t>> const& neighbors) {
		for (auto v : layer[l]) {
			if (neighbors[v].empty()) {
				key[v] = index[v];
				continue;
			}
			float sum = 0;
			for (auto w : neighbors[v])
				sum += index[w];
			key[v] = sum / neighbors[v].size();
		}
		std::stable_sort(layer[l].begin(), layer[l].end(), [&](size_t a, size_t b) { return key[a] < key[b]; });
		reindex(layer[l]);
	};
	for (int pass = 0; pass < 4; pass++) {
		for (size_t l = 1; l < layers; l++)
			sweep(l, pred);
		for (size_t l = layers - 1; l-- > 0;)
			sweep(l, succ);
	}

	// vertical: the layers one below another. Only blocks take room: a layer of bends has no height.
	std::vector<float> top(layers), layerHeight(layers, 0.0f);
	for (size_t l = 0; l < layers; l++)
		for (auto v : layer[l])
			layerHeight[l] = std::max(layerHeight[l], height[v]);
	float y = 0;
	for (size_t l = 0; l < layers; l++) {
		top[l] = y;
		y += layerHeight[l] + options.VerticalGap;
	}

	// horizontal: side by side in each layer to start with, then pulled towards the nodes that they are connected to.
	// Bends can be closer to what is next to them than blocks are.
	auto distance = [&](size_t a, size_t b) {
		bool bend = a >= n || b >= n;
		return (width[a] + width[b]) * 0.5f + options.HorizontalGap * (bend ? 0.35f : 1.0f);
	};
	std::vector<float> x(total, 0.0f);
	for (auto const& l : layer) {
		float span = 0;
		for (size_t i = 1; i < l.size(); i++)
			span += distance(l[i - 1], l[i]);
		float at = -span * 0.5f;
		for (size_t i = 0; i < l.size(); i++) {
			if (i)
				at += distance(l[i - 1], l[i]);
			x[l[i]] = at;
		}
	}
	auto pull = [&](size_t l, std::vector<std::vector<size_t>> const& neighbors) {
		for (auto v : layer[l]) {
			if (neighbors[v].empty()) {
				key[v] = x[v];
				continue;
			}
			float sum = 0;
			for (auto w : neighbors[v])
				sum += x[w];
			key[v] = sum / neighbors[v].size();
		}
		Place(layer[l], key, distance, x);
	};
	for (int pass = 0; pass < 6; pass++) {
		for (size_t l = 1; l < layers; l++)
			pull(l, pred);
		for (size_t l = layers - 1; l-- > 0;)
			pull(l, succ);
	}
	for (size_t l = 1; l < layers; l++)
		pull(l, pred);

	float left = x[0] - width[0] * 0.5f;
	for (size_t i = 1; i < total; i++)
		left = std::min(left, x[i] - width[i] * 0.5f);
	auto center = [&](size_t v) {
		return FlowPoint{ x[v] - left, top[layerOf[v]] + layerHeight[layerOf[v]] * 0.5f };
	};
	for (size_t i = 0; i < n; i++)
		result.Blocks[i] = center(i);
	for (size_t i = 0; i < graph.Edges.size(); i++) {
		for (auto bend : chains[i])
			result.Edges[i].push_back(center(bend));
		if (reversed[i])
			std::reverse(result.Edges[i].begin(), result.Edges[i].end());
	}
	return result;
}
