#pragma once

struct cs_insn;
enum class SymbolTag;
enum class LocationKind;

enum class DllCharacteristics : unsigned short {
	None = 0,
	HighEntropyVA = 0x20,
	DynamicBase = 0x40,
	ForceIntegrity = 0x80,
	NxCompat = 0x100,
	NoIsolation = 0x200,
	NoSEH = 0x400,
	NoBind = 0x800,
	AppContainer = 0x1000,
	WDMDriver = 0x2000,
	ControlFlowGuard = 0x4000,
	TerminalServerAware = 0x8000
};
DEFINE_ENUM_FLAG_OPERATORS(DllCharacteristics);

struct PEStrings abstract final {
	static std::wstring MagicToString(uint16_t magic);
	static std::wstring SubsystemToString(uint32_t type);
	static std::wstring_view MachineTypeToString(uint16_t);
	static std::wstring CharacteristicsToString(uint32_t cs);
	static std::wstring ToDecAndHex(uint32_t value, bool hexFirst = false);
	static std::wstring Sec1970ToString(uint32_t secs);
	static std::wstring DllCharacteristicsToString(uint32_t ch);
	static std::wstring ToHex(uint32_t value, bool leadingZero = false);
	static std::wstring ToHex(ULONGLONG value);
	static std::wstring ToMemorySize(ULONGLONG size);
	static std::wstring ResourceTypeToString(WORD id);
	static CStringA FormatInstruction(const cs_insn& inst);
	static std::wstring ManagedTypeAttributesToString(CorTypeAttr attr);
	//static std::wstring MemberAttributesToString(const ManagedMember& member);
	static std::wstring MethodAttributesToString(CorMethodAttr attr);
	static std::wstring FieldAttributesToString(CorFieldAttr attr);
	static std::wstring PropertyAttributesToString(CorPropertyAttr attr);
	static std::wstring EventAttributesToString(CorEventAttr attr);
	static PCWSTR GetDataDirectoryName(int index);
	static std::wstring SectionCharacteristicsToString(uint32_t c);
	static std::wstring PrimaryLanguageToString(WORD l);
	static std::wstring LanguageToString(uint32_t lang);
	static std::wstring UndecorateName(PCWSTR name);
	static std::string UndecorateName(PCSTR name);
	static std::wstring VersionFileOSToString(uint32_t type);
	static std::wstring FileTypeToString(uint32_t type);
	static std::wstring FileSubTypeToString(uint32_t type, uint32_t subType);
	static std::wstring FileFlagsToString(uint32_t flags);
	static PCWSTR DebugTypeToString(uint32_t type);
	static PCWSTR CertificateTypeToString(uint32_t type);
	static PCWSTR x64RelocationTypeToString(BYTE type);
	static std::wstring CFGFlagsToString(uint32_t flags);
	static std::wstring GuidToString(GUID const& guid);
	static PCWSTR SymbolTagToString(SymbolTag tag);
	static PCWSTR SymbolLocationToString(LocationKind location);
};

