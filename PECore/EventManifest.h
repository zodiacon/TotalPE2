#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The instrumentation manifest of ETW providers, compiled by the message compiler into a WEVT_TEMPLATE resource
// (the "CRIM" format): the providers, and for each its channels, levels, tasks, opcodes, keywords, value maps,
// the templates of the event data and the events. Names are those of the manifest; the texts that are shown
// to users are messages (in the message table of the file or its .mui file), referred to by their identifiers.

constexpr uint32_t NoEventMessage = 0xFFFFFFFF;

struct EventChannel {
	uint32_t Value{ 0 };		// what events refer to: 8 System, 9 Application, 10 Security, 16 and above the provider's own
	std::wstring Name;
	uint32_t Flags{ 0 };		// 1: imported (one of the channels of Windows)
	uint32_t MessageId{ NoEventMessage };
};

struct EventLevel {
	uint32_t Value{ 0 };
	std::wstring Name;
	uint32_t MessageId{ NoEventMessage };
};

struct EventTask {
	uint32_t Value{ 0 };
	std::wstring Name;
	std::string Guid;			// the event GUID of the task (MOF consumers), empty if none
	uint32_t MessageId{ NoEventMessage };
};

struct EventOpcode {
	uint16_t Value{ 0 };
	uint16_t Task{ 0 };			// the task the opcode is defined in, 0 for opcodes of the provider
	std::wstring Name;
	uint32_t MessageId{ NoEventMessage };
};

struct EventKeyword {
	uint64_t Mask{ 0 };
	std::wstring Name;
	uint32_t MessageId{ NoEventMessage };
};

struct EventMapEntry {
	uint32_t Value{ 0 };
	uint32_t MessageId{ NoEventMessage };
};

// a value map (each value has a text) or a bitmap (each bit has a text)
struct EventMap {
	std::wstring Name;
	bool Bitmap{ false };
	std::vector<EventMapEntry> Entries;
};

struct EventField {
	std::wstring Name;
	uint8_t InType{ 0 };		// how the value is stored: win:UInt32, win:UnicodeString...
	uint8_t OutType{ 0 };		// how it is shown: xs:unsignedInt, win:HexInt32, win:Win32Error...
	uint32_t Flags{ 0 };
	uint16_t Count{ 0 };
	uint16_t Length{ 0 };
	int Map{ -1 };				// the value map of the provider, or -1
};

// the data of events: its fields, and the XML that the event is shown as (BinXml)
struct EventTemplate {
	std::string Guid;
	std::vector<EventField> Fields;
	uint32_t XmlSize{ 0 };
};

struct EventDefinition {
	uint16_t Id{ 0 };
	uint8_t Version{ 0 };
	uint8_t Channel{ 0 };
	uint8_t Level{ 0 };
	uint8_t Opcode{ 0 };
	uint16_t Task{ 0 };
	uint64_t Keywords{ 0 };
	uint32_t MessageId{ NoEventMessage };
	// the definitions that the event refers to (indices of the provider's), -1 if none
	int Template{ -1 };
	int ChannelIndex{ -1 };
	int LevelIndex{ -1 };
	int OpcodeIndex{ -1 };
	int TaskIndex{ -1 };
	std::vector<int> KeywordIndices;
};

struct EventProvider {
	std::string Guid;
	std::wstring Name;				// from the provider attributes; empty if they are not there
	uint32_t MessageId{ NoEventMessage };
	std::vector<EventChannel> Channels;
	std::vector<EventLevel> Levels;
	std::vector<EventTask> Tasks;
	std::vector<EventOpcode> Opcodes;
	std::vector<EventKeyword> Keywords;
	std::vector<EventMap> Maps;
	std::vector<EventTemplate> Templates;
	std::vector<EventDefinition> Events;
};

struct EventManifest {
	uint16_t MajorVersion{ 0 };
	uint16_t MinorVersion{ 0 };
	std::vector<EventProvider> Providers;
};

// Parses the contents of a WEVT_TEMPLATE resource; nothing if it is not one. Parts that are malformed are left out.
std::optional<EventManifest> ParseEventManifest(std::span<const std::byte> data);

// The names of the manifest for the types of fields ("win:UInt32", "xs:string"); empty for an unknown type
const wchar_t* EventInTypeName(uint8_t type);
const wchar_t* EventOutTypeName(uint8_t type);
