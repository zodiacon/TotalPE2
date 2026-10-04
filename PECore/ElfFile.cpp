#include "pch.h"
#include "ElfFile.h"
#include <fstream>
#include <unordered_map>

namespace {
	// e_ident
	constexpr size_t IdentSize = 16;
	constexpr uint8_t ClassElf32 = 1, ClassElf64 = 2, DataLittle = 1, DataBig = 2;

	// section types and flags
	constexpr uint32_t ShtSymtab = 2, ShtRela = 4, ShtDynamic = 6, ShtNote = 7, ShtNoBits = 8, ShtRel = 9, ShtDynsym = 11,
		ShtSymtabShndx = 18, ShtRelr = 19;
	constexpr uint64_t ShfAlloc = 2;
	// special section indices
	constexpr uint32_t ShnUndef = 0, ShnLoReserve = 0xFF00, ShnAbs = 0xFFF1, ShnCommon = 0xFFF2, ShnXIndex = 0xFFFF;
	// segment types
	constexpr uint32_t PtLoad = 1, PtDynamic = 2, PtInterp = 3, PtNote = 4;
	// dynamic tags with a string
	constexpr int64_t DtNull = 0, DtNeeded = 1, DtStrtab = 5, DtSoname = 14, DtRpath = 15, DtRunpath = 29;

	// a table cannot have more entries than this: a damaged count does not get to ask for gigabytes
	constexpr uint64_t MaxEntries = 0x1000000;

	std::wstring Hex(uint64_t value) {
		return std::format(L"0x{:X}", value);
	}

	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	std::wstring FlagNames(uint64_t flags, std::initializer_list<std::pair<uint64_t, PCWSTR>> names) {
		std::wstring text;
		for (auto& [flag, name] : names) {
			if (flags & flag) {
				text += (text.empty() ? L"" : L" ") + std::wstring(name);
				flags &= ~flag;
			}
		}
		if (flags)
			text += (text.empty() ? L"" : L" ") + Hex(flags);
		return text;
	}
}

bool ElfFile::IsElf(std::span<const std::byte> data) {
	if (data.size() < 52)
		return false;
	auto b = [&](size_t i) { return std::to_integer<uint8_t>(data[i]); };
	return b(0) == 0x7F && b(1) == 'E' && b(2) == 'L' && b(3) == 'F' && (b(4) == ClassElf32 || b(4) == ClassElf64) &&
		(b(5) == DataLittle || b(5) == DataBig) && b(6) == 1 && (b(4) == ClassElf32 || data.size() >= 64);
}

bool ElfFile::IsElfFile(std::wstring_view path) {
	std::ifstream in(std::wstring(path), std::ios::binary);
	if (!in)
		return false;
	std::vector<std::byte> header(64);
	in.read((char*)header.data(), header.size());
	header.resize((size_t)in.gcount());
	return IsElf(header);
}

bool ElfFile::Open(std::wstring_view path) {
	std::ifstream in(std::wstring(path), std::ios::binary);
	if (!in)
		return false;
	std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
	if (!Parse(std::as_bytes(std::span(bytes))))
		return false;
	m_Path = path;
	return true;
}

void ElfFile::Close() {
	*this = ElfFile();
}

uint16_t ElfFile::U16(uint64_t offset) const {
	if (!Fits(offset, 2))
		return 0;
	uint16_t v;
	memcpy(&v, m_Data.data() + offset, 2);
	return m_BigEndian ? _byteswap_ushort(v) : v;
}

uint32_t ElfFile::U32(uint64_t offset) const {
	if (!Fits(offset, 4))
		return 0;
	uint32_t v;
	memcpy(&v, m_Data.data() + offset, 4);
	return m_BigEndian ? _byteswap_ulong(v) : v;
}

uint64_t ElfFile::U64(uint64_t offset) const {
	if (!Fits(offset, 8))
		return 0;
	uint64_t v;
	memcpy(&v, m_Data.data() + offset, 8);
	return m_BigEndian ? _byteswap_uint64(v) : v;
}

std::string ElfFile::StringAt(uint32_t stringSection, uint64_t offset) const {
	if (stringSection >= m_Sections.size())
		return {};
	auto const& s = m_Sections[stringSection];
	if (s.Type == ShtNoBits || offset >= s.Size || !Fits(s.Offset, s.Size))
		return {};
	auto p = (const char*)m_Data.data() + s.Offset + offset;
	auto max = (size_t)(s.Size - offset);
	return std::string(p, strnlen(p, max));
}

bool ElfFile::Parse(std::span<const std::byte> data) {
	Close();
	if (!IsElf(data))
		return false;
	m_Copy.assign(data.begin(), data.end());
	m_Data = m_Copy;

	auto ident = [&](size_t i) { return std::to_integer<uint8_t>(m_Data[i]); };
	m_Is64 = ident(4) == ClassElf64;
	m_BigEndian = ident(5) == DataBig;
	m_OsAbi = ident(7);
	m_AbiVersion = ident(8);
	m_Type = U16(16);
	m_Machine = U16(18);
	m_Entry = Word(24);
	uint16_t phEntrySize, phCount, shEntrySize, shCount, shNames;
	if (m_Is64) {
		m_PhOffset = U64(32);
		m_ShOffset = U64(40);
		m_Flags = U32(48);
		phEntrySize = U16(54);
		phCount = U16(56);
		shEntrySize = U16(58);
		shCount = U16(60);
		shNames = U16(62);
	}
	else {
		m_PhOffset = U32(28);
		m_ShOffset = U32(32);
		m_Flags = U32(36);
		phEntrySize = U16(42);
		phCount = U16(44);
		shEntrySize = U16(46);
		shCount = U16(48);
		shNames = U16(50);
	}

	// extended numbering: the real numbers are in the first section header
	uint32_t sections = shCount, programHeaders = phCount, namesIndex = shNames;
	size_t minShEntry = m_Is64 ? 64 : 40;
	if (m_ShOffset && shEntrySize >= minShEntry && Fits(m_ShOffset, shEntrySize)) {
		if (shCount == 0)
			sections = (uint32_t)std::min<uint64_t>(m_Is64 ? U64(m_ShOffset + 32) : U32(m_ShOffset + 20), MaxEntries);
		if (shNames == ShnXIndex)
			namesIndex = U32(m_ShOffset + (m_Is64 ? 40 : 24));
		if (phCount == 0xFFFF)
			programHeaders = U32(m_ShOffset + (m_Is64 ? 44 : 28));
	}

	ReadProgramHeaders(phEntrySize, programHeaders);
	ReadSections(shEntrySize, m_ShOffset ? sections : 0, namesIndex);
	ReadSymbols();
	ReadDynamic();
	ReadRelocations();
	ReadNotes();
	return true;
}

void ElfFile::ReadProgramHeaders(uint16_t entrySize, uint32_t count) {
	if (count == 0 || m_PhOffset == 0)
		return;
	if (entrySize < (m_Is64 ? 56 : 32) || !Fits(m_PhOffset, (uint64_t)entrySize * count)) {
		m_Problems.push_back(std::format(L"The program headers ({} of {} bytes at 0x{:X}) are not in the file", count, entrySize, m_PhOffset));
		return;
	}
	for (uint32_t i = 0; i < count; i++) {
		auto at = m_PhOffset + (uint64_t)i * entrySize;
		ElfProgramHeader ph;
		ph.Type = U32(at);
		if (m_Is64) {
			ph.Flags = U32(at + 4);
			ph.Offset = U64(at + 8);
			ph.VirtualAddress = U64(at + 16);
			ph.PhysicalAddress = U64(at + 24);
			ph.FileSize = U64(at + 32);
			ph.MemorySize = U64(at + 40);
			ph.Align = U64(at + 48);
		}
		else {
			ph.Offset = U32(at + 4);
			ph.VirtualAddress = U32(at + 8);
			ph.PhysicalAddress = U32(at + 12);
			ph.FileSize = U32(at + 16);
			ph.MemorySize = U32(at + 20);
			ph.Flags = U32(at + 24);
			ph.Align = U32(at + 28);
		}
		if (ph.FileSize && !Fits(ph.Offset, ph.FileSize))
			m_Problems.push_back(std::format(L"Segment {} ({}) is not in the file", i, SegmentTypeName(ph.Type)));
		else if (ph.Type == PtInterp && ph.FileSize) {
			auto p = (const char*)m_Data.data() + ph.Offset;
			m_Interpreter.assign(p, strnlen(p, (size_t)ph.FileSize));
		}
		m_ProgramHeaders.push_back(ph);
	}
}

void ElfFile::ReadSections(uint16_t entrySize, uint32_t count, uint32_t namesIndex) {
	if (count == 0)
		return;
	if (entrySize < (m_Is64 ? 64 : 40) || !Fits(m_ShOffset, (uint64_t)entrySize * count)) {
		m_Problems.push_back(std::format(L"The section headers ({} of {} bytes at 0x{:X}) are not in the file", count, entrySize, m_ShOffset));
		return;
	}
	std::vector<uint32_t> nameOffsets;
	for (uint32_t i = 0; i < count; i++) {
		auto at = m_ShOffset + (uint64_t)i * entrySize;
		ElfSection s;
		nameOffsets.push_back(U32(at));
		s.Type = U32(at + 4);
		if (m_Is64) {
			s.Flags = U64(at + 8);
			s.Address = U64(at + 16);
			s.Offset = U64(at + 24);
			s.Size = U64(at + 32);
			s.Link = U32(at + 40);
			s.Info = U32(at + 44);
			s.AddressAlign = U64(at + 48);
			s.EntrySize = U64(at + 56);
		}
		else {
			s.Flags = U32(at + 8);
			s.Address = U32(at + 12);
			s.Offset = U32(at + 16);
			s.Size = U32(at + 20);
			s.Link = U32(at + 24);
			s.Info = U32(at + 28);
			s.AddressAlign = U32(at + 32);
			s.EntrySize = U32(at + 36);
		}
		if (i > 0 && s.Type != ShtNoBits && s.Size && !Fits(s.Offset, s.Size))
			m_Problems.push_back(std::format(L"The data of section {} is not in the file", i));
		m_Sections.push_back(std::move(s));
	}
	if (namesIndex != ShnUndef && namesIndex < m_Sections.size())
		for (size_t i = 0; i < m_Sections.size(); i++)
			m_Sections[i].Name = StringAt(namesIndex, nameOffsets[i]);
	else if (namesIndex != ShnUndef)
		m_Problems.push_back(std::format(L"The section of the section names ({}) does not exist", namesIndex));
}

void ElfFile::ReadSymbols() {
	size_t entrySize = m_Is64 ? 24 : 16;
	for (uint32_t t = 0; t < m_Sections.size(); t++) {
		auto const& table = m_Sections[t];
		if (table.Type != ShtSymtab && table.Type != ShtDynsym)
			continue;
		if (!Fits(table.Offset, table.Size)) {
			m_Problems.push_back(std::format(L"The symbol table {} is not in the file", Widen(table.Name)));
			continue;
		}
		// the section indices that do not fit in 16 bits are in a table of their own
		int extended = -1;
		for (uint32_t x = 0; x < m_Sections.size(); x++)
			if (m_Sections[x].Type == ShtSymtabShndx && m_Sections[x].Link == t)
				extended = (int)x;

		auto count = std::min<uint64_t>(table.Size / entrySize, MaxEntries);
		for (uint32_t i = 1; i < count; i++) {		// the first is the null symbol
			auto at = table.Offset + i * entrySize;
			ElfSymbol sym;
			sym.Index = i;
			sym.Dynamic = table.Type == ShtDynsym;
			uint32_t name;
			uint8_t info, other;
			if (m_Is64) {
				name = U32(at);
				info = std::to_integer<uint8_t>(m_Data[at + 4]);
				other = std::to_integer<uint8_t>(m_Data[at + 5]);
				sym.SectionIndex = U16(at + 6);
				sym.Value = U64(at + 8);
				sym.Size = U64(at + 16);
			}
			else {
				name = U32(at);
				sym.Value = U32(at + 4);
				sym.Size = U32(at + 8);
				info = std::to_integer<uint8_t>(m_Data[at + 12]);
				other = std::to_integer<uint8_t>(m_Data[at + 13]);
				sym.SectionIndex = U16(at + 14);
			}
			sym.Type = info & 0xF;
			sym.Bind = info >> 4;
			sym.Visibility = other & 3;
			if (sym.SectionIndex == ShnXIndex && extended >= 0)
				sym.SectionIndex = U32(m_Sections[extended].Offset + (uint64_t)i * 4);
			sym.Name = StringAt(table.Link, name);
			// a section symbol has no name of its own: it is the section's
			if (sym.Name.empty() && sym.Type == 3 && sym.SectionIndex < m_Sections.size())
				sym.Name = m_Sections[sym.SectionIndex].Name;
			m_Symbols.push_back(std::move(sym));
		}
	}
}

void ElfFile::ReadDynamic() {
	// the section, or the segment of a file without sections
	uint64_t offset = 0, size = 0;
	int strings = -1;
	for (uint32_t i = 0; i < m_Sections.size(); i++) {
		if (m_Sections[i].Type == ShtDynamic) {
			offset = m_Sections[i].Offset;
			size = m_Sections[i].Size;
			strings = (int)m_Sections[i].Link;
			break;
		}
	}
	if (size == 0) {
		for (auto const& ph : m_ProgramHeaders) {
			if (ph.Type == PtDynamic) {
				offset = ph.Offset;
				size = ph.FileSize;
				break;
			}
		}
	}
	if (size == 0 || !Fits(offset, size))
		return;

	size_t entrySize = m_Is64 ? 16 : 8;
	uint64_t stringTable = 0;	// DT_STRTAB, for a file without sections
	for (uint64_t at = offset; at + entrySize <= offset + size && m_Dynamic.size() < MaxEntries; at += entrySize) {
		ElfDynamicEntry e;
		e.Tag = m_Is64 ? (int64_t)U64(at) : (int32_t)U32(at);
		e.Value = Word(at + entrySize / 2);
		if (e.Tag == DtNull)
			break;
		if (e.Tag == DtStrtab)
			stringTable = e.Value;
		m_Dynamic.push_back(std::move(e));
	}
	for (auto& e : m_Dynamic) {
		if (e.Tag != DtNeeded && e.Tag != DtSoname && e.Tag != DtRpath && e.Tag != DtRunpath)
			continue;
		if (strings >= 0 && strings < (int)m_Sections.size())
			e.Text = StringAt((uint32_t)strings, e.Value);
		else if (auto at = OffsetOfAddress(stringTable); at >= 0 && Fits(at + e.Value, 1)) {
			auto p = (const char*)m_Data.data() + at + e.Value;
			e.Text.assign(p, strnlen(p, (size_t)(m_Data.size() - at - e.Value)));
		}
	}
}

void ElfFile::ReadRelocations() {
	size_t word = m_Is64 ? 8 : 4;
	// the symbols of each table, by their index in it
	auto symbolName = [&](uint32_t table, uint32_t index) -> std::string {
		if (index == 0 || table >= m_Sections.size())
			return {};
		auto const& t = m_Sections[table];
		size_t entrySize = m_Is64 ? 24 : 16;
		auto at = t.Offset + (uint64_t)index * entrySize;
		if ((uint64_t)index * entrySize >= t.Size || !Fits(at, entrySize))
			return {};
		auto name = StringAt(t.Link, U32(at));
		if (name.empty()) {
			// a section symbol
			auto shndx = m_Is64 ? U16(at + 6) : U16(at + 14);
			if (shndx < m_Sections.size())
				name = m_Sections[shndx].Name;
		}
		return name;
	};

	for (uint32_t s = 0; s < m_Sections.size(); s++) {
		auto const& sec = m_Sections[s];
		if (sec.Type != ShtRel && sec.Type != ShtRela && sec.Type != ShtRelr)
			continue;
		if (!Fits(sec.Offset, sec.Size)) {
			m_Problems.push_back(std::format(L"The relocations of {} are not in the file", Widen(sec.Name)));
			continue;
		}
		if (sec.Type == ShtRelr) {
			// packed relative relocations: an address, then bitmaps of the words that follow it
			uint64_t where = 0;
			for (uint64_t at = sec.Offset; at + word <= sec.Offset + sec.Size && m_Relocations.size() < MaxEntries; at += word) {
				auto entry = Word(at);
				if ((entry & 1) == 0) {
					where = entry;
					m_Relocations.push_back({ s, where, 0, 0, {}, 0, false, true });
					where += word;
				}
				else {
					uint64_t bits = entry >> 1;
					for (uint64_t i = 0; bits; i++, bits >>= 1)
						if (bits & 1)
							m_Relocations.push_back({ s, where + i * word, 0, 0, {}, 0, false, true });
					where += (word * 8 - 1) * word;
				}
			}
			continue;
		}

		bool rela = sec.Type == ShtRela;
		size_t entrySize = (rela ? 3 : 2) * word;
		auto count = std::min<uint64_t>(sec.Size / entrySize, MaxEntries);
		for (uint64_t i = 0; i < count; i++) {
			auto at = sec.Offset + i * entrySize;
			ElfRelocation r;
			r.Section = s;
			r.Offset = Word(at);
			auto info = Word(at + word);
			r.SymbolIndex = m_Is64 ? (uint32_t)(info >> 32) : (uint32_t)(info >> 8);
			r.Type = m_Is64 ? (uint32_t)info : (uint32_t)(info & 0xFF);
			r.HasAddend = rela;
			if (rela)
				r.Addend = m_Is64 ? (int64_t)U64(at + 2 * word) : (int32_t)U32(at + 2 * word);
			r.SymbolName = symbolName(sec.Link, r.SymbolIndex);
			m_Relocations.push_back(std::move(r));
		}
	}
}

void ElfFile::ReadNotes(uint64_t offset, uint64_t size, uint64_t align, std::string const& where) {
	if (!Fits(offset, size))
		return;
	align = align == 8 ? 8 : 4;
	auto pad = [&](uint64_t n) { return (n + align - 1) & ~(align - 1); };
	uint64_t at = offset, end = offset + size;
	while (at + 12 <= end && m_Notes.size() < MaxEntries) {
		auto nameSize = U32(at), descSize = U32(at + 4);
		ElfNote note;
		note.Type = U32(at + 8);
		note.Section = where;
		// the description starts aligned from the start of the note (8-byte notes: .note.gnu.property)
		auto name = at + 12, desc = at + pad(12 + (uint64_t)nameSize);
		if (desc + descSize > end || desc < name) {
			m_Problems.push_back(std::format(L"A note in {} does not fit in it", Widen(where)));
			return;
		}
		auto p = (const char*)m_Data.data() + name;
		note.Owner.assign(p, strnlen(p, nameSize));
		note.Description.assign(m_Data.begin() + desc, m_Data.begin() + desc + descSize);
		m_Notes.push_back(std::move(note));
		at = desc + pad(descSize);
	}
}

void ElfFile::ReadNotes() {
	bool any = false;
	for (auto const& s : m_Sections) {
		if (s.Type == ShtNote) {
			ReadNotes(s.Offset, s.Size, s.AddressAlign, s.Name);
			any = true;
		}
	}
	if (any)
		return;
	// a file without sections (a core dump, a stripped one)
	for (auto const& ph : m_ProgramHeaders)
		if (ph.Type == PtNote)
			ReadNotes(ph.Offset, ph.FileSize, ph.Align, "PT_NOTE");
}

std::vector<std::string> ElfFile::NeededLibraries() const {
	std::vector<std::string> libs;
	for (auto const& e : m_Dynamic)
		if (e.Tag == DtNeeded)
			libs.push_back(e.Text);
	return libs;
}

std::string ElfFile::DynamicString(int64_t tag) const {
	for (auto const& e : m_Dynamic)
		if (e.Tag == tag)
			return e.Text;
	return {};
}

std::string ElfFile::BuildId() const {
	for (auto const& n : m_Notes) {
		if (n.Owner == "GNU" && n.Type == 3) {
			std::string hex;
			for (auto b : n.Description)
				hex += std::format("{:02x}", std::to_integer<uint8_t>(b));
			return hex;
		}
	}
	return {};
}

std::span<const std::byte> ElfFile::SectionData(size_t index) const {
	if (index >= m_Sections.size())
		return {};
	auto const& s = m_Sections[index];
	if (s.Type == ShtNoBits || s.Offset >= m_Data.size())
		return {};
	return m_Data.subspan((size_t)s.Offset, (size_t)std::min<uint64_t>(s.Size, m_Data.size() - s.Offset));
}

int64_t ElfFile::OffsetOfAddress(uint64_t address) const {
	bool loads = false;
	for (auto const& ph : m_ProgramHeaders) {
		if (ph.Type != PtLoad)
			continue;
		loads = true;
		if (address >= ph.VirtualAddress && address - ph.VirtualAddress < ph.FileSize && Fits(ph.Offset + (address - ph.VirtualAddress), 1))
			return (int64_t)(ph.Offset + (address - ph.VirtualAddress));
	}
	if (!loads) {
		for (auto const& s : m_Sections) {
			if ((s.Flags & ShfAlloc) && s.Type != ShtNoBits && address >= s.Address && address - s.Address < s.Size)
				return (int64_t)(s.Offset + (address - s.Address));
		}
	}
	return -1;
}

int ElfFile::SectionOfAddress(uint64_t address) const {
	for (int i = 0; i < (int)m_Sections.size(); i++) {
		auto const& s = m_Sections[i];
		if ((s.Flags & ShfAlloc) && s.Size && address >= s.Address && address - s.Address < s.Size)
			return i;
	}
	return -1;
}

std::wstring ElfFile::TypeName(uint16_t type) {
	switch (type) {
		case 0: return L"None";
		case 1: return L"Relocatable";
		case 2: return L"Executable";
		case 3: return L"Shared Object";
		case 4: return L"Core Dump";
	}
	return Hex(type);
}

std::wstring ElfFile::MachineName(uint16_t machine) {
	switch (machine) {
		case 2: return L"SPARC";
		case 3: return L"x86";
		case 8: return L"MIPS";
		case 20: return L"PowerPC";
		case 21: return L"PowerPC64";
		case 22: return L"S/390";
		case 40: return L"ARM";
		case 42: return L"SuperH";
		case 43: return L"SPARC V9";
		case 50: return L"IA-64";
		case 62: return L"x86-64";
		case 183: return L"AArch64";
		case 243: return L"RISC-V";
		case 247: return L"BPF";
		case 258: return L"LoongArch";
	}
	return Hex(machine);
}

std::wstring ElfFile::OsAbiName(uint8_t abi) {
	switch (abi) {
		case 0: return L"System V";
		case 1: return L"HP-UX";
		case 2: return L"NetBSD";
		case 3: return L"Linux (GNU)";
		case 6: return L"Solaris";
		case 9: return L"FreeBSD";
		case 12: return L"OpenBSD";
		case 97: return L"ARM";
		case 255: return L"Standalone";
	}
	return Hex(abi);
}

std::wstring ElfFile::SegmentTypeName(uint32_t type) {
	switch (type) {
		case 0: return L"NULL";
		case 1: return L"LOAD";
		case 2: return L"DYNAMIC";
		case 3: return L"INTERP";
		case 4: return L"NOTE";
		case 5: return L"SHLIB";
		case 6: return L"PHDR";
		case 7: return L"TLS";
		case 0x6474E550: return L"GNU_EH_FRAME";
		case 0x6474E551: return L"GNU_STACK";
		case 0x6474E552: return L"GNU_RELRO";
		case 0x6474E553: return L"GNU_PROPERTY";
		case 0x6474E554: return L"GNU_SFRAME";
		case 0x70000001: return L"ARM_EXIDX";
	}
	return Hex(type);
}

std::wstring ElfFile::SegmentFlagsToString(uint32_t flags) {
	std::wstring text;
	text += flags & 4 ? L'R' : L'-';
	text += flags & 2 ? L'W' : L'-';
	text += flags & 1 ? L'X' : L'-';
	if (flags & ~7u)
		text += L" " + Hex(flags & ~7u);
	return text;
}

std::wstring ElfFile::SectionTypeName(uint32_t type) {
	switch (type) {
		case 0: return L"NULL";
		case 1: return L"PROGBITS";
		case 2: return L"SYMTAB";
		case 3: return L"STRTAB";
		case 4: return L"RELA";
		case 5: return L"HASH";
		case 6: return L"DYNAMIC";
		case 7: return L"NOTE";
		case 8: return L"NOBITS";
		case 9: return L"REL";
		case 10: return L"SHLIB";
		case 11: return L"DYNSYM";
		case 14: return L"INIT_ARRAY";
		case 15: return L"FINI_ARRAY";
		case 16: return L"PREINIT_ARRAY";
		case 17: return L"GROUP";
		case 18: return L"SYMTAB_SHNDX";
		case 19: return L"RELR";
		case 0x6FFFFFF5: return L"GNU_ATTRIBUTES";
		case 0x6FFFFFF6: return L"GNU_HASH";
		case 0x6FFFFFFD: return L"GNU_VERDEF";
		case 0x6FFFFFFE: return L"GNU_VERNEED";
		case 0x6FFFFFFF: return L"GNU_VERSYM";
		case 0x70000001: return L"X86_64_UNWIND / ARM_EXIDX";
		case 0x70000003: return L"ARM_ATTRIBUTES";
	}
	return Hex(type);
}

std::wstring ElfFile::SectionFlagsToString(uint64_t flags) {
	return FlagNames(flags, { { 1, L"WRITE" }, { 2, L"ALLOC" }, { 4, L"EXEC" }, { 0x10, L"MERGE" }, { 0x20, L"STRINGS" }, { 0x40, L"INFO_LINK" },
		{ 0x80, L"LINK_ORDER" }, { 0x100, L"OS_NONCONFORMING" }, { 0x200, L"GROUP" }, { 0x400, L"TLS" }, { 0x800, L"COMPRESSED" },
		{ 0x200000, L"GNU_RETAIN" }, { 0x80000000, L"EXCLUDE" } });
}

std::wstring ElfFile::SymbolTypeName(uint8_t type) {
	switch (type) {
		case 0: return L"NOTYPE";
		case 1: return L"OBJECT";
		case 2: return L"FUNC";
		case 3: return L"SECTION";
		case 4: return L"FILE";
		case 5: return L"COMMON";
		case 6: return L"TLS";
		case 10: return L"GNU_IFUNC";
	}
	return Hex(type);
}

std::wstring ElfFile::SymbolBindName(uint8_t bind) {
	switch (bind) {
		case 0: return L"LOCAL";
		case 1: return L"GLOBAL";
		case 2: return L"WEAK";
		case 10: return L"GNU_UNIQUE";
	}
	return Hex(bind);
}

std::wstring ElfFile::SymbolVisibilityName(uint8_t visibility) {
	switch (visibility) {
		case 0: return L"DEFAULT";
		case 1: return L"INTERNAL";
		case 2: return L"HIDDEN";
		case 3: return L"PROTECTED";
	}
	return Hex(visibility);
}

std::wstring ElfFile::DynamicTagName(int64_t tag) {
	static const std::unordered_map<int64_t, PCWSTR> names{
		{ 0, L"NULL" }, { 1, L"NEEDED" }, { 2, L"PLTRELSZ" }, { 3, L"PLTGOT" }, { 4, L"HASH" }, { 5, L"STRTAB" }, { 6, L"SYMTAB" },
		{ 7, L"RELA" }, { 8, L"RELASZ" }, { 9, L"RELAENT" }, { 10, L"STRSZ" }, { 11, L"SYMENT" }, { 12, L"INIT" }, { 13, L"FINI" },
		{ 14, L"SONAME" }, { 15, L"RPATH" }, { 16, L"SYMBOLIC" }, { 17, L"REL" }, { 18, L"RELSZ" }, { 19, L"RELENT" }, { 20, L"PLTREL" },
		{ 21, L"DEBUG" }, { 22, L"TEXTREL" }, { 23, L"JMPREL" }, { 24, L"BIND_NOW" }, { 25, L"INIT_ARRAY" }, { 26, L"FINI_ARRAY" },
		{ 27, L"INIT_ARRAYSZ" }, { 28, L"FINI_ARRAYSZ" }, { 29, L"RUNPATH" }, { 30, L"FLAGS" }, { 32, L"PREINIT_ARRAY" },
		{ 33, L"PREINIT_ARRAYSZ" }, { 35, L"RELRSZ" }, { 36, L"RELR" }, { 37, L"RELRENT" },
		{ 0x6FFFFEF5, L"GNU_HASH" }, { 0x6FFFFFF0, L"VERSYM" }, { 0x6FFFFFF9, L"RELACOUNT" }, { 0x6FFFFFFA, L"RELCOUNT" },
		{ 0x6FFFFFFB, L"FLAGS_1" }, { 0x6FFFFFFC, L"VERDEF" }, { 0x6FFFFFFD, L"VERDEFNUM" }, { 0x6FFFFFFE, L"VERNEED" }, { 0x6FFFFFFF, L"VERNEEDNUM" },
	};
	if (auto it = names.find(tag); it != names.end())
		return it->second;
	return Hex((uint64_t)tag);
}

std::wstring ElfFile::RelocationTypeName(uint16_t machine, uint32_t type) {
	static const std::unordered_map<uint32_t, PCWSTR> x64{
		{ 0, L"NONE" }, { 1, L"64" }, { 2, L"PC32" }, { 3, L"GOT32" }, { 4, L"PLT32" }, { 5, L"COPY" }, { 6, L"GLOB_DAT" }, { 7, L"JUMP_SLOT" },
		{ 8, L"RELATIVE" }, { 9, L"GOTPCREL" }, { 10, L"32" }, { 11, L"32S" }, { 12, L"16" }, { 13, L"PC16" }, { 14, L"8" }, { 15, L"PC8" },
		{ 16, L"DTPMOD64" }, { 17, L"DTPOFF64" }, { 18, L"TPOFF64" }, { 19, L"TLSGD" }, { 20, L"TLSLD" }, { 21, L"DTPOFF32" },
		{ 22, L"GOTTPOFF" }, { 23, L"TPOFF32" }, { 24, L"PC64" }, { 25, L"GOTOFF64" }, { 26, L"GOTPC32" }, { 32, L"SIZE32" }, { 33, L"SIZE64" },
		{ 34, L"GOTPC32_TLSDESC" }, { 35, L"TLSDESC_CALL" }, { 36, L"TLSDESC" }, { 37, L"IRELATIVE" }, { 41, L"GOTPCRELX" }, { 42, L"REX_GOTPCRELX" },
	};
	static const std::unordered_map<uint32_t, PCWSTR> x86{
		{ 0, L"NONE" }, { 1, L"32" }, { 2, L"PC32" }, { 3, L"GOT32" }, { 4, L"PLT32" }, { 5, L"COPY" }, { 6, L"GLOB_DAT" }, { 7, L"JMP_SLOT" },
		{ 8, L"RELATIVE" }, { 9, L"GOTOFF" }, { 10, L"GOTPC" }, { 14, L"TLS_TPOFF" }, { 35, L"TLS_DTPMOD32" }, { 36, L"TLS_DTPOFF32" },
		{ 37, L"TLS_TPOFF32" }, { 42, L"IRELATIVE" }, { 43, L"GOT32X" },
	};
	static const std::unordered_map<uint32_t, PCWSTR> arm64{
		{ 0, L"NONE" }, { 257, L"ABS64" }, { 258, L"ABS32" }, { 259, L"ABS16" }, { 260, L"PREL64" }, { 261, L"PREL32" }, { 262, L"PREL16" },
		{ 275, L"ADR_PREL_PG_HI21" }, { 277, L"ADD_ABS_LO12_NC" }, { 278, L"LDST8_ABS_LO12_NC" }, { 282, L"JUMP26" }, { 283, L"CALL26" },
		{ 284, L"LDST16_ABS_LO12_NC" }, { 285, L"LDST32_ABS_LO12_NC" }, { 286, L"LDST64_ABS_LO12_NC" }, { 299, L"LDST128_ABS_LO12_NC" },
		{ 311, L"ADR_GOT_PAGE" }, { 312, L"LD64_GOT_LO12_NC" }, { 1024, L"COPY" }, { 1025, L"GLOB_DAT" }, { 1026, L"JUMP_SLOT" },
		{ 1027, L"RELATIVE" }, { 1028, L"TLS_DTPMOD" }, { 1029, L"TLS_DTPREL" }, { 1030, L"TLS_TPREL" }, { 1031, L"TLSDESC" }, { 1032, L"IRELATIVE" },
	};
	auto find = [&](auto const& names, PCWSTR prefix) -> std::wstring {
		if (auto it = names.find(type); it != names.end())
			return std::wstring(prefix) + it->second;
		return Hex(type);
	};
	switch (machine) {
		case 62: return find(x64, L"R_X86_64_");
		case 3: return find(x86, L"R_386_");
		case 183: return find(arm64, L"R_AARCH64_");
	}
	return Hex(type);
}

std::wstring ElfFile::NoteTypeName(std::string const& owner, uint32_t type) {
	if (owner == "GNU") {
		switch (type) {
			case 1: return L"NT_GNU_ABI_TAG";
			case 2: return L"NT_GNU_HWCAP";
			case 3: return L"NT_GNU_BUILD_ID";
			case 4: return L"NT_GNU_GOLD_VERSION";
			case 5: return L"NT_GNU_PROPERTY_TYPE_0";
		}
	}
	else if (owner == "CORE" || owner == "LINUX") {
		switch (type) {
			case 1: return L"NT_PRSTATUS";
			case 2: return L"NT_FPREGSET";
			case 3: return L"NT_PRPSINFO";
			case 6: return L"NT_AUXV";
			case 0x46494C45: return L"NT_FILE";
			case 0x53494749: return L"NT_SIGINFO";
		}
	}
	else if (owner == "stapsdt" && type == 3)
		return L"NT_STAPSDT";
	else if (owner == "Go" && type == 4)
		return L"Go Build ID";
	else if (owner == "FreeBSD" && type == 1)
		return L"NT_FREEBSD_ABI_TAG";
	return Hex(type);
}

std::wstring ElfFile::NoteDescription(ElfNote const& note) const {
	auto const& d = note.Description;
	auto word = [&](size_t i) {
		uint32_t v = 0;
		if (i * 4 + 4 <= d.size())
			memcpy(&v, d.data() + i * 4, 4);
		return m_BigEndian ? _byteswap_ulong(v) : v;
	};
	if (note.Owner == "GNU" && note.Type == 1 && d.size() >= 16) {
		static PCWSTR const os[] = { L"Linux", L"Hurd", L"Solaris", L"FreeBSD", L"NetBSD", L"Syllable" };
		auto o = word(0);
		return std::format(L"{} {}.{}.{} or later", o < _countof(os) ? os[o] : Hex(o).c_str(), word(1), word(2), word(3));
	}
	if ((note.Owner == "GNU" && note.Type == 4) || (note.Owner == "Go" && note.Type == 4)) {
		std::string text((const char*)d.data(), strnlen((const char*)d.data(), d.size()));
		return Widen(text);
	}
	std::wstring hex;
	for (size_t i = 0; i < d.size() && i < 64; i++)
		hex += std::format(L"{:02x}", std::to_integer<uint8_t>(d[i]));
	if (d.size() > 64)
		hex += std::format(L"... ({} bytes)", d.size());
	return hex;
}
