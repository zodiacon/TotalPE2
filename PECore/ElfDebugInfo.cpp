#include "pch.h"
#include "ElfDebugInfo.h"
#include "ElfFile.h"
#include <filesystem>
#include <map>
#include <zlib.h>

namespace {
	// DWARF constants
	enum : uint32_t {
		DW_TAG_compile_unit = 0x11, DW_TAG_subprogram = 0x2E, DW_TAG_partial_unit = 0x3C, DW_TAG_skeleton_unit = 0x4A,

		DW_AT_name = 0x03, DW_AT_low_pc = 0x11, DW_AT_high_pc = 0x12, DW_AT_language = 0x13, DW_AT_comp_dir = 0x1B,
		DW_AT_producer = 0x25, DW_AT_abstract_origin = 0x31, DW_AT_decl_line = 0x3B, DW_AT_external = 0x3F,
		DW_AT_specification = 0x47, DW_AT_linkage_name = 0x6E, DW_AT_str_offsets_base = 0x72, DW_AT_addr_base = 0x73,
		DW_AT_MIPS_linkage_name = 0x2007, DW_AT_GNU_addr_base = 0x2133,

		DW_FORM_addr = 0x01, DW_FORM_block2 = 0x03, DW_FORM_block4 = 0x04, DW_FORM_data2 = 0x05, DW_FORM_data4 = 0x06,
		DW_FORM_data8 = 0x07, DW_FORM_string = 0x08, DW_FORM_block = 0x09, DW_FORM_block1 = 0x0A, DW_FORM_data1 = 0x0B,
		DW_FORM_flag = 0x0C, DW_FORM_sdata = 0x0D, DW_FORM_strp = 0x0E, DW_FORM_udata = 0x0F, DW_FORM_ref_addr = 0x10,
		DW_FORM_ref1 = 0x11, DW_FORM_ref2 = 0x12, DW_FORM_ref4 = 0x13, DW_FORM_ref8 = 0x14, DW_FORM_ref_udata = 0x15,
		DW_FORM_indirect = 0x16, DW_FORM_sec_offset = 0x17, DW_FORM_exprloc = 0x18, DW_FORM_flag_present = 0x19,
		DW_FORM_strx = 0x1A, DW_FORM_addrx = 0x1B, DW_FORM_ref_sup4 = 0x1C, DW_FORM_strp_sup = 0x1D, DW_FORM_data16 = 0x1E,
		DW_FORM_line_strp = 0x1F, DW_FORM_ref_sig8 = 0x20, DW_FORM_implicit_const = 0x21, DW_FORM_loclistx = 0x22,
		DW_FORM_rnglistx = 0x23, DW_FORM_ref_sup8 = 0x24, DW_FORM_strx1 = 0x25, DW_FORM_strx2 = 0x26, DW_FORM_strx3 = 0x27,
		DW_FORM_strx4 = 0x28, DW_FORM_addrx1 = 0x29, DW_FORM_addrx2 = 0x2A, DW_FORM_addrx3 = 0x2B, DW_FORM_addrx4 = 0x2C,
		DW_FORM_GNU_addr_index = 0x1F01, DW_FORM_GNU_str_index = 0x1F02, DW_FORM_GNU_ref_alt = 0x1F20, DW_FORM_GNU_strp_alt = 0x1F21,
	};

	constexpr uint64_t SHF_COMPRESSED = 0x800;

	std::wstring Widen(std::string const& s) {
		return std::wstring(s.begin(), s.end());
	}

	// reads the data of a section, in the byte order of the file; reading past the end gives zeros and sets Bad
	struct Reader {
		std::span<const std::byte> Data;
		bool Big{ false };
		size_t Pos{ 0 };
		bool Bad{ false };

		bool Has(size_t n) const { return Pos <= Data.size() && Data.size() - Pos >= n; }

		uint64_t Unsigned(size_t n) {
			if (!Has(n)) {
				Bad = true;
				Pos = Data.size();
				return 0;
			}
			uint64_t value = 0;
			for (size_t i = 0; i < n; i++) {
				auto b = std::to_integer<uint64_t>(Data[Pos + i]);
				value |= Big ? b << ((n - 1 - i) * 8) : b << (i * 8);
			}
			Pos += n;
			return value;
		}
		uint8_t U8() { return (uint8_t)Unsigned(1); }
		uint16_t U16() { return (uint16_t)Unsigned(2); }
		uint32_t U32() { return (uint32_t)Unsigned(4); }
		uint64_t U64() { return Unsigned(8); }

		uint64_t Uleb() {
			uint64_t value = 0;
			for (int shift = 0; ; shift += 7) {
				if (!Has(1)) {
					Bad = true;
					return value;
				}
				auto b = std::to_integer<uint8_t>(Data[Pos++]);
				if (shift < 64)
					value |= (uint64_t)(b & 0x7F) << shift;
				if (!(b & 0x80))
					return value;
			}
		}

		int64_t Sleb() {
			int64_t value = 0;
			int shift = 0;
			uint8_t b;
			do {
				if (!Has(1)) {
					Bad = true;
					return value;
				}
				b = std::to_integer<uint8_t>(Data[Pos++]);
				if (shift < 64)
					value |= (int64_t)(b & 0x7F) << shift;
				shift += 7;
			} while (b & 0x80);
			if (shift < 64 && (b & 0x40))
				value |= -((int64_t)1 << shift);
			return value;
		}

		std::string String() {
			size_t start = Pos;
			while (Pos < Data.size() && Data[Pos] != std::byte{ 0 })
				Pos++;
			std::string s((const char*)Data.data() + start, Pos - start);
			if (Pos < Data.size())
				Pos++;
			else
				Bad = true;
			return s;
		}

		void Skip(uint64_t n) {
			if (!Has((size_t)std::min<uint64_t>(n, SIZE_MAX)))
				Bad = true, Pos = Data.size();
			else
				Pos += (size_t)n;
		}
	};

	std::string StringAt(std::span<const std::byte> data, uint64_t offset) {
		if (offset >= data.size())
			return {};
		auto p = (const char*)data.data() + offset;
		return std::string(p, strnlen(p, data.size() - (size_t)offset));
	}

	struct AttributeSpec {
		uint32_t Name, Form;
		int64_t Implicit;
	};

	struct Abbreviation {
		uint32_t Tag{};
		bool Children{};
		std::vector<AttributeSpec> Attributes;
	};

	using AbbreviationTable = std::map<uint64_t, Abbreviation>;

	// what an attribute is: a constant, an address, a string... or something to resolve once the bases of the unit are known
	struct Value {
		enum Kind { None, Constant, Address, String, StringIndex, AddressIndex, Reference, Flag } Type{ None };
		uint64_t U{};
		std::string Text;
		uint32_t Form{};
	};

	class DwarfReader {
	public:
		DwarfReader(ElfFile const& elf, DwarfInfo& info) : m_Elf(elf), m_Info(info) {}

		void Read() {
			m_DebugInfo = Section("info");
			if (m_DebugInfo.empty())
				return;
			m_Abbrev = Section("abbrev");
			m_Str = Section("str");
			m_LineStr = Section("line_str");
			m_StrOffsets = Section("str_offsets");
			m_Addr = Section("addr");
			ReadUnits();
			ResolveNames();
			std::ranges::sort(m_Info.Functions, {}, &DwarfFunction::LowPc);
		}

	private:
		// a .debug_ (or .zdebug_) section, uncompressed
		std::vector<std::byte> Section(char const* name) {
			auto const& sections = m_Elf.Sections();
			for (size_t i = 0; i < sections.size(); i++) {
				auto const& s = sections[i];
				bool z = s.Name == std::string(".zdebug_") + name;
				if (s.Name != std::string(".debug_") + name && !z)
					continue;
				auto data = m_Elf.SectionData(i);
				DwarfSection info{ s.Name, data.size(), data.size() };
				std::vector<std::byte> result;
				if (s.Flags & SHF_COMPRESSED) {
					// Elf32_Chdr / Elf64_Chdr: the type, the size of the data, its alignment
					Reader r{ data, m_Elf.BigEndian() };
					uint32_t type = r.U32();
					if (m_Elf.Is64())
						r.U32();
					uint64_t size = m_Elf.Is64() ? r.U64() : r.U32();
					r.Skip(m_Elf.Is64() ? 8 : 4);
					info.UncompressedSize = size;
					if (type == 1) {
						info.Compression = L"zlib";
						result = Inflate(data.subspan(std::min(r.Pos, data.size())), size, s.Name);
					}
					else
						m_Info.Problems.push_back(std::format(L"{} is compressed with {}, which is not supported", Widen(s.Name),
							type == 2 ? L"zstd" : std::format(L"method {}", type)));
				}
				else if (z) {
					// "ZLIB", the size in big endian, the zlib stream
					Reader r{ data, true };
					r.Skip(4);
					uint64_t size = r.U64();
					info.UncompressedSize = size;
					info.Compression = L"zlib";
					if (data.size() >= 12 && memcmp(data.data(), "ZLIB", 4) == 0)
						result = Inflate(data.subspan(12), size, s.Name);
				}
				else
					result.assign(data.begin(), data.end());
				m_Info.Sections.push_back(std::move(info));
				return result;
			}
			return {};
		}

		std::vector<std::byte> Inflate(std::span<const std::byte> data, uint64_t size, std::string const& name) {
			if (size > 1ull << 31) {
				m_Info.Problems.push_back(std::format(L"{} is too large to decompress", Widen(name)));
				return {};
			}
			std::vector<std::byte> out((size_t)size);
			uLongf outSize = (uLongf)size;
			if (uncompress((Bytef*)out.data(), &outSize, (Bytef const*)data.data(), (uLong)data.size()) != Z_OK) {
				m_Info.Problems.push_back(std::format(L"{} could not be decompressed", Widen(name)));
				return {};
			}
			out.resize(outSize);
			return out;
		}

		AbbreviationTable const& Abbreviations(uint64_t offset) {
			auto it = m_Tables.find(offset);
			if (it != m_Tables.end())
				return it->second;
			auto& table = m_Tables[offset];
			Reader r{ m_Abbrev, m_Elf.BigEndian(), (size_t)std::min<uint64_t>(offset, m_Abbrev.size()) };
			while (!r.Bad) {
				auto code = r.Uleb();
				if (code == 0)
					break;
				Abbreviation a;
				a.Tag = (uint32_t)r.Uleb();
				a.Children = r.U8() != 0;
				while (!r.Bad) {
					auto name = (uint32_t)r.Uleb(), form = (uint32_t)r.Uleb();
					if (name == 0 && form == 0)
						break;
					int64_t implicit = form == DW_FORM_implicit_const ? r.Sleb() : 0;
					a.Attributes.push_back({ name, form, implicit });
				}
				table[code] = std::move(a);
			}
			return table;
		}

		struct Unit {
			uint64_t Start{}, End{};
			uint16_t Version{};
			uint8_t OffsetSize{ 4 }, AddressSize{ 8 };
			uint64_t StrOffsetsBase{}, AddrBase{};
			bool HasStrOffsetsBase{}, HasAddrBase{};
		};

		// false for a form that is not known (the rest of the unit cannot be read then)
		bool ReadValue(Reader& r, Unit const& u, uint32_t form, int64_t implicit, Value& v) {
			v.Form = form;
			switch (form) {
				case DW_FORM_addr: v.Type = Value::Address; v.U = r.Unsigned(u.AddressSize); break;
				case DW_FORM_block1: r.Skip(r.U8()); break;
				case DW_FORM_block2: r.Skip(r.U16()); break;
				case DW_FORM_block4: r.Skip(r.U32()); break;
				case DW_FORM_block: case DW_FORM_exprloc: r.Skip(r.Uleb()); break;
				case DW_FORM_data1: v.Type = Value::Constant; v.U = r.U8(); break;
				case DW_FORM_data2: v.Type = Value::Constant; v.U = r.U16(); break;
				case DW_FORM_data4: v.Type = Value::Constant; v.U = r.U32(); break;
				case DW_FORM_data8: v.Type = Value::Constant; v.U = r.U64(); break;
				case DW_FORM_data16: r.Skip(16); break;
				case DW_FORM_sdata: v.Type = Value::Constant; v.U = (uint64_t)r.Sleb(); break;
				case DW_FORM_udata: v.Type = Value::Constant; v.U = r.Uleb(); break;
				case DW_FORM_implicit_const: v.Type = Value::Constant; v.U = (uint64_t)implicit; break;
				case DW_FORM_string: v.Type = Value::String; v.Text = r.String(); break;
				case DW_FORM_strp: v.Type = Value::String; v.Text = StringAt(m_Str, r.Unsigned(u.OffsetSize)); break;
				case DW_FORM_line_strp: v.Type = Value::String; v.Text = StringAt(m_LineStr, r.Unsigned(u.OffsetSize)); break;
				case DW_FORM_strp_sup: case DW_FORM_GNU_strp_alt: r.Unsigned(u.OffsetSize); break;	// in another file
				case DW_FORM_strx: case DW_FORM_GNU_str_index: v.Type = Value::StringIndex; v.U = r.Uleb(); break;
				case DW_FORM_strx1: v.Type = Value::StringIndex; v.U = r.U8(); break;
				case DW_FORM_strx2: v.Type = Value::StringIndex; v.U = r.U16(); break;
				case DW_FORM_strx3: v.Type = Value::StringIndex; v.U = r.Unsigned(3); break;
				case DW_FORM_strx4: v.Type = Value::StringIndex; v.U = r.U32(); break;
				case DW_FORM_addrx: case DW_FORM_GNU_addr_index: v.Type = Value::AddressIndex; v.U = r.Uleb(); break;
				case DW_FORM_addrx1: v.Type = Value::AddressIndex; v.U = r.U8(); break;
				case DW_FORM_addrx2: v.Type = Value::AddressIndex; v.U = r.U16(); break;
				case DW_FORM_addrx3: v.Type = Value::AddressIndex; v.U = r.Unsigned(3); break;
				case DW_FORM_addrx4: v.Type = Value::AddressIndex; v.U = r.U32(); break;
				case DW_FORM_flag: v.Type = Value::Flag; v.U = r.U8(); break;
				case DW_FORM_flag_present: v.Type = Value::Flag; v.U = 1; break;
				// references in the unit are relative to its start
				case DW_FORM_ref1: v.Type = Value::Reference; v.U = u.Start + r.U8(); break;
				case DW_FORM_ref2: v.Type = Value::Reference; v.U = u.Start + r.U16(); break;
				case DW_FORM_ref4: v.Type = Value::Reference; v.U = u.Start + r.U32(); break;
				case DW_FORM_ref8: v.Type = Value::Reference; v.U = u.Start + r.U64(); break;
				case DW_FORM_ref_udata: v.Type = Value::Reference; v.U = u.Start + r.Uleb(); break;
				case DW_FORM_ref_addr: v.Type = Value::Reference; v.U = r.Unsigned(u.Version <= 2 ? u.AddressSize : u.OffsetSize); break;
				case DW_FORM_ref_sig8: r.Skip(8); break;
				case DW_FORM_ref_sup4: r.Skip(4); break;
				case DW_FORM_ref_sup8: r.Skip(8); break;
				case DW_FORM_GNU_ref_alt: r.Unsigned(u.OffsetSize); break;
				case DW_FORM_sec_offset: v.Type = Value::Constant; v.U = r.Unsigned(u.OffsetSize); break;
				case DW_FORM_loclistx: case DW_FORM_rnglistx: r.Uleb(); break;
				case DW_FORM_indirect:
				{
					auto actual = (uint32_t)r.Uleb();
					if (actual == DW_FORM_indirect)
						return false;
					return ReadValue(r, u, actual, actual == DW_FORM_implicit_const ? r.Sleb() : 0, v);
				}
				default:
					return false;
			}
			return !r.Bad;
		}

		// a string or an address that is an index in .debug_str_offsets or .debug_addr
		std::string Text(Value const& v, Unit const& u) {
			if (v.Type == Value::String)
				return v.Text;
			if (v.Type == Value::StringIndex) {
				// GCC's split DWARF (DW_FORM_GNU_str_index) and DWARF 5 without a base start after the header
				uint64_t base = u.HasStrOffsetsBase ? u.StrOffsetsBase : (u.Version >= 5 ? 8 : 0);
				Reader r{ m_StrOffsets, m_Elf.BigEndian(), 0 };
				r.Pos = (size_t)std::min<uint64_t>(base + v.U * u.OffsetSize, m_StrOffsets.size());
				auto offset = r.Unsigned(u.OffsetSize);
				return r.Bad ? std::string() : StringAt(m_Str, offset);
			}
			return {};
		}

		uint64_t Address(Value const& v, Unit const& u) {
			if (v.Type == Value::Address || v.Type == Value::Constant)
				return v.U;
			if (v.Type == Value::AddressIndex) {
				uint64_t base = u.HasAddrBase ? u.AddrBase : (u.Version >= 5 ? 8 : 0);
				Reader r{ m_Addr, m_Elf.BigEndian(), 0 };
				r.Pos = (size_t)std::min<uint64_t>(base + v.U * u.AddressSize, m_Addr.size());
				return r.Unsigned(u.AddressSize);
			}
			return 0;
		}

		void ReadUnits() {
			Reader r{ m_DebugInfo, m_Elf.BigEndian() };
			while (r.Has(4) && m_Info.Units.size() < 1000000) {
				Unit u;
				u.Start = r.Pos;
				uint64_t length = r.U32();
				if (length == 0xFFFFFFFF) {
					length = r.U64();
					u.OffsetSize = 8;
				}
				else if (length >= 0xFFFFFFF0) {
					m_Info.Problems.push_back(std::format(L"The unit at 0x{:X} has a reserved length", u.Start));
					break;
				}
				if (!r.Has((size_t)std::min<uint64_t>(length, SIZE_MAX))) {
					m_Info.Problems.push_back(std::format(L"The unit at 0x{:X} does not fit in .debug_info", u.Start));
					break;
				}
				u.End = r.Pos + length;
				u.Version = r.U16();
				DwarfUnit unit{ .Offset = u.Start, .Version = u.Version, .UnitType = 1 };
				uint64_t abbrevOffset;
				if (u.Version >= 5) {
					unit.UnitType = r.U8();
					u.AddressSize = r.U8();
					abbrevOffset = r.Unsigned(u.OffsetSize);
					if (unit.UnitType == 4 || unit.UnitType == 5)		// skeleton, split compile: the DWO ID
						r.Skip(8);
					else if (unit.UnitType == 2 || unit.UnitType == 6)	// type units: the signature and the type
						r.Skip(8 + u.OffsetSize);
				}
				else {
					abbrevOffset = r.Unsigned(u.OffsetSize);
					u.AddressSize = r.U8();
				}
				unit.AddressSize = u.AddressSize;
				if (u.Version < 2 || u.Version > 5 || (u.AddressSize != 4 && u.AddressSize != 8)) {
					m_Info.Problems.push_back(std::format(L"The unit at 0x{:X} has DWARF version {} and addresses of {} bytes, which are not supported",
						u.Start, u.Version, u.AddressSize));
					r.Pos = (size_t)u.End;
					continue;
				}
				auto index = (uint32_t)m_Info.Units.size();
				m_Info.Units.push_back(unit);
				ReadEntries(r, u, Abbreviations(abbrevOffset), index);
				r.Pos = (size_t)u.End;
			}
		}

		struct PendingFunction {
			uint64_t Specification{}, Origin{};
		};

		void ReadEntries(Reader& r, Unit& u, AbbreviationTable const& table, uint32_t unitIndex) {
			bool first = true;
			std::vector<std::pair<uint32_t, Value>> values;
			while (r.Pos < u.End && !r.Bad) {
				uint64_t offset = r.Pos;
				auto code = r.Uleb();
				if (code == 0)
					continue;	// the end of the children of an entry
				auto it = table.find(code);
				if (it == table.end()) {
					m_Info.Problems.push_back(std::format(L"The entry at 0x{:X} has abbreviation {}, which is not in its table", offset, code));
					return;
				}
				auto const& abbrev = it->second;
				values.clear();
				for (auto const& spec : abbrev.Attributes) {
					Value v;
					if (!ReadValue(r, u, spec.Form, spec.Implicit, v)) {
						m_Info.Problems.push_back(std::format(L"The entry at 0x{:X} has form 0x{:X}, which is not supported", offset, spec.Form));
						return;
					}
					if (v.Type != Value::None)
						values.emplace_back(spec.Name, std::move(v));
				}
				auto find = [&](uint32_t name) -> Value const* {
					for (auto const& [n, v] : values)
						if (n == name)
							return &v;
					return nullptr;
				};

				if (first && (abbrev.Tag == DW_TAG_compile_unit || abbrev.Tag == DW_TAG_partial_unit || abbrev.Tag == DW_TAG_skeleton_unit)) {
					// the bases first: the other attributes of the unit may need them
					if (auto v = find(DW_AT_str_offsets_base))
						u.StrOffsetsBase = v->U, u.HasStrOffsetsBase = true;
					if (auto v = find(DW_AT_addr_base); v || (v = find(DW_AT_GNU_addr_base)))
						u.AddrBase = v->U, u.HasAddrBase = true;
					auto& unit = m_Info.Units[unitIndex];
					if (auto v = find(DW_AT_name))
						unit.Name = Text(*v, u);
					if (auto v = find(DW_AT_producer))
						unit.Producer = Text(*v, u);
					if (auto v = find(DW_AT_comp_dir))
						unit.CompDir = Text(*v, u);
					if (auto v = find(DW_AT_language))
						unit.Language = (uint32_t)v->U;
					if (auto low = find(DW_AT_low_pc)) {
						unit.LowPc = Address(*low, u);
						if (auto high = find(DW_AT_high_pc)) {
							unit.HighPc = high->Type == Value::Constant ? unit.LowPc + high->U : Address(*high, u);
							unit.HasRange = true;
						}
					}
				}
				first = false;

				if (abbrev.Tag == DW_TAG_subprogram) {
					std::string name, linkage;
					if (auto v = find(DW_AT_name))
						name = Text(*v, u);
					if (auto v = find(DW_AT_linkage_name); v || (v = find(DW_AT_MIPS_linkage_name)))
						linkage = Text(*v, u);
					// the declarations that definitions refer to
					if (!name.empty() || !linkage.empty())
						m_Names[offset] = { name, linkage };
					PendingFunction pending;
					if (auto v = find(DW_AT_specification); v && v->Type == Value::Reference)
						pending.Specification = v->U;
					if (auto v = find(DW_AT_abstract_origin); v && v->Type == Value::Reference)
						pending.Origin = v->U;
					if (pending.Specification || pending.Origin)
						m_References[offset] = pending;

					if (auto low = find(DW_AT_low_pc)) {
						DwarfFunction f{ .Name = name, .LinkageName = linkage, .LowPc = Address(*low, u), .Unit = unitIndex };
						f.HighPc = f.LowPc;
						if (auto high = find(DW_AT_high_pc))
							f.HighPc = high->Type == Value::Constant ? f.LowPc + high->U : Address(*high, u);
						if (auto v = find(DW_AT_external))
							f.External = v->U != 0;
						if (auto v = find(DW_AT_decl_line))
							f.Line = (uint32_t)v->U;
						m_FunctionEntries.push_back({ m_Info.Functions.size(), offset });
						m_Info.Functions.push_back(std::move(f));
						m_Info.Units[unitIndex].Functions++;
					}
				}
			}
		}

		// a definition without a name has it in its declaration (DW_AT_specification), or in the inline function it is
		// an instance of (DW_AT_abstract_origin), which may have it in its declaration in turn
		void ResolveNames() {
			for (auto [index, offset] : m_FunctionEntries) {
				auto& f = m_Info.Functions[index];
				auto at = offset;
				for (int hop = 0; hop < 4 && (f.Name.empty() || f.LinkageName.empty()); hop++) {
					auto ref = m_References.find(at);
					if (ref == m_References.end())
						break;
					at = ref->second.Specification ? ref->second.Specification : ref->second.Origin;
					if (auto names = m_Names.find(at); names != m_Names.end()) {
						if (f.Name.empty())
							f.Name = names->second.first;
						if (f.LinkageName.empty())
							f.LinkageName = names->second.second;
					}
				}
			}
		}

		ElfFile const& m_Elf;
		DwarfInfo& m_Info;
		std::vector<std::byte> m_DebugInfo, m_Abbrev, m_Str, m_LineStr, m_StrOffsets, m_Addr;
		std::map<uint64_t, AbbreviationTable> m_Tables;
		std::unordered_map<uint64_t, std::pair<std::string, std::string>> m_Names;
		std::unordered_map<uint64_t, PendingFunction> m_References;
		std::vector<std::pair<size_t, uint64_t>> m_FunctionEntries;	// a function, and the offset of its entry
	};
}

DwarfInfo ReadDwarf(ElfFile const& elf) {
	DwarfInfo info;
	DwarfReader(elf, info).Read();
	return info;
}

ElfDebugLink ReadDebugLink(ElfFile const& elf) {
	ElfDebugLink link;
	auto const& sections = elf.Sections();
	for (size_t i = 0; i < sections.size(); i++) {
		if (sections[i].Name != ".gnu_debuglink")
			continue;
		Reader r{ elf.SectionData(i), elf.BigEndian() };
		link.File = r.String();
		r.Pos = (r.Pos + 3) & ~(size_t)3;
		link.Crc = r.U32();
		link.Present = !r.Bad && !link.File.empty();
		break;
	}
	return link;
}

std::vector<std::wstring> DebugFileCandidates(std::wstring const& path, ElfDebugLink const& link, std::string const& buildId) {
	std::vector<std::wstring> candidates;
	auto slash = path.find_last_of(L"\\/");
	auto dir = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
	auto self = slash == std::wstring::npos ? path : path.substr(slash + 1);
	auto name = Widen(link.File);
	if (link.Present) {
		if (name != self)
			candidates.push_back(dir + name);
		candidates.push_back(dir + L".debug\\" + name);
	}
	// a file of a WSL distribution: \\wsl.localhost\<distro>\ or \\wsl$\<distro>\ is the root of its file system
	std::wstring root;
	for (auto prefix : { L"\\\\wsl.localhost\\", L"\\\\wsl$\\" }) {
		if (_wcsnicmp(path.c_str(), prefix, wcslen(prefix)) == 0) {
			auto end = path.find(L'\\', wcslen(prefix));
			if (end != std::wstring::npos)
				root = path.substr(0, end + 1);
		}
	}
	if (!root.empty()) {
		if (buildId.size() > 2)
			candidates.push_back(root + L"usr\\lib\\debug\\.build-id\\" + Widen(buildId.substr(0, 2)) + L"\\" + Widen(buildId.substr(2)) + L".debug");
		if (link.Present && dir.size() >= root.size())
			candidates.push_back(root + L"usr\\lib\\debug\\" + dir.substr(root.size()) + name);
	}
	return candidates;
}

uint32_t Crc32(std::span<const std::byte> data) {
	uLong crc = crc32(0, nullptr, 0);
	// crc32 takes 32-bit lengths
	while (!data.empty()) {
		auto n = (uInt)std::min<size_t>(data.size(), 1u << 30);
		crc = crc32(crc, (Bytef const*)data.data(), n);
		data = data.subspan(n);
	}
	return (uint32_t)crc;
}

ElfDebugInfo LoadElfDebugInfo(ElfFile const& elf) {
	ElfDebugInfo info;
	info.Link = ReadDebugLink(elf);
	info.Dwarf = ReadDwarf(elf);
	if (!info.Dwarf.Empty())
		return info;
	auto buildId = elf.BuildId();
	if (!info.Link.Present && buildId.empty())
		return info;
	for (auto const& candidate : DebugFileCandidates(elf.Path(), info.Link, buildId)) {
		std::error_code ec;
		if (!std::filesystem::is_regular_file(candidate, ec)) {
			info.Searched.push_back(candidate);
			continue;
		}
		ElfFile file;
		if (!file.Open(candidate))
			continue;
		// it has to be the debug file of this file: the CRC of the link, or the same build ID
		bool matches = info.Link.Present ? Crc32(file.Data()) == info.Link.Crc : file.BuildId() == buildId;
		if (!matches && !buildId.empty())
			matches = file.BuildId() == buildId;
		if (!matches) {
			info.Searched.push_back(candidate + L" (does not match)");
			continue;
		}
		info.Dwarf = ReadDwarf(file);
		info.DebugFile = candidate;
		info.Searched.clear();
		break;
	}
	return info;
}

std::wstring DwarfInfo::LanguageName(uint32_t language) {
	switch (language) {
		case 0x01: return L"C89";
		case 0x02: return L"C";
		case 0x03: return L"Ada 83";
		case 0x04: return L"C++";
		case 0x05: return L"COBOL 74";
		case 0x06: return L"COBOL 85";
		case 0x07: return L"Fortran 77";
		case 0x08: return L"Fortran 90";
		case 0x09: return L"Pascal 83";
		case 0x0A: return L"Modula-2";
		case 0x0B: return L"Java";
		case 0x0C: return L"C99";
		case 0x0D: return L"Ada 95";
		case 0x0E: return L"Fortran 95";
		case 0x0F: return L"PL/I";
		case 0x10: return L"Objective-C";
		case 0x11: return L"Objective-C++";
		case 0x12: return L"UPC";
		case 0x13: return L"D";
		case 0x14: return L"Python";
		case 0x15: return L"OpenCL";
		case 0x16: return L"Go";
		case 0x17: return L"Modula-3";
		case 0x18: return L"Haskell";
		case 0x19: return L"C++03";
		case 0x1A: return L"C++11";
		case 0x1B: return L"OCaml";
		case 0x1C: return L"Rust";
		case 0x1D: return L"C11";
		case 0x1E: return L"Swift";
		case 0x1F: return L"Julia";
		case 0x20: return L"Dylan";
		case 0x21: return L"C++14";
		case 0x22: return L"Fortran 03";
		case 0x23: return L"Fortran 08";
		case 0x24: return L"RenderScript";
		case 0x25: return L"BLISS";
		case 0x26: return L"Kotlin";
		case 0x27: return L"Zig";
		case 0x28: return L"Crystal";
		case 0x2A: return L"C++17";
		case 0x2B: return L"C++20";
		case 0x2C: return L"C17";
		case 0x2D: return L"Fortran 18";
		case 0x2E: return L"Ada 2005";
		case 0x2F: return L"Ada 2012";
		case 0x8001: return L"MIPS Assembler";
		case 0x8E57: return L"GOOGLE RenderScript";
		case 0xB000: return L"Borland Delphi";
	}
	return language ? std::format(L"0x{:X}", language) : L"";
}

std::wstring DwarfInfo::UnitTypeName(uint8_t type) {
	switch (type) {
		case 1: return L"Compile";
		case 2: return L"Type";
		case 3: return L"Partial";
		case 4: return L"Skeleton";
		case 5: return L"Split Compile";
		case 6: return L"Split Type";
	}
	return std::to_wstring(type);
}
