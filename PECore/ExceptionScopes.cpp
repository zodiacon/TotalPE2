#include "pch.h"
#include "ExceptionScopes.h"
#include "PEFile.h"
#include "UnwindInfo.h"
#include "CodeAnalysis.h"
#include <capstone/capstone.h>
#include <algorithm>
#include <format>
#include <map>

ExceptionHandlerKind ExceptionHandlerKindOf(std::string_view name) {
	// (the names of the GS checks contain those of the handlers they call: the longer ones first)
	if (name.find("GSHandlerCheck_EH4") != std::string_view::npos || name.find("CxxFrameHandler4") != std::string_view::npos)
		return ExceptionHandlerKind::Cxx4;
	if (name.find("GSHandlerCheck_SEH") != std::string_view::npos || name.find("C_specific_handler") != std::string_view::npos)
		return ExceptionHandlerKind::Seh;
	if (name.find("GSHandlerCheck_EH") != std::string_view::npos || name.find("CxxFrameHandler") != std::string_view::npos)
		return ExceptionHandlerKind::Cxx3;
	return ExceptionHandlerKind::None;
}

bool ReadCompressedUnsigned(std::span<const uint8_t> data, size_t& used, uint32_t& value) {
	if (data.empty())
		return false;
	// the low bits of the first byte: its length (0: 1 byte, 01: 2, 011: 3, 0111: 4, 1111: 5, the 4 after it the value)
	auto first = data[0];
	size_t length = (first & 1) == 0 ? 1 : (first & 3) == 1 ? 2 : (first & 7) == 3 ? 3 : (first & 15) == 7 ? 4 : 5;
	if (data.size() < length)
		return false;
	if (length == 5)
		value = data[1] | (data[2] << 8) | (data[3] << 16) | ((uint32_t)data[4] << 24);
	else {
		uint32_t raw = 0;
		for (size_t i = 0; i < length; i++)
			raw |= (uint32_t)data[i] << (8 * i);
		value = raw >> length;
	}
	used += length;
	return true;
}

namespace {
	constexpr uint32_t MaxEntries = 4096;	// (more is damaged data)

	struct Reader {
		ImageReader const& Read;
		uint32_t Rva;
		bool Ok{ true };

		template<typename T>
		T Get() {
			T value{};
			if (Ok && !Read(Rva, &value, sizeof(value)))
				Ok = false;
			Rva += sizeof(T);
			return value;
		}
		// a value of the compressed encoding of __CxxFrameHandler4
		uint32_t Compressed() {
			uint8_t bytes[5]{};
			uint32_t value = 0;
			size_t used = 0;
			if (!Ok || !Read(Rva, bytes, 1)) {
				Ok = false;
				return 0;
			}
			auto first = bytes[0];
			size_t length = (first & 1) == 0 ? 1 : (first & 3) == 1 ? 2 : (first & 7) == 3 ? 3 : (first & 15) == 7 ? 4 : 5;
			if (length > 1 && !Read(Rva + 1, bytes + 1, length - 1)) {
				Ok = false;
				return 0;
			}
			ReadCompressedUnsigned(std::span<const uint8_t>(bytes, length), used, value);
			Rva += (uint32_t)used;
			return value;
		}
	};

	// The code of each state: from the IP-to-state map, [ip, the next ip) (the last to the end of the function)
	using StateRanges = std::vector<std::tuple<uint32_t, uint32_t, int32_t>>;	// begin, end, state

	StateRanges RangesOf(std::vector<std::pair<uint32_t, int32_t>> map, uint32_t functionEnd) {
		StateRanges ranges;
		std::ranges::sort(map);
		for (size_t i = 0; i < map.size(); i++) {
			auto begin = map[i].first;
			auto end = i + 1 < map.size() ? map[i + 1].first : std::max(begin, functionEnd);
			if (end > begin)
				ranges.emplace_back(begin, end, map[i].second);
		}
		return ranges;
	}

	// the code of a try: of the states from 'low' to 'high' (adjacent pieces joined)
	std::vector<std::pair<uint32_t, uint32_t>> TryRanges(StateRanges const& states, int32_t low, int32_t high) {
		std::vector<std::pair<uint32_t, uint32_t>> ranges;
		for (auto const& [begin, end, state] : states) {
			if (state < low || state > high)
				continue;
			if (!ranges.empty() && ranges.back().second == begin)
				ranges.back().second = end;
			else
				ranges.emplace_back(begin, end);
		}
		return ranges;
	}

	std::string TypeName(ImageReader const& read, uint32_t typeRva, uint32_t pointerSize = 8) {
		// TypeDescriptor: the pointer to type_info's vtable and a spare pointer, then the decorated name
		std::string name;
		char ch;
		for (uint32_t rva = typeRva + 2 * pointerSize; name.size() < 1024 && read(rva, &ch, 1) && ch; rva++)
			name += ch;
		return name;
	}

	void ReadSeh(ImageReader const& read, uint32_t dataRva, ExceptionHandling& eh) {
		Reader r{ read, dataRva };
		auto count = r.Get<uint32_t>();
		if (!r.Ok || count > MaxEntries) {
			eh.Error = L"The scope table cannot be read";
			return;
		}
		// one region of the source may be in several entries (pieces of code): those with the same handler are one
		for (uint32_t i = 0; i < count; i++) {
			auto begin = r.Get<uint32_t>(), end = r.Get<uint32_t>(), handler = r.Get<uint32_t>(), target = r.Get<uint32_t>();
			if (!r.Ok) {
				eh.Error = std::format(L"The scope table ends at its entry {}", i);
				return;
			}
			if (end <= begin)
				continue;
			ExceptionScope scope;
			if (target == 0) {
				scope.Kind = ExceptionScope::Kinds::Finally;
				scope.HandlerRva = handler;
			}
			else {
				scope.Kind = ExceptionScope::Kinds::Except;
				scope.FilterRva = handler == 1 ? 0 : handler;	// 1: EXCEPTION_EXECUTE_HANDLER
				scope.HandlerRva = target;
			}
			auto same = std::ranges::find_if(eh.Scopes, [&](auto const& s) {
				return s.Kind == scope.Kind && s.FilterRva == scope.FilterRva && s.HandlerRva == scope.HandlerRva;
				});
			if (same != eh.Scopes.end())
				same->Ranges.emplace_back(begin, end);
			else {
				scope.Ranges.emplace_back(begin, end);
				eh.Scopes.push_back(std::move(scope));
			}
		}
	}

	void ReadCxx3(ImageReader const& read, uint32_t funcInfoRva, uint32_t functionEnd, ExceptionHandling& eh) {
		Reader r{ read, funcInfoRva };
		auto magic = r.Get<uint32_t>();
		r.Get<int32_t>();		// maxState
		r.Get<int32_t>();		// dispUnwindMap
		auto tryCount = r.Get<uint32_t>();
		auto tryMap = r.Get<uint32_t>();
		auto ipCount = r.Get<uint32_t>();
		auto ipMap = r.Get<uint32_t>();
		if (!r.Ok || magic < 0x19930520 || magic > 0x19930522 || tryCount > MaxEntries || ipCount > MaxEntries * 16) {
			eh.Error = L"The FuncInfo of the C++ exceptions cannot be read";
			return;
		}
		std::vector<std::pair<uint32_t, int32_t>> map;
		Reader ip{ read, ipMap };
		for (uint32_t i = 0; i < ipCount && ip.Ok; i++) {
			auto at = ip.Get<uint32_t>();
			auto state = ip.Get<int32_t>();
			map.emplace_back(at, state);
		}
		auto states = RangesOf(std::move(map), functionEnd);
		Reader t{ read, tryMap };
		for (uint32_t i = 0; i < tryCount; i++) {
			auto low = t.Get<int32_t>(), high = t.Get<int32_t>();
			t.Get<int32_t>();	// catchHigh
			auto catches = t.Get<uint32_t>();
			auto handlers = t.Get<uint32_t>();
			if (!t.Ok || catches > MaxEntries) {
				eh.Error = std::format(L"The try block map ends at its entry {}", i);
				return;
			}
			ExceptionScope scope;
			scope.Kind = ExceptionScope::Kinds::Try;
			scope.Ranges = TryRanges(states, low, high);
			Reader h{ read, handlers };
			for (uint32_t c = 0; c < catches; c++) {
				ExceptionScope::Catch katch;
				katch.Adjectives = h.Get<uint32_t>();
				auto type = h.Get<uint32_t>();
				h.Get<int32_t>();	// dispCatchObj
				katch.HandlerRva = h.Get<uint32_t>();
				h.Get<int32_t>();	// dispFrame
				if (!h.Ok)
					break;
				if (type)
					katch.Type = TypeName(read, type);
				scope.Catches.push_back(std::move(katch));
			}
			eh.Scopes.push_back(std::move(scope));
		}
	}

	void ReadCxx4(ImageReader const& read, uint32_t funcInfoRva, uint32_t functionBegin, uint32_t functionEnd, ExceptionHandling& eh) {
		Reader r{ read, funcInfoRva };
		auto header = r.Get<uint8_t>();
		bool isCatch = header & 0x01, separated = header & 0x02, bbt = header & 0x04, unwindMap = header & 0x08, tryBlocks = header & 0x10;
		if (bbt)
			r.Compressed();
		if (unwindMap)
			r.Get<uint32_t>();
		uint32_t tryMap = tryBlocks ? r.Get<uint32_t>() : 0;
		auto ipMap = r.Get<uint32_t>();
		if (!r.Ok) {
			eh.Error = L"The FuncInfo4 of the C++ exceptions cannot be read";
			return;
		}
		if (isCatch || !tryMap)
			return;		// a catch funclet's own, or no try blocks (only destructors to call)
		// a function in pieces: the map of the piece that begins the function
		if (separated) {
			Reader s{ read, ipMap };
			auto count = s.Compressed();
			uint32_t found = 0;
			for (uint32_t i = 0; i < count && i < MaxEntries && s.Ok; i++) {
				auto start = s.Get<uint32_t>(), map = s.Get<uint32_t>();
				if (start == functionBegin)
					found = map;
			}
			if (!found) {
				eh.Error = L"The IP-to-state map of the function's code is not there";
				return;
			}
			ipMap = found;
		}
		std::vector<std::pair<uint32_t, int32_t>> map;
		Reader ip{ read, ipMap };
		auto ipCount = ip.Compressed();
		uint32_t at = functionBegin;
		for (uint32_t i = 0; i < ipCount && i < MaxEntries * 16 && ip.Ok; i++) {
			at += ip.Compressed();
			auto state = (int32_t)ip.Compressed() - 1;
			if (ip.Ok)
				map.emplace_back(at, state);
		}
		auto states = RangesOf(std::move(map), functionEnd);
		Reader t{ read, tryMap };
		auto tryCount = t.Compressed();
		for (uint32_t i = 0; i < tryCount && i < MaxEntries; i++) {
			auto low = (int32_t)t.Compressed(), high = (int32_t)t.Compressed();
			t.Compressed();	// catchHigh
			auto handlers = t.Get<uint32_t>();
			if (!t.Ok) {
				eh.Error = std::format(L"The try block map ends at its entry {}", i);
				return;
			}
			ExceptionScope scope;
			scope.Kind = ExceptionScope::Kinds::Try;
			scope.Ranges = TryRanges(states, low, high);
			Reader h{ read, handlers };
			auto catches = h.Compressed();
			for (uint32_t c = 0; c < catches && c < MaxEntries && h.Ok; c++) {
				ExceptionScope::Catch katch;
				auto flags = h.Get<uint8_t>();
				if (flags & 0x01)
					katch.Adjectives = h.Compressed();
				uint32_t type = (flags & 0x02) ? h.Get<uint32_t>() : 0;
				if (flags & 0x04)
					h.Compressed();		// dispCatchObj
				katch.HandlerRva = h.Get<uint32_t>();
				// the addresses execution continues at
				uint32_t continuations = (flags >> 4) & 3;
				for (uint32_t k = 0; k < continuations; k++)
					if (flags & 0x08)
						h.Get<uint32_t>();
					else
						h.Compressed();
				if (!h.Ok)
					break;
				if (type)
					katch.Type = TypeName(read, type);
				scope.Catches.push_back(std::move(katch));
			}
			eh.Scopes.push_back(std::move(scope));
		}
	}
}

ExceptionHandling ReadExceptionHandling(ImageReader const& read, ExceptionHandlerKind kind, uint32_t handlerRva,
	uint32_t dataRva, uint32_t functionBegin, uint32_t functionEnd) {
	ExceptionHandling eh;
	eh.Kind = kind;
	eh.HandlerRva = handlerRva;
	switch (kind) {
		case ExceptionHandlerKind::Seh:
			ReadSeh(read, dataRva, eh);
			break;
		case ExceptionHandlerKind::Cxx3:
		case ExceptionHandlerKind::Cxx4:
		{
			// the data: the RVA of the FuncInfo (then, for the GS checks, theirs)
			uint32_t funcInfo = 0;
			if (!read(dataRva, &funcInfo, sizeof(funcInfo)) || !funcInfo) {
				eh.Error = L"The handler's data cannot be read";
				break;
			}
			if (kind == ExceptionHandlerKind::Cxx3)
				ReadCxx3(read, funcInfo, functionEnd, eh);
			else
				ReadCxx4(read, funcInfo, functionBegin, functionEnd, eh);
			break;
		}
		default:
			break;
	}
	// inner ones first: a region inside another is shown in it
	std::ranges::stable_sort(eh.Scopes, [](auto const& a, auto const& b) {
		auto size = [](ExceptionScope const& s) {
			uint64_t total = 0;
			for (auto const& [begin, end] : s.Ranges)
				total += end - begin;
			return total;
		};
		return size(a) < size(b);
		});
	return eh;
}

// ARM64's .xdata: a word (the function's length, then X: the handler is there, E: one epilog in the word, the count of the
// epilogs and of the words of unwind codes; both 0: in a second word), the epilogs' scopes (unless E), the unwind codes,
// then the handler's RVA and its data
bool ReadArm64Handler(ImageReader const& read, uint32_t xdata, uint32_t& handlerRva, uint32_t& handlerDataRva) {
	uint32_t header;
	if (!xdata || !read(xdata, &header, 4) || !(header & (1u << 20)))
		return false;
	uint32_t epilogs = (header >> 22) & 0x1F, words = (header >> 27) & 0x1F, offset = 4;
	if (epilogs == 0 && words == 0) {
		uint32_t extended;
		if (!read(xdata + 4, &extended, 4))
			return false;
		epilogs = extended & 0xFFFF;
		words = (extended >> 16) & 0xFF;
		offset = 8;
	}
	if (!(header & (1u << 21)))
		offset += epilogs * 4;
	offset += words * 4;
	handlerDataRva = xdata + offset + 4;
	return read(xdata + offset, &handlerRva, 4) && handlerRva != 0;
}

ExceptionHandling ReadX86ExceptionHandling(ImageReader const& read, uint32_t imageBase, uint32_t functionRva,
	uint32_t functionLength, std::function<std::string(uint32_t rva)> const& handlerName) {
	ExceptionHandling eh;
	size_t handle = 0;
	if (!OpenDisassembler(CpuArch::X86, handle))
		return eh;
	auto cs = (csh)handle;
	auto rvaOf = [&](uint32_t va) { return va - imageBase; };
	// the code (to its end, or to the padding after a return)
	constexpr uint32_t MaxLength = 0x2000;
	std::vector<uint8_t> code(functionLength ? std::min(functionLength, MaxLength * 8) : MaxLength);
	// (in pieces: as much as the image has)
	size_t size = 0;
	while (size < code.size()) {
		size_t piece = std::min<size_t>(256, code.size() - size);
		while (piece > 0 && !read(functionRva + (uint32_t)size, code.data() + size, piece))
			piece = piece > 16 ? 16 : piece - 1;
		if (!piece)
			break;
		size += piece;
	}
	if (!size) {
		cs_close(&cs);
		return eh;
	}
	cs_insn* insns = nullptr;
	auto total = cs_disasm(cs, code.data(), size, imageBase + functionRva, 0, &insns);
	auto count = total;
	if (!functionLength)
		for (size_t i = 0; i + 1 < count; i++)
			if (insns[i].id == X86_INS_RET && insns[i + 1].id == X86_INS_INT3) {
				count = i + 1;
				break;
			}
	auto name = [&](uint32_t va) { return handlerName ? handlerName(rvaOf(va)) : std::string(); };

	// the prologue: push ebp; mov ebp, esp; then the pushes of the registration
	std::vector<uint32_t> pushes;
	for (size_t i = 0; i < count && i < 8 && pushes.size() < 3; i++) {
		auto const& x = insns[i].detail->x86;
		if (insns[i].id == X86_INS_PUSH && x.op_count == 1 && x.operands[0].type == X86_OP_IMM)
			pushes.push_back((uint32_t)x.operands[0].imm);
	}
	enum class Kind { None, Seh3, Seh4, Cxx } kind = Kind::None;
	uint32_t funcInfo = 0;
	if (pushes.size() == 3) {
		auto handler = name(pushes[2]);
		if (handler.find("except_handler4") != std::string::npos)
			kind = Kind::Seh4;
		else if (handler.find("except_handler3") != std::string::npos)
			kind = Kind::Seh3;
		if (kind != Kind::None)
			eh.HandlerRva = rvaOf(pushes[2]);
	}
	if (kind == Kind::None && pushes.size() >= 2 && pushes[0] == 0xFFFFFFFF) {
		// the stub: mov eax, offset FuncInfo; jmp __CxxFrameHandler3 (after the check of its cookie)
		uint8_t stub[64];
		if (read(rvaOf(pushes[1]), stub, sizeof(stub))) {
			cs_insn* s = nullptr;
			auto n = cs_disasm(cs, stub, sizeof(stub), pushes[1], 12, &s);
			for (size_t i = 0; i < n && !funcInfo; i++) {
				auto const& x = s[i].detail->x86;
				if (s[i].id == X86_INS_MOV && x.op_count == 2 && x.operands[0].type == X86_OP_REG && x.operands[0].reg == X86_REG_EAX &&
					x.operands[1].type == X86_OP_IMM) {
					uint32_t magic = 0;
					if (read(rvaOf((uint32_t)x.operands[1].imm), &magic, 4) && magic >= 0x19930520 && magic <= 0x19930522)
						funcInfo = (uint32_t)x.operands[1].imm;
				}
			}
			if (n)
				cs_free(s, n);
		}
		if (funcInfo) {
			kind = Kind::Cxx;
			eh.HandlerRva = rvaOf(pushes[1]);
		}
	}
	if (kind == Kind::None) {
		if (total)
			cs_free(insns, total);
		cs_close(&cs);
		return eh;
	}

	// the states of the code: where it sets [ebp-4] (mov dword or byte; and with 0), from the state the prologue pushed
	int32_t state = kind == Kind::Cxx || kind == Kind::Seh3 ? -1 : (int32_t)pushes[0];
	std::vector<std::pair<uint32_t, int32_t>> map{ { functionRva, state } };
	int32_t maxState = -1;
	uint32_t end = functionRva;
	for (size_t i = 0; i < count; i++) {
		auto const& insn = insns[i];
		auto const& x = insn.detail->x86;
		end = (uint32_t)(insn.address + insn.size - imageBase);
		if (x.op_count != 2 || x.operands[0].type != X86_OP_MEM || x.operands[0].mem.base != X86_REG_EBP ||
			x.operands[0].mem.disp != -4 || x.operands[0].mem.index != X86_REG_INVALID || x.operands[1].type != X86_OP_IMM)
			continue;
		int32_t value;
		if (insn.id == X86_INS_MOV)
			value = x.operands[0].size == 1 ? (int32_t)(uint8_t)x.operands[1].imm : (int32_t)x.operands[1].imm;
		else if (insn.id == X86_INS_AND && x.operands[1].imm == 0)
			value = 0;
		else
			continue;
		maxState = std::max(maxState, value);
		map.emplace_back(end, value);
	}
	if (total)
		cs_free(insns, total);
	cs_close(&cs);
	auto states = RangesOf(std::move(map), end);
	// (the code of the states some of which are given: adjacent pieces joined)
	auto rangesOf = [&](auto const& wanted) {
		std::vector<std::pair<uint32_t, uint32_t>> ranges;
		for (auto const& [begin, stop, s] : states) {
			if (!wanted(s))
				continue;
			if (!ranges.empty() && ranges.back().second == begin)
				ranges.back().second = stop;
			else
				ranges.emplace_back(begin, stop);
		}
		return ranges;
	};

	if (kind == Kind::Cxx) {
		// FuncInfo (of 32-bit VAs): magic, maxState, the unwind map, the try blocks; their catches: adjectives, the type, the
		// place of the object, the handler (16 bytes)
		eh.Kind = ExceptionHandlerKind::Cxx3;
		Reader r{ read, rvaOf(funcInfo) };
		r.Get<uint32_t>();
		r.Get<int32_t>();
		r.Get<uint32_t>();
		auto tryCount = r.Get<uint32_t>();
		auto tryMap = r.Get<uint32_t>();
		if (!r.Ok || tryCount > MaxEntries) {
			eh.Error = L"The FuncInfo of the C++ exceptions cannot be read";
			return eh;
		}
		Reader t{ read, rvaOf(tryMap) };
		for (uint32_t i = 0; i < tryCount; i++) {
			auto low = t.Get<int32_t>(), high = t.Get<int32_t>();
			t.Get<int32_t>();
			auto catches = t.Get<uint32_t>();
			auto handlers = t.Get<uint32_t>();
			if (!t.Ok || catches > MaxEntries) {
				eh.Error = std::format(L"The try block map ends at its entry {}", i);
				return eh;
			}
			ExceptionScope scope;
			scope.Kind = ExceptionScope::Kinds::Try;
			scope.Ranges = rangesOf([&](int32_t s) { return s >= low && s <= high; });
			Reader h{ read, rvaOf(handlers) };
			for (uint32_t c = 0; c < catches; c++) {
				ExceptionScope::Catch katch;
				katch.Adjectives = h.Get<uint32_t>();
				auto type = h.Get<uint32_t>();
				h.Get<int32_t>();
				katch.HandlerRva = rvaOf(h.Get<uint32_t>());
				if (!h.Ok)
					break;
				if (type)
					katch.Type = TypeName(read, rvaOf(type), 4);
				scope.Catches.push_back(std::move(katch));
			}
			eh.Scopes.push_back(std::move(scope));
		}
		return eh;
	}

	// the scope table (EH4: after the offsets of the cookies): for each try level, the one it is in, the filter (none: a
	// __finally) and the handler
	eh.Kind = ExceptionHandlerKind::Seh;
	if (maxState < 0 || maxState >= (int32_t)MaxEntries)
		return eh;
	struct Level {
		int32_t Enclosing;
		uint32_t Filter, Handler;
	};
	std::vector<Level> levels(maxState + 1);
	Reader r{ read, rvaOf(pushes[1]) + (kind == Kind::Seh4 ? 16u : 0u) };
	for (auto& level : levels) {
		level.Enclosing = r.Get<int32_t>();
		level.Filter = r.Get<uint32_t>();
		level.Handler = r.Get<uint32_t>();
	}
	if (!r.Ok) {
		eh.Error = L"The scope table cannot be read";
		return eh;
	}
	// a level is in those it is enclosed by; the inner ones first
	auto in = [&](int32_t s, int32_t level) {
		for (int hops = 0; s >= 0 && s < (int32_t)levels.size() && hops <= maxState; hops++, s = levels[s].Enclosing)
			if (s == level)
				return true;
		return false;
	};
	auto depth = [&](int32_t level) {
		int d = 0;
		for (auto s = levels[level].Enclosing; s >= 0 && s < (int32_t)levels.size() && d <= maxState; s = levels[s].Enclosing)
			d++;
		return d;
	};
	std::vector<int32_t> order(levels.size());
	for (int32_t i = 0; i < (int32_t)order.size(); i++)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int32_t a, int32_t b) { return depth(a) > depth(b); });
	for (auto level : order) {
		ExceptionScope scope;
		auto const& l = levels[level];
		if (l.Filter == 0) {
			scope.Kind = ExceptionScope::Kinds::Finally;
			scope.HandlerRva = rvaOf(l.Handler);
		}
		else {
			scope.Kind = ExceptionScope::Kinds::Except;
			scope.FilterRva = l.Filter == 1 ? 0 : rvaOf(l.Filter);
			scope.HandlerRva = rvaOf(l.Handler);
		}
		scope.Ranges = rangesOf([&](int32_t s) { return in(s, level); });
		if (!scope.Ranges.empty())
			eh.Scopes.push_back(std::move(scope));
	}
	return eh;
}

ExceptionHandling ReadExceptionHandling(PEFile const& pe, uint32_t functionRva, std::function<std::string(uint32_t rva)> const& handlerName,
	uint32_t functionLength) {
	ExceptionHandling eh;
	if (!pe.GetFileInfo()->IsPE64 && pe.GetNTHeader()->NTHdr32.FileHeader.Machine == IMAGE_FILE_MACHINE_I386) {
		auto fileSize = pe.GetFileSize();
		ImageReader read = [&](uint32_t rva, void* buffer, size_t size) {
			auto offset = pe.GetOffsetFromRVA(rva);
			if (rva == 0 || offset == 0 || offset + size > fileSize)
				return false;
			auto span = pe.GetSpan((uint32_t)offset, (uint32_t)size);
			if (span.size() != size)
				return false;
			memcpy(buffer, span.data(), size);
			return true;
		};
		return ReadX86ExceptionHandling(read, (uint32_t)pe.GetImageBase(), functionRva, functionLength, handlerName);
	}
	auto entries = pe.GetExceptions();
	if (!entries)
		return eh;
	auto entry = std::ranges::find_if(*entries, [&](auto const& e) { return e.RuntimeFuncEntry.BeginAddress == functionRva; });
	if (entry == entries->end())
		return eh;
	auto fileSize = pe.GetFileSize();
	ImageReader read = [&](uint32_t rva, void* buffer, size_t size) {
		auto offset = pe.GetOffsetFromRVA(rva);
		if (rva == 0 || offset == 0 || offset + size > fileSize)
			return false;
		auto span = pe.GetSpan((uint32_t)offset, (uint32_t)size);
		if (span.size() != size)
			return false;
		memcpy(buffer, span.data(), size);
		return true;
	};
	// the handler and its data: after the unwind codes of x64's UNWIND_INFO, or after those of ARM64's .xdata
	struct {
		uint32_t HandlerRva{}, HandlerDataRva{};
	} unwind;
	auto machine = pe.GetFileInfo()->IsPE64 ? pe.GetNTHeader()->NTHdr64.FileHeader.Machine : pe.GetNTHeader()->NTHdr32.FileHeader.Machine;
	if (machine == IMAGE_FILE_MACHINE_ARM64) {
		if (!ReadArm64Handler(read, entry->RuntimeFuncEntry.UnwindData, unwind.HandlerRva, unwind.HandlerDataRva))
			return eh;
	}
	else {
		auto info = DecodeUnwindInfo(pe, entry->RuntimeFuncEntry.UnwindData);
		if (!info.HasHandler())
			return eh;
		unwind.HandlerRva = info.HandlerRva;
		unwind.HandlerDataRva = info.HandlerDataRva;
	}
	auto kind = handlerName ? ExceptionHandlerKindOf(handlerName(unwind.HandlerRva)) : ExceptionHandlerKind::None;
	auto begin = entry->RuntimeFuncEntry.BeginAddress, end = entry->RuntimeFuncEntry.EndAddress;
	if (kind == ExceptionHandlerKind::None) {
		// Without a name: a FuncInfo of __CxxFrameHandler3 has a magic number; a scope table has a few entries, of the
		// function's code, with handlers
		uint32_t funcInfo = 0, magic = 0, count = 0;
		auto isScopeTable = [&] {
			if (!read(unwind.HandlerDataRva, &count, 4) || count == 0 || count > 64)
				return false;
			for (uint32_t i = 0; i < count; i++) {
				uint32_t scope[4];
				if (!read(unwind.HandlerDataRva + 4 + i * 16, scope, sizeof(scope)) || scope[0] >= scope[1] ||
					scope[0] < begin || scope[1] > end || scope[2] == 0)
					return false;
			}
			return true;
		};
		if (read(unwind.HandlerDataRva, &funcInfo, 4) && read(funcInfo, &magic, 4) && magic >= 0x19930520 && magic <= 0x19930522)
			kind = ExceptionHandlerKind::Cxx3;
		else if (isScopeTable())
			kind = ExceptionHandlerKind::Seh;
		else {
			eh.HandlerRva = unwind.HandlerRva;
			return eh;
		}
	}
	return ReadExceptionHandling(read, kind, unwind.HandlerRva, unwind.HandlerDataRva, begin, end);
}
