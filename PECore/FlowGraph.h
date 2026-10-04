#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class PEFile;
class XrefMap;

// One decoded instruction of a function.
struct FlowInstruction {
	uint64_t Address{ 0 };
	uint8_t Size{ 0 };
	std::string Mnemonic;
	std::string Operands;
	std::optional<uint64_t> Branch;		// the target of a direct call or jump
	std::optional<uint64_t> Memory;		// the address of a memory operand with a fixed address ([rip+x], [0x403000])
};

// How the code of a block goes on.
enum class FlowEnd : uint8_t {
	Fallthrough,		// into the block that follows: something else jumps there
	Jump,				// jmp to another block
	ConditionalJump,	// jcc: to the target (true) or to the next instruction (false)
	Switch,				// jmp through a jump table
	Return,
	TailCall,			// jmp to another function
	IndirectJump,		// jmp reg / jmp [mem] that is not a jump table: where it goes is not known
	Trap,				// int3, ud2, hlt: the code does not go on
	Invalid,			// what follows is not code (or is outside of the image)
};

enum class FlowEdgeKind : uint8_t {
	Unconditional,		// jmp, or the code going on into a block that is a jump target
	True,				// the jump of a conditional jump is taken
	False,				// it is not: the code goes on with the next instruction
	Case,				// an entry of a jump table
};

struct FlowBlock {
	uint64_t Start{ 0 };
	uint64_t End{ 0 };		// the address after the last instruction
	std::vector<FlowInstruction> Code;
	FlowEnd Ending{ FlowEnd::Invalid };
};

struct FlowEdge {
	size_t From{ 0 };	// indices of blocks
	size_t To{ 0 };
	FlowEdgeKind Kind{ FlowEdgeKind::Unconditional };
};

// The blocks of code of a function: runs of instructions that are entered at the first and left at the last, and the ways
// that lead from one to another. Found by following the code from the entry, not by looking at where the function ends.
struct FlowGraph {
	uint64_t Entry{ 0 };
	size_t EntryBlock{ 0 };
	std::vector<FlowBlock> Blocks;		// by address
	std::vector<FlowEdge> Edges;
	bool Truncated{ false };			// there is more code than the limit allows
};

const wchar_t* FlowEdgeKindToString(FlowEdgeKind kind);

// Follows the code from 'entry': conditional jumps lead to both of their targets, jump tables to all of their entries, calls go
// on with the next instruction, and a jump to the start of a function that is called from elsewhere is taken for a tail call.
// The graph has no blocks if there is no code at 'entry'.
FlowGraph BuildFlowGraph(PEFile const& pe, XrefMap const& xrefs, uint64_t entry, size_t maxInstructions = 100000);

// The start of the function that contains 'va': from the exception directory in 64-bit code, otherwise the closest address
// before 'va' that is called. 'va' itself if nothing is known.
uint64_t FindFunctionStart(PEFile const& pe, XrefMap const& xrefs, uint64_t va);

struct FlowSize {
	float Width{ 0 };
	float Height{ 0 };
};

struct FlowPoint {
	float X{ 0 };
	float Y{ 0 };
};

struct FlowLayoutOptions {
	float HorizontalGap{ 40 };
	float VerticalGap{ 60 };
};

struct FlowLayout {
	std::vector<FlowPoint> Blocks;					// the center of each block
	std::vector<std::vector<FlowPoint>> Edges;		// by edge: the bends between the two blocks, in the direction of the edge (none: a straight line)
};

// Places the blocks in layers from the entry downwards: a block is below the blocks that lead to it, except for the edges that
// close a loop. Blocks in a layer are ordered to keep the edges short and are placed close to the ones they are connected to.
// An edge that skips layers is given bends in the layers that it crosses, so that it goes around the blocks in between.
// 'sizes' has one entry per block. Coordinates are not negative.
FlowLayout LayoutFlowGraph(FlowGraph const& graph, std::span<const FlowSize> sizes, FlowLayoutOptions options = {});
