#include "pch.h"
#include "EventManifest.h"
#include <unordered_map>

// The layout, as the message compiler writes it (offsets are from the start of the resource):
//   CRIM   signature, size, major version (16 bits), minor version (16 bits), number of providers,
//          then for each provider: GUID, offset of its WEVT
//   WEVT   signature, size, message, number of elements, then for each element: (unknown, offset)
//   the elements start with their signature, size and number of definitions:
//   CHAN   16 bytes each: flags, name offset, value, message
//   LEVL   12 bytes each: value, message, name offset
//   OPCO   12 bytes each: task (16 bits), value (16 bits), message, name offset
//   TASK   28 bytes each: value, message, GUID, name offset
//   KEYW   16 bytes each: mask (64 bits), message, name offset
//   MAPS   offsets of the maps; VMAP / BMAP: signature, size, name offset, flags, number of entries, (value, message) each
//   TTBL   TEMP one after the other: signature, size, number of fields, number of names, offset of the fields, event type,
//          GUID, the BinXml of the event, then 20 bytes each field: flags, in type (8 bits), out type (8 bits), unknown
//          (16 bits), map offset, count (16 bits), length (16 bits), name offset
//   EVNT   (an unknown 32 bits before the events) 48 bytes each: id (16 bits), version, channel, level, opcode (8 bits each),
//          task (16 bits), keywords (64 bits), message, template offset, opcode offset, level offset, task offset,
//          number of keywords, offset of the keyword offsets, channel offset
//   PRVA   number of attributes, then (type, offset) each: 1 is the name of the provider, a string that ends with a 0
//   names (but those of the provider attributes) are a size (that includes the size itself) and UTF-16 characters

namespace {
	class Reader {
	public:
		explicit Reader(std::span<const std::byte> data) : m_Data(data) {}

		size_t Size() const { return m_Data.size(); }
		bool Has(size_t offset, size_t size) const { return offset <= m_Data.size() && size <= m_Data.size() - offset; }

		template<typename T>
		T Get(size_t offset) const {
			T value{};
			if (Has(offset, sizeof(T)))
				memcpy(&value, m_Data.data() + offset, sizeof(T));
			return value;
		}

		bool Signature(size_t offset, const char* sig) const {
			return Has(offset, 4) && memcmp(m_Data.data() + offset, sig, 4) == 0;
		}

		// a size and the characters
		std::wstring Name(uint32_t offset) const {
			auto size = Get<uint32_t>(offset);
			if (offset == 0 || size < 4 || !Has(offset, size))
				return {};
			std::wstring name((size - 4) / 2, L'\0');
			memcpy(name.data(), m_Data.data() + offset + 4, name.size() * 2);
			while (!name.empty() && name.back() == L'\0')
				name.pop_back();
			return name;
		}

		// characters that end with a 0
		std::wstring String(uint32_t offset) const {
			std::wstring text;
			for (size_t i = offset; Has(i, 2) && text.size() < 1024; i += 2) {
				auto c = Get<wchar_t>(i);
				if (c == 0)
					break;
				text += c;
			}
			return text;
		}

		std::string Guid(size_t offset) const {
			if (!Has(offset, 16))
				return {};
			auto b = [&](int i) { return Get<uint8_t>(offset + i); };
			bool zero = true;
			for (int i = 0; i < 16; i++)
				zero &= b(i) == 0;
			if (zero)
				return {};
			return std::format("{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}", Get<uint32_t>(offset),
				Get<uint16_t>(offset + 4), Get<uint16_t>(offset + 6), b(8), b(9), b(10), b(11), b(12), b(13), b(14), b(15));
		}

	private:
		std::span<const std::byte> m_Data;
	};

	constexpr size_t MaxCount = 1 << 20;

	// the number of definitions of an element that fit in it
	uint32_t Count(Reader const& r, uint32_t element, size_t header, size_t each) {
		auto count = r.Get<uint32_t>(element + 8);
		if (count > MaxCount || !r.Has(element + header, (size_t)count * each))
			return 0;
		return count;
	}

	// where the definitions of the provider are, so that the events can refer to them
	struct Offsets {
		std::unordered_map<uint32_t, int> Channels, Levels, Tasks, Opcodes, Keywords, Maps, Templates;

		static int Find(std::unordered_map<uint32_t, int> const& map, uint32_t offset) {
			auto it = map.find(offset);
			return it == map.end() ? -1 : it->second;
		}
	};

	void ReadElement(Reader const& r, uint32_t e, EventProvider& p, Offsets& at) {
		if (r.Signature(e, "CHAN")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 16); i < n; i++) {
				auto d = e + 12 + i * 16;
				at.Channels[d] = (int)p.Channels.size();
				p.Channels.push_back({ r.Get<uint32_t>(d + 8), r.Name(r.Get<uint32_t>(d + 4)), r.Get<uint32_t>(d), r.Get<uint32_t>(d + 12) });
			}
		}
		else if (r.Signature(e, "LEVL")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 12); i < n; i++) {
				auto d = e + 12 + i * 12;
				at.Levels[d] = (int)p.Levels.size();
				p.Levels.push_back({ r.Get<uint32_t>(d), r.Name(r.Get<uint32_t>(d + 8)), r.Get<uint32_t>(d + 4) });
			}
		}
		else if (r.Signature(e, "OPCO")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 12); i < n; i++) {
				auto d = e + 12 + i * 12;
				at.Opcodes[d] = (int)p.Opcodes.size();
				p.Opcodes.push_back({ r.Get<uint16_t>(d + 2), r.Get<uint16_t>(d), r.Name(r.Get<uint32_t>(d + 8)), r.Get<uint32_t>(d + 4) });
			}
		}
		else if (r.Signature(e, "TASK")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 28); i < n; i++) {
				auto d = e + 12 + i * 28;
				at.Tasks[d] = (int)p.Tasks.size();
				p.Tasks.push_back({ r.Get<uint32_t>(d), r.Name(r.Get<uint32_t>(d + 24)), r.Guid(d + 8), r.Get<uint32_t>(d + 4) });
			}
		}
		else if (r.Signature(e, "KEYW")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 16); i < n; i++) {
				auto d = e + 12 + i * 16;
				at.Keywords[d] = (int)p.Keywords.size();
				p.Keywords.push_back({ r.Get<uint64_t>(d), r.Name(r.Get<uint32_t>(d + 12)), r.Get<uint32_t>(d + 8) });
			}
		}
		else if (r.Signature(e, "MAPS")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 4); i < n; i++) {
				auto m = r.Get<uint32_t>(e + 12 + i * 4);
				bool bitmap = r.Signature(m, "BMAP");
				if (!bitmap && !r.Signature(m, "VMAP"))
					continue;
				EventMap map;
				map.Name = r.Name(r.Get<uint32_t>(m + 8));
				map.Bitmap = bitmap;
				auto entries = r.Get<uint32_t>(m + 16);
				if (entries > MaxCount || !r.Has(m + 20, (size_t)entries * 8))
					continue;
				for (uint32_t j = 0; j < entries; j++)
					map.Entries.push_back({ r.Get<uint32_t>(m + 20 + j * 8), r.Get<uint32_t>(m + 24 + j * 8) });
				at.Maps[m] = (int)p.Maps.size();
				p.Maps.push_back(std::move(map));
			}
		}
		else if (r.Signature(e, "TTBL")) {
			auto end = (size_t)e + r.Get<uint32_t>(e + 4);
			size_t t = e + 12;
			for (uint32_t i = 0, n = r.Get<uint32_t>(e + 8); i < n && i < MaxCount && r.Signature(t, "TEMP"); i++) {
				auto size = r.Get<uint32_t>(t + 4);
				if (size < 40 || t + size > end)
					break;
				EventTemplate temp;
				temp.Guid = r.Guid(t + 24);
				auto fields = r.Get<uint32_t>(t + 8);
				auto items = r.Get<uint32_t>(t + 16);
				if (items >= t + 40 && items <= t + size)
					temp.XmlSize = (uint32_t)(items - (t + 40));
				if (fields <= MaxCount && r.Has(items, (size_t)fields * 20)) {
					for (uint32_t f = 0; f < fields; f++) {
						auto d = items + f * 20;
						EventField field;
						field.Flags = r.Get<uint32_t>(d);
						field.InType = r.Get<uint8_t>(d + 4);
						field.OutType = r.Get<uint8_t>(d + 5);
						field.Map = r.Get<uint32_t>(d + 8);	// an offset for now, an index once the maps are read
						field.Count = r.Get<uint16_t>(d + 12);
						field.Length = r.Get<uint16_t>(d + 14);
						field.Name = r.Name(r.Get<uint32_t>(d + 16));
						temp.Fields.push_back(std::move(field));
					}
				}
				at.Templates[(uint32_t)t] = (int)p.Templates.size();
				p.Templates.push_back(std::move(temp));
				t += size;
			}
		}
		else if (r.Signature(e, "EVNT")) {
			for (uint32_t i = 0, n = Count(r, e, 16, 48); i < n; i++) {
				auto d = e + 16 + i * 48;
				EventDefinition ev;
				ev.Id = r.Get<uint16_t>(d);
				ev.Version = r.Get<uint8_t>(d + 2);
				ev.Channel = r.Get<uint8_t>(d + 3);
				ev.Level = r.Get<uint8_t>(d + 4);
				ev.Opcode = r.Get<uint8_t>(d + 5);
				ev.Task = r.Get<uint16_t>(d + 6);
				ev.Keywords = r.Get<uint64_t>(d + 8);
				ev.MessageId = r.Get<uint32_t>(d + 16);
				// the offsets of the definitions: indices once all the elements are read
				ev.Template = r.Get<uint32_t>(d + 20);
				ev.OpcodeIndex = r.Get<uint32_t>(d + 24);
				ev.LevelIndex = r.Get<uint32_t>(d + 28);
				ev.TaskIndex = r.Get<uint32_t>(d + 32);
				auto keywords = r.Get<uint32_t>(d + 36), list = r.Get<uint32_t>(d + 40);
				if (keywords < 64 && r.Has(list, keywords * 4))
					for (uint32_t k = 0; k < keywords; k++)
						ev.KeywordIndices.push_back(r.Get<uint32_t>(list + k * 4));
				ev.ChannelIndex = r.Get<uint32_t>(d + 44);
				p.Events.push_back(std::move(ev));
			}
		}
		else if (r.Signature(e, "PRVA")) {
			for (uint32_t i = 0, n = Count(r, e, 12, 8); i < n; i++) {
				auto d = e + 12 + i * 8;
				if ((r.Get<uint32_t>(d) & 0xFFFF) == 1 && p.Name.empty())
					p.Name = r.String(r.Get<uint32_t>(d + 4));
			}
		}
	}
}

std::optional<EventManifest> ParseEventManifest(std::span<const std::byte> data) {
	Reader r(data);
	if (!r.Signature(0, "CRIM") || r.Size() < 16)
		return std::nullopt;
	EventManifest manifest;
	manifest.MajorVersion = r.Get<uint16_t>(8);
	manifest.MinorVersion = r.Get<uint16_t>(10);
	auto providers = r.Get<uint32_t>(12);
	if (providers > 4096 || !r.Has(16, (size_t)providers * 20))
		return std::nullopt;

	for (uint32_t i = 0; i < providers; i++) {
		EventProvider p;
		p.Guid = r.Guid(16 + i * 20);
		auto w = r.Get<uint32_t>(32 + i * 20);
		if (r.Signature(w, "WEVT")) {
			p.MessageId = r.Get<uint32_t>(w + 8);
			auto elements = r.Get<uint32_t>(w + 12);
			Offsets at;
			for (uint32_t e = 0; e < elements && e < 64 && r.Has(w + 20 + e * 8, 4); e++)
				ReadElement(r, r.Get<uint32_t>(w + 20 + e * 8), p, at);

			// from offsets to indices
			for (auto& t : p.Templates)
				for (auto& f : t.Fields)
					f.Map = f.Map ? Offsets::Find(at.Maps, (uint32_t)f.Map) : -1;
			for (auto& ev : p.Events) {
				auto find = [](auto const& map, int offset) { return offset ? Offsets::Find(map, (uint32_t)offset) : -1; };
				ev.Template = find(at.Templates, ev.Template);
				ev.OpcodeIndex = find(at.Opcodes, ev.OpcodeIndex);
				ev.LevelIndex = find(at.Levels, ev.LevelIndex);
				ev.TaskIndex = find(at.Tasks, ev.TaskIndex);
				ev.ChannelIndex = find(at.Channels, ev.ChannelIndex);
				std::vector<int> keywords;
				for (auto k : ev.KeywordIndices)
					if (auto index = find(at.Keywords, k); index >= 0)
						keywords.push_back(index);
				ev.KeywordIndices = std::move(keywords);
			}
		}
		manifest.Providers.push_back(std::move(p));
	}
	return manifest;
}

const wchar_t* EventInTypeName(uint8_t type) {
	static const wchar_t* const names[] = {
		L"win:Null", L"win:UnicodeString", L"win:AnsiString", L"win:Int8", L"win:UInt8", L"win:Int16", L"win:UInt16",
		L"win:Int32", L"win:UInt32", L"win:Int64", L"win:UInt64", L"win:Float", L"win:Double", L"win:Boolean", L"win:Binary",
		L"win:GUID", L"win:Pointer", L"win:FILETIME", L"win:SYSTEMTIME", L"win:SID", L"win:HexInt32", L"win:HexInt64",
		L"win:CountedString", L"win:CountedAnsiString", L"win:ReversedCountedString", L"win:ReversedCountedAnsiString",
		L"win:NonNullTerminatedString", L"win:NonNullTerminatedAnsiString", L"win:UnicodeChar", L"win:AnsiChar",
		L"win:SizeT", L"win:HexDump", L"win:WbemSID",
	};
	return type < _countof(names) ? names[type] : L"";
}

const wchar_t* EventOutTypeName(uint8_t type) {
	static const wchar_t* const names[] = {
		L"", L"xs:string", L"xs:dateTime", L"xs:byte", L"xs:unsignedByte", L"xs:short", L"xs:unsignedShort", L"xs:int",
		L"xs:unsignedInt", L"xs:long", L"xs:unsignedLong", L"xs:float", L"xs:double", L"xs:boolean", L"xs:GUID",
		L"xs:hexBinary", L"win:HexInt8", L"win:HexInt16", L"win:HexInt32", L"win:HexInt64", L"win:PID", L"win:TID",
		L"win:Port", L"win:IPv4", L"win:IPv6", L"win:SocketAddress", L"win:CIMDateTime", L"win:ETWTIME", L"win:Xml",
		L"win:ErrorCode", L"win:Win32Error", L"win:NTSTATUS", L"win:HResult", L"win:DateTimeCultureInsensitive", L"win:Json",
		L"win:Utf8", L"win:Pkcs7WithTypeInfo", L"win:CodePointer", L"win:DateTimeUtc",
	};
	return type < _countof(names) ? names[type] : L"";
}
