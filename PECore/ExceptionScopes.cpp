#include "pch.h"
#include "ExceptionScopes.h"
#include "PEFile.h"
#include "UnwindInfo.h"
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

	std::string TypeName(ImageReader const& read, uint32_t typeRva) {
		// TypeDescriptor: the pointer to type_info's vtable and a spare pointer, then the decorated name
		std::string name;
		char ch;
		for (uint32_t rva = typeRva + 16; name.size() < 1024 && read(rva, &ch, 1) && ch; rva++)
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

ExceptionHandling ReadExceptionHandling(PEFile const& pe, uint32_t functionRva, std::function<std::string(uint32_t rva)> const& handlerName) {
	ExceptionHandling eh;
	auto entries = pe.GetExceptions();
	if (!entries)
		return eh;
	auto entry = std::ranges::find_if(*entries, [&](auto const& e) { return e.RuntimeFuncEntry.BeginAddress == functionRva; });
	if (entry == entries->end())
		return eh;
	auto unwind = DecodeUnwindInfo(pe, entry->RuntimeFuncEntry.UnwindData);
	if (!unwind.HasHandler())
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
