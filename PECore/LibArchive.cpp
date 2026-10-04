#include "pch.h"
#include "LibArchive.h"
#include <algorithm>
#include <cstring>

namespace {
	constexpr size_t SignatureSize = 8;
	constexpr size_t HeaderSize = 60;
	constexpr char Signature[] = "!<arch>\n";

	template<typename T>
	bool ReadAt(std::span<const std::byte> data, uint64_t offset, T& value) {
		if (offset > data.size() || data.size() - offset < sizeof(T))
			return false;
		memcpy(&value, data.data() + offset, sizeof(T));
		return true;
	}

	uint32_t FromBigEndian(uint32_t v) {
		return _byteswap_ulong(v);
	}

	// a number that is written in text, padded with spaces
	bool ParseNumber(const char* text, size_t length, int base, uint64_t& value) {
		size_t i = 0;
		while (i < length && text[i] == ' ')
			i++;
		size_t digits = 0;
		value = 0;
		for (; i < length && text[i] != ' ' && text[i] != '\0'; i++, digits++) {
			int d;
			if (text[i] >= '0' && text[i] <= '9')
				d = text[i] - '0';
			else
				return false;
			if (d >= base || value > (UINT64_MAX - d) / base)
				return false;
			value = value * base + d;
		}
		return digits > 0;
	}

	std::string TrimRight(const char* text, size_t length) {
		while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\0'))
			length--;
		return std::string(text, length);
	}

	// a string that ends in a null (or at the end of the data)
	std::string ReadCString(std::span<const std::byte> data, size_t& pos) {
		size_t start = pos;
		while (pos < data.size() && data[pos] != std::byte{ 0 })
			pos++;
		std::string s(reinterpret_cast<const char*>(data.data()) + start, pos - start);
		if (pos < data.size())
			pos++;
		return s;
	}

	bool KnownMachine(uint16_t machine) {
		switch (machine) {
			case 0x14c: case 0x8664: case 0x1c0: case 0x1c2: case 0x1c4: case 0xaa64: case 0xa641: case 0xa64e:
			case 0x200: case 0x1f0: case 0x1f1: case 0x366: case 0x466: case 0x5032: case 0x5064: case 0x6232:
			case 0x6264: case 0xebc:
				return true;
		}
		return false;
	}

#pragma pack(push, 1)
	struct ImportHeader {
		uint16_t Sig1, Sig2, Version, Machine;
		uint32_t TimeDateStamp, SizeOfData;
		uint16_t OrdinalOrHint, Flags;
	};
	struct ObjectHeader {
		uint16_t Machine, Sections;
		uint32_t TimeDateStamp, SymbolTable, Symbols;
		uint16_t OptionalHeader, Characteristics;
	};
	struct BigObjectHeader {
		uint16_t Sig1, Sig2, Version, Machine;
		uint32_t TimeDateStamp;
		uint8_t ClassId[16];
		uint32_t SizeOfData, Flags, MetaDataSize, MetaDataOffset, Sections, SymbolTable, Symbols;
	};
#pragma pack(pop)
	static_assert(sizeof(ImportHeader) == 20);
	static_assert(sizeof(ObjectHeader) == 20);
	static_assert(sizeof(BigObjectHeader) == 56);
}

bool LibArchive::IsArchive(std::span<const std::byte> data) {
	return data.size() >= SignatureSize && memcmp(data.data(), Signature, SignatureSize) == 0;
}

bool LibArchive::IsArchiveFile(std::wstring_view path) {
	wil::unique_hfile file(::CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return false;
	std::byte buffer[SignatureSize];
	DWORD read = 0;
	if (!::ReadFile(file.get(), buffer, SignatureSize, &read, nullptr) || read != SignatureSize)
		return false;
	return IsArchive(buffer);
}

bool LibArchive::Open(std::wstring_view path) {
	Close();
	wil::unique_hfile file(::CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return false;
	LARGE_INTEGER size;
	if (!::GetFileSizeEx(file.get(), &size) || size.QuadPart < (LONGLONG)SignatureSize || (uint64_t)size.QuadPart > SIZE_MAX / 2)
		return false;
	wil::unique_handle mapping(::CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
	if (!mapping)
		return false;
	m_Map.reset(static_cast<uint8_t*>(::MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0)));
	if (!m_Map)
		return false;
	m_Data = std::span<const std::byte>(reinterpret_cast<const std::byte*>(m_Map.get()), (size_t)size.QuadPart);
	m_Path = path;
	if (!Parse()) {
		Close();
		return false;
	}
	return true;
}

bool LibArchive::Open(std::span<const std::byte> data) {
	Close();
	m_Copy.assign(data.begin(), data.end());
	m_Data = m_Copy;
	if (!Parse()) {
		Close();
		return false;
	}
	return true;
}

void LibArchive::Close() {
	m_Members.clear();
	m_Symbols.clear();
	m_Imports.clear();
	m_Data = {};
	m_Map.reset();
	m_Copy.clear();
	m_Path.clear();
}

std::span<const std::byte> LibArchive::MemberData(size_t index) const {
	if (index >= m_Members.size())
		return {};
	auto const& m = m_Members[index];
	if (m.DataOffset > m_Data.size())
		return {};
	return m_Data.subspan((size_t)m.DataOffset, (size_t)std::min<uint64_t>(m.Size, m_Data.size() - m.DataOffset));
}

bool LibArchive::Parse() {
	if (!IsArchive(m_Data))
		return false;

	// the raw names, as the headers have them: the long ones are resolved when the table of them is known
	std::vector<std::string> raw;
	uint64_t pos = SignatureSize;
	while (pos + HeaderSize <= m_Data.size()) {
		auto h = reinterpret_cast<const char*>(m_Data.data() + pos);
		if (h[58] != '`' || h[59] != '\n')
			break;		// not a header: the rest of the file is damaged
		uint64_t size, date = 0, mode = 0;
		if (!ParseNumber(h + 48, 10, 10, size))
			break;
		ParseNumber(h + 16, 12, 10, date);
		ParseNumber(h + 40, 8, 8, mode);

		ArchiveMember m;
		m.HeaderOffset = pos;
		m.DataOffset = pos + HeaderSize;
		m.Size = size;
		m.Date = (uint32_t)date;
		m.Mode = (uint32_t)mode;
		if (m.DataOffset + m.Size > m_Data.size())
			m.Size = m_Data.size() - m.DataOffset;		// truncated file: what is there

		auto name = TrimRight(h, 16);
		// BSD style: the name is at the start of the data
		uint64_t bsdLength;
		if (name.starts_with("#1/") && ParseNumber(name.c_str() + 3, name.size() - 3, 10, bsdLength) && bsdLength <= m.Size) {
			name = TrimRight(reinterpret_cast<const char*>(m_Data.data() + m.DataOffset), (size_t)bsdLength);
			m.DataOffset += bsdLength;
			m.Size -= bsdLength;
			raw.push_back(std::move(name));
			m.Name = raw.back();
		}
		else {
			raw.push_back(name);
		}
		m_Members.push_back(std::move(m));

		pos += HeaderSize + size;
		pos += pos & 1;
	}
	if (m_Members.empty())
		return !m_Data.empty() && m_Data.size() == SignatureSize;	// an empty archive is valid

	// classify the members and find the special ones
	int linkers = 0;
	int firstLinker = -1, secondLinker = -1, longNames = -1;
	for (int i = 0; i < (int)m_Members.size(); i++) {
		auto& m = m_Members[i];
		auto const& name = raw[i];
		if (name == "/" && linkers < 2 && m.Name.empty()) {
			m.Kind = ArchiveMemberKind::LinkerMember;
			m.Name = linkers == 0 ? "First Linker Member" : "Second Linker Member";
			(linkers == 0 ? firstLinker : secondLinker) = i;
			linkers++;
		}
		else if (name == "//" && longNames < 0) {
			m.Kind = ArchiveMemberKind::LongNames;
			m.Name = "Longnames";
			longNames = i;
		}
		else if (name == "/SYM64/" || (name == "/" && m.Name.empty())) {
			m.Kind = ArchiveMemberKind::LinkerMember;
			m.Name = name;
		}
	}

	auto longNamesData = longNames >= 0 ? MemberData(longNames) : std::span<const std::byte>();
	for (int i = 0; i < (int)m_Members.size(); i++) {
		auto& m = m_Members[i];
		if (m.Kind != ArchiveMemberKind::Other || !m.Name.empty())
			continue;
		auto name = raw[i];
		uint64_t offset;
		if (name.size() > 1 && name[0] == '/' && ParseNumber(name.c_str() + 1, name.size() - 1, 10, offset)) {
			if (offset < longNamesData.size()) {
				// the names are separated by "/\n"
				auto p = reinterpret_cast<const char*>(longNamesData.data());
				size_t end = (size_t)offset;
				while (end < longNamesData.size() && p[end] != '\n' && p[end] != '\0')
					end++;
				name.assign(p + offset, end - offset);
			}
		}
		if (name.size() > 1 && name.back() == '/')
			name.pop_back();
		m.Name = name;
	}

	// what the regular members are
	for (int i = 0; i < (int)m_Members.size(); i++) {
		auto& m = m_Members[i];
		if (m.Kind != ArchiveMemberKind::Other)
			continue;
		auto data = MemberData(i);
		ImportHeader ih;
		if (!ReadAt(data, 0, ih))
			continue;
		if (ih.Sig1 == 0 && ih.Sig2 == 0xFFFF) {
			if (ih.Version == 0 && ih.SizeOfData <= data.size() - sizeof(ih)) {
				m.Kind = ArchiveMemberKind::ImportRecord;
				m.Machine = ih.Machine;
				ArchiveImport imp;
				imp.Member = i;
				imp.Machine = ih.Machine;
				imp.TimeDateStamp = ih.TimeDateStamp;
				imp.OrdinalOrHint = ih.OrdinalOrHint;
				auto type = ih.Flags & 3, nameType = (ih.Flags >> 2) & 7;
				imp.Type = type <= 2 ? (ImportType)type : ImportType::Unknown;
				imp.NameType = nameType <= 3 ? (ImportNameType)nameType : ImportNameType::Unknown;
				size_t p = sizeof(ih);
				imp.Symbol = ReadCString(data.first(sizeof(ih) + ih.SizeOfData), p);
				imp.Dll = ReadCString(data.first(sizeof(ih) + ih.SizeOfData), p);
				m_Imports.push_back(std::move(imp));
			}
			else if (ih.Version >= 2) {
				BigObjectHeader bh;
				if (ReadAt(data, 0, bh)) {
					m.Kind = ArchiveMemberKind::Object;
					m.BigObj = true;
					m.Machine = bh.Machine;
					m.Sections = bh.Sections;
					m.Symbols = bh.Symbols;
				}
			}
			continue;
		}
		ObjectHeader oh;
		// Machine 0 is an object that is for any machine (the ones that only have debug information are like that): demand more of those
		if (ReadAt(data, 0, oh) && oh.OptionalHeader < 0x1000 && oh.SymbolTable <= data.size() &&
			(KnownMachine(oh.Machine) || (oh.Machine == 0 && oh.Sections > 0 && oh.Sections < 0x8000 && oh.Symbols < data.size() &&
				oh.SymbolTable + (uint64_t)oh.Symbols * 18 <= data.size()))) {
			m.Kind = ArchiveMemberKind::Object;
			m.Machine = oh.Machine;
			m.Sections = oh.Sections;
			m.Symbols = oh.Symbols;
		}
	}

	if (secondLinker >= 0 || firstLinker >= 0)
		ReadSymbolIndex(firstLinker >= 0 ? firstLinker : 0, secondLinker);
	return true;
}

// The symbols with the member that has each of them. The second linker member has them sorted and with the number of the member
// (in the table of the offsets of the members); the first has the offset of the member of each symbol.
void LibArchive::ReadSymbolIndex(size_t first, int second) {
	auto memberAt = [&](uint64_t headerOffset) {
		auto it = std::lower_bound(m_Members.begin(), m_Members.end(), headerOffset,
			[](ArchiveMember const& m, uint64_t offset) { return m.HeaderOffset < offset; });
		return it != m_Members.end() && it->HeaderOffset == headerOffset ? (int)(it - m_Members.begin()) : -1;
	};

	if (second >= 0) {
		auto data = MemberData(second);
		uint32_t count = 0, symbols = 0;
		if (!ReadAt(data, 0, count) || count > data.size() / 4)
			return;
		size_t pos = 4 + (size_t)count * 4;
		if (!ReadAt(data, pos, symbols) || symbols > data.size() / 2)
			return;
		size_t indices = pos + 4;
		size_t names = indices + (size_t)symbols * 2;
		if (names > data.size())
			return;
		m_Symbols.reserve(symbols);
		for (uint32_t i = 0; i < symbols; i++) {
			uint16_t number = 0;
			ReadAt(data, indices + (size_t)i * 2, number);
			ArchiveSymbol sym;
			uint32_t offset = 0;
			if (number >= 1 && number <= count && ReadAt(data, 4 + (size_t)(number - 1) * 4, offset))
				sym.Member = memberAt(offset);
			if (names >= data.size())
				break;
			sym.Name = ReadCString(data, names);
			m_Symbols.push_back(std::move(sym));
		}
		return;
	}

	auto data = MemberData(first);
	uint32_t count = 0;
	if (!ReadAt(data, 0, count))
		return;
	count = FromBigEndian(count);
	if (count > data.size() / 4)
		return;
	size_t names = 4 + (size_t)count * 4;
	m_Symbols.reserve(count);
	for (uint32_t i = 0; i < count && names < data.size(); i++) {
		uint32_t offset = 0;
		ReadAt(data, 4 + (size_t)i * 4, offset);
		ArchiveSymbol sym;
		sym.Member = memberAt(FromBigEndian(offset));
		sym.Name = ReadCString(data, names);
		m_Symbols.push_back(std::move(sym));
	}
}

std::wstring_view LibArchive::MachineName(uint16_t machine) {
	switch (machine) {
		case 0x14c: return L"x86";
		case 0x8664: return L"x64";
		case 0x1c0: return L"ARM";
		case 0x1c2: return L"ARM Thumb";
		case 0x1c4: return L"ARMNT";
		case 0xaa64: return L"ARM64";
		case 0xa641: return L"ARM64EC";
		case 0xa64e: return L"ARM64X";
		case 0x200: return L"IA64";
		case 0x1f0: return L"PowerPC";
		case 0x1f1: return L"PowerPC FP";
		case 0x366: return L"MIPS";
		case 0x466: return L"MIPS16";
		case 0x5032: return L"RISC-V 32";
		case 0x5064: return L"RISC-V 64";
		case 0x6232: return L"LoongArch 32";
		case 0x6264: return L"LoongArch 64";
		case 0xebc: return L"EBC";
	}
	return L"";
}

std::wstring_view LibArchive::KindName(ArchiveMemberKind kind) {
	switch (kind) {
		case ArchiveMemberKind::Object: return L"Object";
		case ArchiveMemberKind::ImportRecord: return L"Import";
		case ArchiveMemberKind::LinkerMember: return L"Linker Member";
		case ArchiveMemberKind::LongNames: return L"Long Names";
	}
	return L"Other";
}
