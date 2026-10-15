#include "pch.h"
#include "LibrarySignatures.h"
#include "CoffObject.h"
#include "LibArchive.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <compressapi.h>

#pragma comment(lib, "cabinet.lib")

namespace {
	// How many bytes a relocation writes (0: none, IMAGE_REL_*_ABSOLUTE); ARM64's are of the instruction they are in
	uint8_t RelocationSize(uint16_t machine, uint16_t type) {
		switch (machine) {
			case IMAGE_FILE_MACHINE_AMD64:
				switch (type) {
					case IMAGE_REL_AMD64_ABSOLUTE: return 0;
					case IMAGE_REL_AMD64_ADDR64: return 8;
					case IMAGE_REL_AMD64_SECTION: return 2;
					case IMAGE_REL_AMD64_SECREL7: return 1;
					default: return 4;
				}
			case IMAGE_FILE_MACHINE_I386:
				switch (type) {
					case IMAGE_REL_I386_ABSOLUTE: return 0;
					case IMAGE_REL_I386_DIR16: case IMAGE_REL_I386_REL16: case IMAGE_REL_I386_SECTION: return 2;
					case IMAGE_REL_I386_SECREL7: return 1;
					default: return 4;
				}
			case IMAGE_FILE_MACHINE_ARM64:
				switch (type) {
					case IMAGE_REL_ARM64_ABSOLUTE: return 0;
					case IMAGE_REL_ARM64_ADDR64: return 8;
					case IMAGE_REL_ARM64_SECTION: return 2;
					default: return 4;
				}
		}
		return 4;
	}

	// a relocation of a call or a jump to the function it names: x86 and x64 a rel32 after E8 or E9, ARM64 a b or a bl
	bool IsCall(uint16_t machine, uint16_t type, std::span<const std::byte> code, uint32_t offset) {
		switch (machine) {
			case IMAGE_FILE_MACHINE_AMD64:
				return type == IMAGE_REL_AMD64_REL32 && offset > 0 && ((uint8_t)code[offset - 1] == 0xE8 || (uint8_t)code[offset - 1] == 0xE9);
			case IMAGE_FILE_MACHINE_I386:
				return type == IMAGE_REL_I386_REL32 && offset > 0 && ((uint8_t)code[offset - 1] == 0xE8 || (uint8_t)code[offset - 1] == 0xE9);
			case IMAGE_FILE_MACHINE_ARM64:
				return type == IMAGE_REL_ARM64_BRANCH26;
		}
		return false;
	}

	uint64_t Fnv(uint64_t hash, uint8_t byte) {
		return (hash ^ byte) * 0x100000001B3ULL;
	}

	// the key of the index: the machine, the number of bytes, the bytes
	uint64_t Key(uint16_t machine, std::span<const uint8_t> bytes) {
		uint64_t hash = 0xCBF29CE484222325ULL;
		hash = Fnv(Fnv(hash, (uint8_t)machine), (uint8_t)(machine >> 8));
		hash = Fnv(hash, (uint8_t)bytes.size());
		for (auto b : bytes)
			hash = Fnv(hash, b);
		return hash;
	}

	// the fixed bytes a signature starts with (at most 16)
	size_t Prefix(LibrarySignatures::Signature const& s) {
		size_t prefix = std::min<size_t>(16, s.Code.size());
		if (!s.Masked.empty())
			prefix = std::min<size_t>(prefix, s.Masked.front().first);
		return prefix;
	}

	bool Fixed(LibrarySignatures::Signature const& s) {
		size_t masked = 0;
		for (auto const& [offset, count] : s.Masked)
			masked += count;
		return s.Code.size() >= LibrarySignatures::MinLength && s.Code.size() - masked >= LibrarySignatures::MinFixed;
	}
}

size_t LibrarySignatures::AddObject(CoffObject const& object) {
	auto machine = object.Machine();
	if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386 && machine != IMAGE_FILE_MACHINE_ARM64)
		return 0;
	auto const& sections = object.Sections();
	auto const& symbols = object.Symbols();
	size_t added = 0;
	for (size_t s = 0; s < sections.size(); s++) {
		auto const& section = sections[s];
		if (!(section.Characteristics & IMAGE_SCN_CNT_CODE))
			continue;
		auto data = object.SectionData(s);
		if (data.empty())
			continue;
		// the functions of the section, in order
		std::vector<CoffSymbol const*> functions;
		for (auto const& sym : symbols)
			if (sym.SectionNumber == (int32_t)s + 1 && (sym.Type >> 4) == IMAGE_SYM_DTYPE_FUNCTION &&
				(sym.StorageClass == IMAGE_SYM_CLASS_EXTERNAL || sym.StorageClass == IMAGE_SYM_CLASS_STATIC) &&
				!sym.Name.empty() && sym.Name[0] != '$' && sym.Name[0] != '.' && sym.Value < data.size())
				functions.push_back(&sym);
		std::stable_sort(functions.begin(), functions.end(), [](auto a, auto b) { return a->Value < b->Value; });
		// the relocations of the section
		std::vector<CoffRelocation const*> relocations;
		for (auto const& r : object.Relocations())
			if (r.Section == s)
				relocations.push_back(&r);
		std::sort(relocations.begin(), relocations.end(), [](auto a, auto b) { return a->Offset < b->Offset; });
		for (size_t f = 0; f < functions.size(); f++) {
			uint32_t begin = functions[f]->Value;
			uint32_t end = f + 1 < functions.size() ? functions[f + 1]->Value : (uint32_t)data.size();
			if (end <= begin)
				continue;
			Signature signature;
			signature.Machine = machine;
			signature.Name = functions[f]->Name;
			signature.Code.assign((uint8_t const*)data.data() + begin, (uint8_t const*)data.data() + end);
			for (auto r : relocations) {
				if (r->Offset < begin || r->Offset >= end)
					continue;
				auto size = RelocationSize(machine, r->Type);
				if (size == 0)
					continue;
				auto offset = r->Offset - begin;
				if (machine == IMAGE_FILE_MACHINE_ARM64 && size == 4)
					offset &= ~3u;
				size = (uint8_t)std::min<uint32_t>(size, end - begin - offset);
				signature.Masked.emplace_back(offset, size);
				if (IsCall(machine, r->Type, data, r->Offset))
					if (auto target = object.SymbolByIndex(r->SymbolIndex); target && !target->Name.empty() && target->Name[0] != '$')
						signature.Calls.emplace_back(offset, target->Name);
			}
			if (!Fixed(signature))
				continue;
			m_Signatures.push_back(std::move(signature));
			added++;
		}
	}
	m_Indexed = false;
	return added;
}

size_t LibrarySignatures::AddLibrary(LibArchive const& library) {
	size_t added = 0;
	for (size_t i = 0; i < library.Members().size(); i++) {
		if (library.Members()[i].Kind != ArchiveMemberKind::Object)
			continue;
		CoffObject object;
		if (object.Parse(library.MemberData(i)))
			added += AddObject(object);
	}
	return added;
}

void LibrarySignatures::Finish() {
	// by code (the bytes the linker writes as 0): one of each name; of several names, ambiguous
	std::map<std::string, size_t> byCode;
	std::vector<Signature> kept;
	for (auto& s : m_Signatures) {
		std::string key((char const*)&s.Machine, 2);
		auto code = s.Code;
		for (auto const& [offset, count] : s.Masked)
			std::fill_n(code.begin() + offset, count, 0);
		key.append((char const*)code.data(), code.size());
		for (auto const& [offset, count] : s.Masked)
			key += std::to_string(offset) + ":" + std::to_string(count) + ";";
		auto [it, added] = byCode.try_emplace(key, kept.size());
		if (added)
			kept.push_back(std::move(s));
		else if (kept[it->second].Name != s.Name)
			kept[it->second].Ambiguous = true;
	}
	m_Signatures = std::move(kept);
	m_Indexed = false;
}

// A file: "TPESIG1\0", the size of the data, then the data compressed (LZMS): the count; each: the machine, ambiguous, the
// name, the code, the masked bytes, the calls
bool LibrarySignatures::Save(std::wstring const& path) const {
	std::ostringstream out;
	auto put = [&](auto value) { out.write((char const*)&value, sizeof(value)); };
	auto putString = [&](std::string const& text) {
		put((uint16_t)text.size());
		out.write(text.data(), text.size());
	};
	put((uint32_t)m_Signatures.size());
	for (auto const& s : m_Signatures) {
		put(s.Machine);
		put((uint8_t)s.Ambiguous);
		putString(s.Name);
		put((uint32_t)s.Code.size());
		out.write((char const*)s.Code.data(), s.Code.size());
		put((uint32_t)s.Masked.size());
		for (auto const& [offset, count] : s.Masked) {
			put(offset);
			put(count);
		}
		put((uint32_t)s.Calls.size());
		for (auto const& [offset, name] : s.Calls) {
			put(offset);
			putString(name);
		}
	}
	auto data = out.str();
	COMPRESSOR_HANDLE compressor = nullptr;
	if (!::CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &compressor))
		return false;
	SIZE_T size = 0;
	::Compress(compressor, data.data(), data.size(), nullptr, 0, &size);
	std::string compressed(size, '\0');
	bool ok = ::Compress(compressor, data.data(), data.size(), compressed.data(), compressed.size(), &size);
	::CloseCompressor(compressor);
	if (!ok)
		return false;
	std::ofstream file(path, std::ios::binary);
	uint64_t raw = data.size();
	file.write("TPESIG1", 8);
	file.write((char const*)&raw, sizeof(raw));
	file.write(compressed.data(), size);
	return (bool)file;
}

bool LibrarySignatures::Load(std::wstring const& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	char magic[8]{};
	uint64_t raw = 0;
	file.read(magic, 8);
	file.read((char*)&raw, sizeof(raw));
	if (!file || memcmp(magic, "TPESIG1", 8) != 0 || raw > (1ull << 31))
		return false;
	std::string compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::string data(raw, '\0');
	DECOMPRESSOR_HANDLE decompressor = nullptr;
	if (!::CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &decompressor))
		return false;
	SIZE_T size = 0;
	bool decompressed = ::Decompress(decompressor, compressed.data(), compressed.size(), data.data(), data.size(), &size) && size == raw;
	::CloseDecompressor(decompressor);
	if (!decompressed)
		return false;
	std::istringstream in(data);
	bool ok = true;
	auto get = [&](auto& value) {
		ok = ok && (bool)in.read((char*)&value, sizeof(value));
	};
	auto getString = [&](std::string& text) {
		uint16_t size = 0;
		get(size);
		text.resize(size);
		ok = ok && (size == 0 || (bool)in.read(text.data(), size));
	};
	uint32_t count = 0;
	get(count);
	constexpr uint32_t MaxCount = 10'000'000, MaxSize = 1 << 20;
	if (!ok || count > MaxCount)
		return false;
	std::vector<Signature> loaded;
	loaded.reserve(count);
	for (uint32_t i = 0; i < count && ok; i++) {
		Signature s;
		uint8_t ambiguous = 0;
		uint32_t size = 0;
		get(s.Machine);
		get(ambiguous);
		s.Ambiguous = ambiguous != 0;
		getString(s.Name);
		get(size);
		if (!ok || size > MaxSize)
			return false;
		s.Code.resize(size);
		ok = ok && (size == 0 || (bool)in.read((char*)s.Code.data(), size));
		get(size);
		if (!ok || size > MaxSize)
			return false;
		s.Masked.resize(size);
		for (auto& [offset, n] : s.Masked) {
			get(offset);
			get(n);
			ok = ok && offset + n <= s.Code.size();
		}
		get(size);
		if (!ok || size > MaxSize)
			return false;
		s.Calls.resize(size);
		for (auto& [offset, name] : s.Calls) {
			get(offset);
			getString(name);
			ok = ok && offset + 4 <= s.Code.size();
		}
		if (ok)
			loaded.push_back(std::move(s));
	}
	if (!ok)
		return false;
	m_Signatures.insert(m_Signatures.end(), std::make_move_iterator(loaded.begin()), std::make_move_iterator(loaded.end()));
	m_Indexed = false;
	return true;
}

void LibrarySignatures::Index() const {
	if (m_Indexed)
		return;
	m_Index.clear();
	m_Prefixes.clear();
	std::map<uint16_t, std::set<uint32_t>> prefixes;
	for (uint32_t i = 0; i < (uint32_t)m_Signatures.size(); i++) {
		auto const& s = m_Signatures[i];
		auto prefix = Prefix(s);
		m_Index[Key(s.Machine, std::span(s.Code.data(), prefix))].push_back(i);
		prefixes[s.Machine].insert((uint32_t)prefix);
	}
	for (auto const& [machine, set] : prefixes)
		m_Prefixes[machine].assign(set.begin(), set.end());
	m_Indexed = true;
}

LibrarySignatures::Signature const* LibrarySignatures::Find(uint16_t machine, std::span<const uint8_t> code) const {
	Index();
	auto prefixes = m_Prefixes.find(machine);
	if (prefixes == m_Prefixes.end())
		return nullptr;
	Signature const* best = nullptr;
	for (auto prefix : prefixes->second) {
		if (prefix > code.size())
			continue;
		auto it = m_Index.find(Key(machine, code.first(prefix)));
		if (it == m_Index.end())
			continue;
		for (auto i : it->second) {
			auto const& s = m_Signatures[i];
			if (s.Machine != machine || s.Code.size() > code.size() || Prefix(s) != prefix)
				continue;
			// the bytes between those the linker writes
			bool same = true;
			size_t at = 0;
			for (size_t m = 0; m <= s.Masked.size() && same; m++) {
				size_t end = m < s.Masked.size() ? s.Masked[m].first : s.Code.size();
				same = end < at || memcmp(s.Code.data() + at, code.data() + at, end - at) == 0;
				if (m < s.Masked.size())
					at = std::max(at, (size_t)s.Masked[m].first + s.Masked[m].second);
			}
			if (same && (!best || s.Code.size() > best->Code.size()))
				best = &s;
		}
	}
	return best && !best->Ambiguous ? best : nullptr;
}

std::vector<std::pair<uint64_t, std::string>> LibrarySignatures::CallsOf(Signature const& signature, std::span<const uint8_t> code, uint64_t address) {
	std::vector<std::pair<uint64_t, std::string>> calls;
	for (auto const& [offset, name] : signature.Calls) {
		if (offset + 4 > code.size())
			continue;
		int32_t value;
		memcpy(&value, code.data() + offset, 4);
		uint64_t target;
		if (signature.Machine == IMAGE_FILE_MACHINE_ARM64) {
			// b, bl: imm26, in instructions
			int32_t imm = (value & 0x03FFFFFF) << 6 >> 6;
			target = address + offset + (int64_t)imm * 4;
		}
		else
			target = address + offset + 4 + (int64_t)value;
		calls.emplace_back(target, name);
	}
	return calls;
}
