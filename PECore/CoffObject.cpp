#include "pch.h"
#include "CoffObject.h"
#include "LibArchive.h"
#include <fstream>

namespace {
	// {D1BAA1C7-BAEE-4BA9-AF20-FAF66AA4DCB8}, as it is in the header of a bigobj
	constexpr uint8_t BigObjClassId[16] = { 0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B, 0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8 };

	constexpr size_t HeaderSize = 20, BigHeaderSize = 56, SectionHeaderSize = 40, RelocationSize = 10;
	constexpr size_t SymbolSize = 18, BigSymbolSize = 20;

	template<typename T>
	T Read(std::span<const std::byte> data, size_t offset) {
		T value{};
		if (offset <= data.size() && data.size() - offset >= sizeof(T))
			memcpy(&value, data.data() + offset, sizeof(T));
		return value;
	}

	bool Fits(std::span<const std::byte> data, uint64_t offset, uint64_t size) {
		return offset <= data.size() && data.size() - offset >= size;
	}

	bool IsBigObj(std::span<const std::byte> data) {
		return Fits(data, 0, BigHeaderSize) && Read<uint16_t>(data, 0) == 0 && Read<uint16_t>(data, 2) == 0xFFFF && Read<uint16_t>(data, 4) >= 2 &&
			memcmp(data.data() + 12, BigObjClassId, 16) == 0;
	}

	std::wstring Hex(uint32_t value) {
		return std::format(L"0x{:X}", value);
	}

	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	// the name of a symbol or a file in an auxiliary record: up to the first zero
	std::string FixedString(const char* p, size_t size) {
		size_t n = 0;
		while (n < size && p[n])
			n++;
		return std::string(p, n);
	}

	const char* ComdatSelectionName(uint8_t selection) {
		switch (selection) {
			case 1: return "No Duplicates";
			case 2: return "Any";
			case 3: return "Same Size";
			case 4: return "Exact Match";
			case 5: return "Associative";
			case 6: return "Largest";
			case 7: return "Newest";
		}
		return nullptr;
	}
}

// The header of an object (the first bytes of the file at least) and the size of the file: the tables have to fit in it
bool CoffObject::IsObjectHeader(std::span<const std::byte> header, uint64_t fileSize) {
	auto fits = [&](uint64_t offset, uint64_t size) { return offset <= fileSize && fileSize - offset >= size; };
	if (IsBigObj(header)) {
		auto sections = Read<uint32_t>(header, 44), table = Read<uint32_t>(header, 48), symbols = Read<uint32_t>(header, 52);
		return fits(BigHeaderSize, (uint64_t)sections * SectionHeaderSize) && (symbols == 0 || fits(table, (uint64_t)symbols * BigSymbolSize));
	}
	if (!Fits(header, 0, HeaderSize))
		return false;
	auto machine = Read<uint16_t>(header, 0), sections = Read<uint16_t>(header, 2);
	auto table = Read<uint32_t>(header, 8), symbols = Read<uint32_t>(header, 12);
	// objects have no optional header; machine 0 is for objects without code (debug information): those need a section at least
	if (Read<uint16_t>(header, 16) != 0 || sections > 0xFEFF || (sections == 0 && symbols == 0))
		return false;
	if (machine == 0 ? sections == 0 : LibArchive::MachineName(machine).empty())
		return false;
	return fits(HeaderSize, (uint64_t)sections * SectionHeaderSize) && (symbols == 0 || fits(table, (uint64_t)symbols * SymbolSize));
}

bool CoffObject::IsObject(std::span<const std::byte> data) {
	return IsObjectHeader(data, data.size());
}

bool CoffObject::IsObjectFile(std::wstring_view path) {
	std::ifstream in(std::wstring(path), std::ios::binary | std::ios::ate);
	if (!in)
		return false;
	auto fileSize = (uint64_t)in.tellg();
	in.seekg(0);
	std::vector<std::byte> header(BigHeaderSize);
	in.read((char*)header.data(), header.size());
	header.resize((size_t)in.gcount());
	return IsObjectHeader(header, fileSize);
}

bool CoffObject::Open(std::wstring_view path) {
	std::ifstream in(std::wstring(path), std::ios::binary);
	if (!in)
		return false;
	std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
	if (!Parse(std::as_bytes(std::span(bytes))))
		return false;
	m_Path = path;
	return true;
}

void CoffObject::Close() {
	*this = CoffObject();
}

bool CoffObject::Parse(std::span<const std::byte> data) {
	Close();
	if (!IsObject(data))
		return false;
	m_Copy.assign(data.begin(), data.end());
	m_Data = m_Copy;

	uint32_t sections;
	size_t tableOffset, symbolSize;
	m_BigObj = IsBigObj(m_Data);
	if (m_BigObj) {
		m_Machine = Read<uint16_t>(m_Data, 6);
		m_TimeDateStamp = Read<uint32_t>(m_Data, 8);
		sections = Read<uint32_t>(m_Data, 44);
		m_SymbolTable = Read<uint32_t>(m_Data, 48);
		m_SymbolCount = Read<uint32_t>(m_Data, 52);
		tableOffset = BigHeaderSize;
		symbolSize = BigSymbolSize;
	}
	else {
		m_Machine = Read<uint16_t>(m_Data, 0);
		sections = Read<uint16_t>(m_Data, 2);
		m_TimeDateStamp = Read<uint32_t>(m_Data, 4);
		m_SymbolTable = Read<uint32_t>(m_Data, 8);
		m_SymbolCount = Read<uint32_t>(m_Data, 12);
		m_Characteristics = Read<uint16_t>(m_Data, 18);
		tableOffset = HeaderSize;
		symbolSize = SymbolSize;
	}

	// the string table follows the symbols; its size (which counts itself) is first
	if (m_SymbolCount) {
		auto strings = m_SymbolTable + (uint64_t)m_SymbolCount * symbolSize;
		if (Fits(m_Data, strings, 4)) {
			m_StringTableSize = Read<uint32_t>(m_Data, (size_t)strings);
			if (!Fits(m_Data, strings, m_StringTableSize)) {
				m_Problems.push_back(std::format(L"The string table ({} bytes) does not fit in the file", m_StringTableSize));
				m_StringTableSize = (uint32_t)(m_Data.size() - strings);
			}
		}
	}

	ReadSections(tableOffset, sections);
	ReadSymbols(symbolSize);
	ReadRelocations();
	ReadLineNumbers();
	return true;
}

std::string CoffObject::StringAt(uint32_t offset) const {
	auto strings = m_SymbolTable + (uint64_t)m_SymbolCount * (m_BigObj ? BigSymbolSize : SymbolSize);
	if (offset < 4 || offset >= m_StringTableSize)
		return {};
	auto p = (const char*)m_Data.data() + strings + offset;
	return FixedString(p, m_StringTableSize - offset);
}

// "/123" is an offset in the string table; a bigobj writes the long ones as "//" and the offset in base 64
std::string CoffObject::SectionName(const char* raw) const {
	auto name = FixedString(raw, 8);
	if (name.size() > 2 && name[0] == '/' && name[1] == '/') {
		uint64_t offset = 0;
		for (size_t i = 2; i < name.size(); i++) {
			auto c = name[i];
			int digit = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
				c == '+' ? 62 : c == '/' ? 63 : -1;
			if (digit < 0)
				return name;
			offset = offset * 64 + digit;
		}
		auto s = StringAt((uint32_t)offset);
		return s.empty() ? name : s;
	}
	if (name.size() > 1 && name[0] == '/' && isdigit((uint8_t)name[1])) {
		auto s = StringAt((uint32_t)strtoul(name.c_str() + 1, nullptr, 10));
		return s.empty() ? name : s;
	}
	return name;
}

void CoffObject::ReadSections(size_t tableOffset, uint32_t count) {
	m_Sections.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		auto at = tableOffset + (size_t)i * SectionHeaderSize;
		CoffSection s;
		s.Name = SectionName((const char*)m_Data.data() + at);
		s.VirtualSize = Read<uint32_t>(m_Data, at + 8);
		s.VirtualAddress = Read<uint32_t>(m_Data, at + 12);
		s.SizeOfRawData = Read<uint32_t>(m_Data, at + 16);
		s.PointerToRawData = Read<uint32_t>(m_Data, at + 20);
		s.PointerToRelocations = Read<uint32_t>(m_Data, at + 24);
		s.PointerToLinenumbers = Read<uint32_t>(m_Data, at + 28);
		s.NumberOfRelocations = Read<uint16_t>(m_Data, at + 32);
		s.NumberOfLinenumbers = Read<uint16_t>(m_Data, at + 34);
		s.Characteristics = Read<uint32_t>(m_Data, at + 36);
		// too many relocations for the header: the first relocation has their number (which counts itself)
		if ((s.Characteristics & IMAGE_SCN_LNK_NRELOC_OVFL) && s.NumberOfRelocations == 0xFFFF) {
			s.NumberOfRelocations = Read<uint32_t>(m_Data, s.PointerToRelocations);
			s.RelocationOverflow = true;
		}
		if (s.SizeOfRawData && !Fits(m_Data, s.PointerToRawData, s.SizeOfRawData) && !(s.Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA))
			m_Problems.push_back(std::format(L"The data of section {} ({}) is outside the file", i + 1, Widen(s.Name)));
		m_Sections.push_back(std::move(s));
	}
}

void CoffObject::ReadSymbols(size_t symbolSize) {
	if (!Fits(m_Data, m_SymbolTable, (uint64_t)m_SymbolCount * symbolSize)) {
		if (m_SymbolCount)
			m_Problems.push_back(L"The symbol table is outside the file");
		return;
	}
	m_SymbolPositions.assign(m_SymbolCount, -1);
	auto nameOf = [&](size_t at) {
		// a short name in place, or 4 zeros and an offset in the string table
		if (Read<uint32_t>(m_Data, at) == 0)
			return StringAt(Read<uint32_t>(m_Data, at + 4));
		return FixedString((const char*)m_Data.data() + at, 8);
	};

	for (uint32_t i = 0; i < m_SymbolCount; ) {
		auto at = m_SymbolTable + (size_t)i * symbolSize;
		CoffSymbol sym;
		sym.Index = i;
		sym.Name = nameOf(at);
		sym.Value = Read<uint32_t>(m_Data, at + 8);
		if (m_BigObj) {
			sym.SectionNumber = Read<int32_t>(m_Data, at + 12);
			sym.Type = Read<uint16_t>(m_Data, at + 16);
			sym.StorageClass = Read<uint8_t>(m_Data, at + 18);
			sym.AuxCount = Read<uint8_t>(m_Data, at + 19);
		}
		else {
			sym.SectionNumber = Read<int16_t>(m_Data, at + 12);
			sym.Type = Read<uint16_t>(m_Data, at + 14);
			sym.StorageClass = Read<uint8_t>(m_Data, at + 16);
			sym.AuxCount = Read<uint8_t>(m_Data, at + 17);
		}
		auto aux = at + symbolSize;
		uint32_t auxCount = std::min<uint32_t>(sym.AuxCount, m_SymbolCount - i - 1);

		if (auxCount) {
			if (sym.StorageClass == IMAGE_SYM_CLASS_FILE) {
				// the name of the source file, in as many records as it takes
				sym.Details = FixedString((const char*)m_Data.data() + aux, auxCount * symbolSize);
			}
			else if (sym.StorageClass == IMAGE_SYM_CLASS_STATIC && sym.SectionNumber > 0 && sym.Type == 0 && sym.Value == 0) {
				// the definition of a section (which a COMDAT needs)
				auto length = Read<uint32_t>(m_Data, aux);
				auto relocations = Read<uint16_t>(m_Data, aux + 4);
				auto checksum = Read<uint32_t>(m_Data, aux + 8);
				uint32_t number = Read<uint16_t>(m_Data, aux + 12);
				if (m_BigObj)
					number |= (uint32_t)Read<uint16_t>(m_Data, aux + 16) << 16;
				auto selection = Read<uint8_t>(m_Data, aux + 14);
				sym.Details = std::format("Length 0x{:X}, {} relocations, checksum 0x{:08X}", length, relocations, checksum);
				if (auto name = ComdatSelectionName(selection)) {
					sym.Details += std::format(", COMDAT: {}", name);
					if (selection == 5)
						sym.Details += std::format(" (with section {})", number);
				}
			}
			else if (sym.StorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL ||
				(sym.StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym.SectionNumber == 0 && sym.Value == 0)) {
				// a weak external: the symbol that is used if this one is not defined
				auto tag = Read<uint32_t>(m_Data, aux);
				auto kind = Read<uint32_t>(m_Data, aux + 4);
				auto target = tag < m_SymbolCount ? nameOf(m_SymbolTable + (size_t)tag * symbolSize) : std::string("?");
				sym.Details = std::format("Weak external; default: {} ({})", target,
					kind == 1 ? "no library search" : kind == 2 ? "library search" : kind == 3 ? "alias" : kind == 4 ? "anti-dependency" : "?");
			}
			else if (sym.StorageClass == IMAGE_SYM_CLASS_EXTERNAL && (sym.Type >> 4) == IMAGE_SYM_DTYPE_FUNCTION && sym.SectionNumber > 0) {
				sym.Details = std::format("Function, size 0x{:X}", Read<uint32_t>(m_Data, aux + 4));
			}
		}

		m_SymbolPositions[i] = (int)m_Symbols.size();
		m_Symbols.push_back(std::move(sym));
		i += 1 + auxCount;
	}
}

void CoffObject::ReadRelocations() {
	for (uint32_t s = 0; s < (uint32_t)m_Sections.size(); s++) {
		auto const& sec = m_Sections[s];
		uint32_t count = sec.NumberOfRelocations;
		uint32_t first = sec.RelocationOverflow ? 1 : 0;	// the first one is the count
		if (count == 0)
			continue;
		if (!Fits(m_Data, sec.PointerToRelocations, (uint64_t)count * RelocationSize)) {
			m_Problems.push_back(std::format(L"The relocations of section {} ({}) are outside the file", s + 1, Widen(sec.Name)));
			continue;
		}
		for (uint32_t r = first; r < count; r++) {
			auto at = sec.PointerToRelocations + (size_t)r * RelocationSize;
			m_Relocations.push_back({ s, Read<uint32_t>(m_Data, at), Read<uint32_t>(m_Data, at + 4), Read<uint16_t>(m_Data, at + 8) });
		}
	}
}

void CoffObject::ReadLineNumbers() {
	constexpr size_t LineNumberSize = 6;
	for (uint32_t s = 0; s < (uint32_t)m_Sections.size(); s++) {
		auto const& sec = m_Sections[s];
		if (sec.NumberOfLinenumbers == 0)
			continue;
		if (!Fits(m_Data, sec.PointerToLinenumbers, (uint64_t)sec.NumberOfLinenumbers * LineNumberSize)) {
			m_Problems.push_back(std::format(L"The line numbers of section {} ({}) are outside the file", s + 1, Widen(sec.Name)));
			continue;
		}
		for (uint32_t i = 0; i < sec.NumberOfLinenumbers; i++) {
			auto at = sec.PointerToLinenumbers + (size_t)i * LineNumberSize;
			m_LineNumbers.push_back({ s, Read<uint32_t>(m_Data, at), Read<uint16_t>(m_Data, at + 4) });
		}
	}
}

std::span<const std::byte> CoffObject::SectionData(size_t index) const {
	if (index >= m_Sections.size())
		return {};
	auto const& s = m_Sections[index];
	if (s.Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA || s.PointerToRawData >= m_Data.size())
		return {};
	return m_Data.subspan(s.PointerToRawData, std::min<size_t>(s.SizeOfRawData, m_Data.size() - s.PointerToRawData));
}

CoffSymbol const* CoffObject::SymbolByIndex(uint32_t index) const {
	if (index >= m_SymbolPositions.size() || m_SymbolPositions[index] < 0)
		return nullptr;
	return &m_Symbols[m_SymbolPositions[index]];
}

std::string CoffObject::Directives() const {
	std::string text;
	for (size_t i = 0; i < m_Sections.size(); i++) {
		if (m_Sections[i].Name != ".drectve" || !(m_Sections[i].Characteristics & IMAGE_SCN_LNK_INFO))
			continue;
		auto data = SectionData(i);
		size_t start = 0;
		// UTF-8 with a byte order mark is allowed
		if (data.size() >= 3 && data[0] == std::byte{ 0xEF } && data[1] == std::byte{ 0xBB } && data[2] == std::byte{ 0xBF })
			start = 3;
		text.append((const char*)data.data() + start, data.size() - start);
	}
	while (!text.empty() && (text.back() == '\0' || text.back() == ' '))
		text.pop_back();
	auto start = text.find_first_not_of(' ');
	return start == std::string::npos ? std::string() : text.substr(start);
}

std::wstring CoffObject::StorageClassName(uint8_t storageClass) {
	switch (storageClass) {
		case IMAGE_SYM_CLASS_END_OF_FUNCTION: return L"End of Function";
		case IMAGE_SYM_CLASS_NULL: return L"Null";
		case IMAGE_SYM_CLASS_AUTOMATIC: return L"Automatic";
		case IMAGE_SYM_CLASS_EXTERNAL: return L"External";
		case IMAGE_SYM_CLASS_STATIC: return L"Static";
		case IMAGE_SYM_CLASS_REGISTER: return L"Register";
		case IMAGE_SYM_CLASS_EXTERNAL_DEF: return L"External Definition";
		case IMAGE_SYM_CLASS_LABEL: return L"Label";
		case IMAGE_SYM_CLASS_UNDEFINED_LABEL: return L"Undefined Label";
		case IMAGE_SYM_CLASS_MEMBER_OF_STRUCT: return L"Member of Struct";
		case IMAGE_SYM_CLASS_ARGUMENT: return L"Argument";
		case IMAGE_SYM_CLASS_STRUCT_TAG: return L"Struct Tag";
		case IMAGE_SYM_CLASS_MEMBER_OF_UNION: return L"Member of Union";
		case IMAGE_SYM_CLASS_UNION_TAG: return L"Union Tag";
		case IMAGE_SYM_CLASS_TYPE_DEFINITION: return L"Type Definition";
		case IMAGE_SYM_CLASS_UNDEFINED_STATIC: return L"Undefined Static";
		case IMAGE_SYM_CLASS_ENUM_TAG: return L"Enum Tag";
		case IMAGE_SYM_CLASS_MEMBER_OF_ENUM: return L"Member of Enum";
		case IMAGE_SYM_CLASS_REGISTER_PARAM: return L"Register Parameter";
		case IMAGE_SYM_CLASS_BIT_FIELD: return L"Bit Field";
		case IMAGE_SYM_CLASS_BLOCK: return L"Block";
		case IMAGE_SYM_CLASS_FUNCTION: return L"Function";
		case IMAGE_SYM_CLASS_END_OF_STRUCT: return L"End of Struct";
		case IMAGE_SYM_CLASS_FILE: return L"File";
		case IMAGE_SYM_CLASS_SECTION: return L"Section";
		case IMAGE_SYM_CLASS_WEAK_EXTERNAL: return L"Weak External";
		case IMAGE_SYM_CLASS_CLR_TOKEN: return L"CLR Token";
	}
	return std::format(L"0x{:X}", storageClass);
}

std::wstring CoffObject::SectionNumberName(int32_t number) {
	switch (number) {
		case IMAGE_SYM_UNDEFINED: return L"Undefined";
		case IMAGE_SYM_ABSOLUTE: return L"Absolute";
		case IMAGE_SYM_DEBUG: return L"Debug";
	}
	return std::to_wstring(number);
}

std::wstring CoffObject::RelocationTypeName(uint16_t machine, uint16_t type) {
	static const wchar_t* const amd64[] = {
		L"ABSOLUTE", L"ADDR64", L"ADDR32", L"ADDR32NB", L"REL32", L"REL32_1", L"REL32_2", L"REL32_3", L"REL32_4", L"REL32_5",
		L"SECTION", L"SECREL", L"SECREL7", L"TOKEN", L"SREL32", L"PAIR", L"SSPAN32",
	};
	static const wchar_t* const arm64[] = {
		L"ABSOLUTE", L"ADDR32", L"ADDR32NB", L"BRANCH26", L"PAGEBASE_REL21", L"REL21", L"PAGEOFFSET_12A", L"PAGEOFFSET_12L",
		L"SECREL", L"SECREL_LOW12A", L"SECREL_HIGH12A", L"SECREL_LOW12L", L"TOKEN", L"SECTION", L"ADDR64", L"BRANCH19", L"BRANCH14", L"REL32",
	};
	switch (machine) {
		case IMAGE_FILE_MACHINE_AMD64:
			if (type < _countof(amd64))
				return amd64[type];
			break;
		case IMAGE_FILE_MACHINE_ARM64:
		case 0xA641:	// ARM64EC
		case 0xA64E:	// ARM64X
			if (type < _countof(arm64))
				return arm64[type];
			break;
		case IMAGE_FILE_MACHINE_I386:
			switch (type) {
				case 0x00: return L"ABSOLUTE";
				case 0x01: return L"DIR16";
				case 0x02: return L"REL16";
				case 0x06: return L"DIR32";
				case 0x07: return L"DIR32NB";
				case 0x09: return L"SEG12";
				case 0x0A: return L"SECTION";
				case 0x0B: return L"SECREL";
				case 0x0C: return L"TOKEN";
				case 0x0D: return L"SECREL7";
				case 0x14: return L"REL32";
			}
			break;
	}
	return Hex(type);
}

std::wstring CoffObject::CharacteristicsToString(uint16_t characteristics) {
	std::wstring text;
	auto add = [&](uint16_t flag, PCWSTR name) {
		if (characteristics & flag)
			text += (text.empty() ? L"" : L", ") + std::wstring(name);
	};
	add(IMAGE_FILE_RELOCS_STRIPPED, L"Relocations Stripped");
	add(IMAGE_FILE_LINE_NUMS_STRIPPED, L"Line Numbers Stripped");
	add(IMAGE_FILE_LOCAL_SYMS_STRIPPED, L"Local Symbols Stripped");
	add(IMAGE_FILE_LARGE_ADDRESS_AWARE, L"Large Address Aware");
	add(IMAGE_FILE_32BIT_MACHINE, L"32-bit");
	add(IMAGE_FILE_DEBUG_STRIPPED, L"Debug Stripped");
	return text;
}
