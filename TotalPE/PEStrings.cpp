#include "pch.h"
#include "PEStrings.h"
#include <atltime.h>
#include <DbgHelp.h>
#include "..\External\Capstone\capstone.h"

#pragma comment(lib, "dbghelp")

static PCWSTR memberVisiblity[] = {
	L"Private Scope", L"Private", L"Protected and Internal", L"Internal", L"Protected",
	L"Protected or Internal", L"Public"
};

enum class SectionFlags : unsigned {
	None = 0,
	NoPad = 8,
	Code = 0x20,
	InitializedData = 0x40,
	UninitializedData = 0x80,
	Other = 0x100,
	Info = 0x200,
	Remove = 0x800,
	Comdat = 0x1000,
	GPRel = 0x80000,
	Align1Byte = 0x100000,
	Align2Bytes = 0x200000,
	ExtendedReloc = 0x1000000,
	Discardable = 0x2000000,
	NotCached = 0x4000000,
	NotPaged = 0x8000000,
	Shared = 0x10000000,
	Execute = 0x20000000,
	Read = 0x40000000,
	Write = 0x80000000,
};
DEFINE_ENUM_FLAG_OPERATORS(SectionFlags);

std::wstring PEStrings::SubsystemToString(uint32_t type) {
	switch (type) {
		case IMAGE_SUBSYSTEM_NATIVE: return L"Native";
		case IMAGE_SUBSYSTEM_WINDOWS_GUI: return L"Window GUI";
		case IMAGE_SUBSYSTEM_WINDOWS_CUI: return L"Windows CUI";
		case IMAGE_SUBSYSTEM_OS2_CUI: return L"OS2 CUI";
		case IMAGE_SUBSYSTEM_POSIX_CUI: return L"POSIX CUI";
		case IMAGE_SUBSYSTEM_NATIVE_WINDOWS: return L"Native Windows 9x";
		case IMAGE_SUBSYSTEM_WINDOWS_CE_GUI: return L"Windows CE GUI";
		case IMAGE_SUBSYSTEM_EFI_APPLICATION: return L"EFI Application";
		case IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER: return L"EFI Boot Service Driver";
		case IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER: return L"EFI Runtime Driver";
		case IMAGE_SUBSYSTEM_EFI_ROM: return L"EFI ROM";
		case IMAGE_SUBSYSTEM_XBOX: return L"XBOX";
		case IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION: return L"Windows Boot Application";
	}
	return L"(Unknown)";
}

std::wstring PEStrings::ToDecAndHex(uint32_t value, bool hexFirst) {
	std::wstring text;
	if (hexFirst)
		text = std::format(L"0x{:X} ({})", value, value);
	else
		text = std::format(L"{} (0x{:X})", value, value);
	return text;
}

std::wstring PEStrings::MagicToString(uint16_t magic) {
	switch (magic) {
		case IMAGE_NT_OPTIONAL_HDR32_MAGIC: return L"PE32";
		case IMAGE_NT_OPTIONAL_HDR64_MAGIC: return L"PE32+";
		case IMAGE_ROM_OPTIONAL_HDR_MAGIC: return L"ROM";
	}
	return L"";
}

std::wstring_view PEStrings::MachineTypeToString(uint16_t type) {
	switch (type) {
		case IMAGE_FILE_MACHINE_UNKNOWN: return L"Unknown";
		case IMAGE_FILE_MACHINE_I386: return L"x86";
		case IMAGE_FILE_MACHINE_ARM: return L"ARM";
		case IMAGE_FILE_MACHINE_ARMNT: return L"ARM NT";
		case IMAGE_FILE_MACHINE_IA64: return L"IA64";
		case IMAGE_FILE_MACHINE_AMD64: return L"x64";
		case IMAGE_FILE_MACHINE_ARM64: return L"ARM 64";
	}
	return L"";
}

std::wstring PEStrings::DllCharacteristicsToString(uint32_t dc) {
	std::wstring result;

	static const struct {
		DllCharacteristics cs;
		PCWSTR text;
	} chars[] = {
		{ DllCharacteristics::AppContainer,			L"App Container" },
		{ DllCharacteristics::HighEntropyVA,		L"High Entropy VA" },
		{ DllCharacteristics::DynamicBase,			L"Dynamic Base" },
		{ DllCharacteristics::ForceIntegrity,		L"Force Integrity" },
		{ DllCharacteristics::NxCompat,				L"NX Compat" },
		{ DllCharacteristics::ControlFlowGuard,		L"Control Flow Guard" },
		{ DllCharacteristics::NoBind,				L"No Bind" },
		{ DllCharacteristics::WDMDriver,			L"WDM Driver" },
		{ DllCharacteristics::NoIsolation,			L"No Isolation" },
		{ DllCharacteristics::TerminalServerAware,	L"Terminal Server Aware" },
		{ DllCharacteristics::NoSEH,				L"No SEH" },
	};

	for (auto& ch : chars) {
		if ((ch.cs & (DllCharacteristics)dc) == ch.cs)
			result += std::wstring(ch.text) + L", ";
	}

	if (!result.empty())
		result = result.substr(0, result.size() - 2);
	return result;

}

std::wstring PEStrings::Sec1970ToString(uint32_t secs) {
	return (PCWSTR)CTime(secs).Format(L"%X");
}

std::wstring PEStrings::CharacteristicsToString(uint32_t cs) {
	std::wstring result;

	static const struct {
		uint32_t cs;
		PCWSTR text;
	} chars[] = {
		{ IMAGE_FILE_RELOCS_STRIPPED,			L"Relocations Stripped" },
		{ IMAGE_FILE_EXECUTABLE_IMAGE,			L"Executable Image" },
		{ IMAGE_FILE_AGGRESIVE_WS_TRIM,			L"Aggressive Trim Working Set" },
		{ IMAGE_FILE_LARGE_ADDRESS_AWARE,		L"Large Address Aware" },
		{ IMAGE_FILE_DEBUG_STRIPPED,			L"Debug Info Stripped" },
		{ IMAGE_FILE_LINE_NUMS_STRIPPED,		L"Line Numbers Stripped" },
		{ IMAGE_FILE_DLL,						L"DLL File" },
		{ IMAGE_FILE_BYTES_REVERSED_LO,			L"Little Endian" },
		{ IMAGE_FILE_32BIT_MACHINE,				L"32-Bit Machine" },
		{ IMAGE_FILE_LOCAL_SYMS_STRIPPED,		L"Local Symbols Stripped" },
		{ IMAGE_FILE_NET_RUN_FROM_SWAP,			L"Net Run from Swap" },
		{ IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP,	L"Removable Run from Swap" },
		{ IMAGE_FILE_UP_SYSTEM_ONLY,			L"Single CPU Only" },
		{ IMAGE_FILE_SYSTEM,					L"System File" },
		{ IMAGE_FILE_BYTES_REVERSED_HI,			L"Big Endian" },
	};

	for (auto& ch : chars) {
		if ((ch.cs & (uint32_t)cs) == ch.cs)
			result += std::wstring(ch.text) + L", ";
	}

	if (!result.empty())
		result = result.substr(0, result.size() - 2);
	return result;
}

std::wstring PEStrings::ToHex(uint32_t value, bool leadingZero) {
	if (leadingZero)
		return std::format(L"0x{:08X}", value);
	return std::format(L"0x{:X}", value);
}

std::wstring PEStrings::ToHex(ULONGLONG value) {
	auto result = std::format(L"0x{:X}", value);
	return result;
}

std::wstring PEStrings::ToMemorySize(ULONGLONG size) {
	auto result = std::format(L"0x{:X}", size);
	if (size < 1 << 14)
		result = std::format(L"{} ({} B)", result, size);
	else if (size < 1 << 23)
		result = std::format(L"{} ({} KB)", result, size >> 10);
	else if (size < 1LL << 33)
		result = std::format(L"{} ({} MB)", result, size >> 20);
	else
		result = std::format(L"{} ({} GB)", result, size >> 30);
	return result;
}

std::wstring PEStrings::ResourceTypeToString(WORD id) {
	static PCWSTR types[] = {
		L"", L"Cursor", L"Bitmap", L"Icon",	L"Menu", L"Dialog",
		L"String Table", L"Font Directory", L"Font",
		L"Accelerators", L"RC Data", L"Message Table", L"Group Cursor", L"",
		L"Group Icon", L"", L"Version", L"Dialog Include",
		L"", L"Plug & Play", L"VxD", L"Animated Cursor", L"Animated Icon",
		L"HTML", L"Manifest"
	};
	return id >= _countof(types) ? L"" : types[id];
}

CStringA PEStrings::FormatInstruction(const cs_insn& inst) {
	CStringA text;
	text.Format("%llX %-10s %-40s ;", inst.address, inst.mnemonic, inst.op_str);
//	text.Format("%-10s %-40s ;", inst.mnemonic, inst.op_str);
	for (int i = 0; i < inst.size; i++)
		text += std::format(" {:02X}", inst.bytes[i]).c_str();
	return text;
}

std::wstring PEStrings::ManagedTypeAttributesToString(CorTypeAttr attr) {
	static PCWSTR visiblity[] = {
		L"Private", L"Public", L"Nested Public", L"Nested Private",
		L"Nested Family", L"Nested Internal", L"Nested Protected and Internal", L"Nested Protected Internal"
	};

	std::wstring text = visiblity[attr & tdVisibilityMask];
	text += L", ";

	if ((attr & tdLayoutMask) == tdSequentialLayout)
		text += L"Sequential, ";
	else if ((attr & tdLayoutMask) == tdExplicitLayout)
		text += L"Explicit, ";

	if ((attr & tdClassSemanticsMask) == tdInterface)
		text += L"Interface, ";

	if (attr & tdAbstract)
		text += L"Abstract, ";
	if (attr & tdSealed)
		text += L"Sealed, ";
	if (attr & tdSpecialName)
		text += L"Special Name, ";
	if (attr & tdImport)
		text += L"Import, ";
	if (attr & tdSerializable)
		text += L"Serializable, ";
	if (attr & tdWindowsRuntime)
		text += L"Windows Runtime, ";

	return text.substr(0, text.size() - 2);
}

//std::wstring PEStrings::MemberAttributesToString(const ManagedMember& member) {
//	switch (member.Type) {
//		case ManagedMemberType::Method:
//		case ManagedMemberType::Constructor:
//		case ManagedMemberType::StaticConstructor:
//			return MethodAttributesToString((CorMethodAttr)member.Attributes);
//		case ManagedMemberType::Field:
//			return FieldAttributesToString((CorFieldAttr)member.Attributes);
//		case ManagedMemberType::Property:
//			return PropertyAttributesToString((CorPropertyAttr)member.Attributes);
//		case ManagedMemberType::Event:
//			return EventAttributesToString((CorEventAttr)member.Attributes);
//	}
//
//	return L"";
//}

std::wstring PEStrings::MethodAttributesToString(CorMethodAttr attr) {
	std::wstring text = memberVisiblity[attr & mdMemberAccessMask];
	text += L", ";

	if (attr & mdStatic)
		text += L"Static, ";
	if (attr & mdFinal)
		text += L"Final, ";
	if (attr & mdVirtual)
		text += L"Virtual, ";
	if (attr & mdHideBySig)
		text += L"Hide By Sig, ";

	if ((attr & mdVtableLayoutMask) == mdNewSlot)
		text += L"New Slot, ";

	if (attr & mdCheckAccessOnOverride)
		text += L"Check Access on Override, ";
	if (attr & mdAbstract)
		text += L"Abstract, ";
	if (attr & mdSpecialName)
		text += L"Special Name, ";
	if (attr & mdPinvokeImpl)
		text += L"P/Invoke, ";
	if (attr & mdUnmanagedExport)
		text += L"Unmanaged Export, ";

	if ((attr & mdReservedMask) == mdRTSpecialName)
		text += L"Runtime Special Name, ";
	if ((attr & mdReservedMask) == mdHasSecurity)
		text += L"Has Security, ";
	if ((attr & mdReservedMask) == mdRequireSecObject)
		text += L"Require Secure Object, ";

	return text.substr(0, text.size() - 2);
}

std::wstring PEStrings::FieldAttributesToString(CorFieldAttr attr) {
	std::wstring text = memberVisiblity[attr & fdFieldAccessMask];
	text += L", ";

	if (attr & fdStatic)
		text += L"Static, ";
	if (attr & fdInitOnly)
		text += L"Read Only, ";
	if (attr & fdLiteral)
		text += L"Literal, ";
	if (attr & fdNotSerialized)
		text += L"Not Serialized, ";
	if (attr & fdSpecialName)
		text += L"Special Name, ";
	if (attr & fdPinvokeImpl)
		text += L"P/Invoke, ";

	if ((attr & fdReservedMask) == fdRTSpecialName)
		text += L"Runtime Special Name, ";
	if ((attr & fdReservedMask) == fdHasFieldMarshal)
		text += L"Has Field Marshal, ";
	if ((attr & fdReservedMask) == fdHasDefault)
		text += L"Has Default, ";
	if ((attr & fdReservedMask) == fdHasFieldRVA)
		text += L"Has Field RVA, ";

	return text.substr(0, text.size() - 2);
}

std::wstring PEStrings::PropertyAttributesToString(CorPropertyAttr attr) {
	std::wstring text;

	if (IsPrSpecialName(attr))
		text += L"Special Name, ";
	if (IsPrRTSpecialName(attr))
		text += L"Runtime Special Name";
	if (IsPrHasDefault(attr))
		text += L"Has Default, ";

	if (!text.empty())
		text = text.substr(0, text.size() - 2);
	return text;
}

std::wstring PEStrings::EventAttributesToString(CorEventAttr attr) {
	std::wstring text;

	if (IsEvSpecialName(attr))
		text += L"Special Name, ";
	if (IsEvRTSpecialName(attr))
		text += L"Runtime Special Name";

	if (!text.empty())
		text = text.substr(0, text.size() - 2);
	return text;
}


PCWSTR PEStrings::GetDataDirectoryName(int index) {
	PCWSTR names[] = {
		L"Export", L"Import", L"Resource", L"Exception", L"Security", L"Base Relocation",
		L"Debug", L"Architecture", L"Global Pointer", L"Thread Local Storage", L"Load Config",
		L"Bound Import", L"IAT", L"Delay Import", L"COM Descriptor (CLR)"
	};
	return index < 0 || index >= _countof(names) ? L"" : names[index];
}

std::wstring PEStrings::SectionCharacteristicsToString(uint32_t c) {
	std::wstring result;
	auto ch = static_cast<SectionFlags>(c);

	struct {
		SectionFlags Flags;
		PCWSTR Text;
	} items[] = {
		{ SectionFlags::NoPad,				L"No Pad" },
		{ SectionFlags::Code,				L"Code" },
		{ SectionFlags::Read,				L"Read" },
		{ SectionFlags::Write,				L"Write" },
		{ SectionFlags::Execute,			L"Execute" },
		{ SectionFlags::Shared,				L"Shared" },
		{ SectionFlags::InitializedData,	L"Initialized Data" },
		{ SectionFlags::UninitializedData,	L"Uninitialized Data" },
		{ SectionFlags::Remove,				L"Remove" },
		{ SectionFlags::Discardable,		L"Discardable" },
		{ SectionFlags::Info,				L"Info" },
		{ SectionFlags::Comdat,				L"Comdat" },
		{ SectionFlags::GPRel,				L"GP Relative" },
		{ SectionFlags::ExtendedReloc,		L"Extended Reloc" },
		{ SectionFlags::NotPaged,			L"Not Paged" },
		{ SectionFlags::NotCached,			L"Not Cached" },
	};

	for (auto& item : items)
		if ((ch & item.Flags) != SectionFlags::None)
			result += item.Text + std::wstring(L", ");

	if (!result.empty())
		result = result.substr(0, result.length() - 2);

	return result;
}

std::wstring PEStrings::PrimaryLanguageToString(WORD l) {
	static const std::unordered_map<WORD, std::wstring> languages = {
		{ LANG_NEUTRAL,				L"Neutral" },
		{ LANG_INVARIANT,			L"Invariant" },
		{ LANG_AFRIKAANS,			L"Afrikaans" },
		{ LANG_ALBANIAN,			L"Albanian" },
		{ LANG_ALSATIAN,			L"Alsatian" },
		{ LANG_AMHARIC,				L"Amharic" },
		{ LANG_ARABIC,				L"Arabic" },
		{ LANG_ARMENIAN,			L"Armenian" },
		{ LANG_ASSAMESE,			L"Assmese" },
		{ LANG_AZERI,				L"Azeri" },
		{ LANG_AZERBAIJANI,			L"Azerbaujani" },
		{ LANG_BANGLA,				L"Bangla" },
		{ LANG_BASHKIR,				L"Bashkir" },
		{ LANG_BASQUE,				L"Basque" },
		{ LANG_BELARUSIAN,			L"Belarusian" },
		{ LANG_BENGALI,				L"Bengali" },
		{ LANG_BRETON,				L"Breton" },
		{ LANG_BOSNIAN,				L"Bosnian" },
		{ LANG_BOSNIAN_NEUTRAL,		L"Bosnian Neutral" },
		{ LANG_BULGARIAN,			L"Bulgarian" },
		{ LANG_CATALAN,				L"Catalan" },
		{ LANG_CENTRAL_KURDISH,		L"Central Kurdish" },
		{ LANG_CHEROKEE,			L"Cherokee" },
		{ LANG_CHINESE,				L"Chinese" },
		{ LANG_CHINESE_SIMPLIFIED,	L"Simplified Chinese" },
		{ LANG_CHINESE_TRADITIONAL, L"Traditional Chinese" },
		{ LANG_CORSICAN,			L"Corsican" },
		{ LANG_CROATIAN,			L"Croatian" },
		{ LANG_CZECH,				L"Czech" },
		{ LANG_DANISH,				L"Danish" },
		{ LANG_DARI,				L"Dari" },
		{ LANG_DIVEHI,				L"Divehi" },
		{ LANG_DUTCH,				L"Dutch" },
		{ LANG_ENGLISH,				L"English" },
		{ LANG_ESTONIAN,			L"Estonian" },
		{ LANG_FAEROESE,			L"Faerose" },
		{ LANG_FARSI,				L"Farsi" },
		{ LANG_FILIPINO,			L"Filipino" },
		{ LANG_FINNISH,				L"Finnish" },
		{ LANG_FRENCH,				L"French" },
		{ LANG_FRISIAN,				L"Frisian" },
		{ LANG_FULAH,				L"Fulah" },
		{ LANG_GALICIAN,			L"Galician" },
		{ LANG_GEORGIAN,			L"Georgian" },
		{ LANG_GERMAN,				L"German" },
		{ LANG_GREEK,				L"Greek" },
		{ LANG_GREENLANDIC,			L"Greenlandic" },
		{ LANG_GUJARATI,			L"Gujarati" },
		{ LANG_HAUSA,				L"Hausa" },
		{ LANG_HAWAIIAN,			L"Hawaiian" },
		{ LANG_HEBREW,				L"Hebrew" },
		{ LANG_HINDI,				L"Hindi" },
		{ LANG_HUNGARIAN,			L"Hungarian" },
		{ LANG_ICELANDIC,			L"Icelandic" },
		{ LANG_IGBO,				L"Igbo" },
		{ LANG_INDONESIAN,			L"Indonesian" },
		{ LANG_INUKTITUT,			L"Inuktitut" },
		{ LANG_IRISH,				L"Irish" },
		{ LANG_ITALIAN,				L"Italian" },
		{ LANG_JAPANESE,			L"Japanese" },
		//{ LANG_KANNADA
		//{ LANG_KASHMIRI
		//{ LANG_KAZAK
		//{ LANG_KHMER
		//{ LANG_KICHE
		//{ LANG_KINYARWANDA
		//{ LANG_KONKANI
		//{ LANG_KOREAN
		//{ LANG_KYRGYZ
		//{ LANG_LAO
		{ LANG_LATVIAN,				L"Latvian" },
		{ LANG_LITHUANIAN,			L"Lithuanian" },
		//{ LANG_LOWER_SORBIAN
		//{ LANG_LUXEMBOURGISH
		//{ LANG_MACEDONIAN
		//{ LANG_MALAY
		//{ LANG_MALAYALAM
		//{ LANG_MALTESE
		//{ LANG_MANIPURI
		//{ LANG_MAORI
		//{ LANG_MAPUDUNGUN
		//{ LANG_MARATHI
		//{ LANG_MOHAWK
		//{ LANG_MONGOLIAN
		//{ LANG_NEPALI
		{ LANG_NORWEGIAN,			L"Norwegian" },
		//{ LANG_OCCITAN
		//{ LANG_ODIA
		//{ LANG_ORIYA
		//{ LANG_PASHTO
		{ LANG_PERSIAN,				L"Persian" },
		{ LANG_POLISH,				L"Polish" },
		{ LANG_PORTUGUESE,			L"Portuguese" },
		//{ LANG_PULAR
		//{ LANG_PUNJABI
		//{ LANG_QUECHUA
		{ LANG_ROMANIAN,			L"Romanian" },
		//{ LANG_ROMANSH
		{ LANG_RUSSIAN,				L"Russian" },
		//{ LANG_SAKHA
		//{ LANG_SAMI
		//{ LANG_SANSKRIT
		//{ LANG_SCOTTISH_GAELIC
		//{ LANG_SERBIAN
		//{ LANG_SERBIAN_NEUTRAL
		//{ LANG_SINDHI
		//{ LANG_SINHALESE
		//{ LANG_SLOVAK
		//{ LANG_SLOVENIAN
		//{ LANG_SOTHO
		{ LANG_SPANISH,				L"Spanish" },
		{ LANG_SWAHILI,				L"Swahili" },
		{ LANG_SWEDISH,				L"Swedish" },
		//{ LANG_SYRIAC
		//{ LANG_TAJIK
		//{ LANG_TAMAZIGHT
		//{ LANG_TAMIL
		//{ LANG_TATAR
		//{ LANG_TELUGU
		{ LANG_THAI,				L"Thai" },
		//{ LANG_TIBETAN
		//{ LANG_TIGRIGNA
		//{ LANG_TIGRINYA
		//{ LANG_TSWANA
		{ LANG_TURKISH,				L"Turkish" },
		//{ LANG_TURKMEN
		//{ LANG_UIGHUR
		//{ LANG_UKRAINIAN
		//{ LANG_UPPER_SORBIAN
		//{ LANG_URDU
		//{ LANG_UZBEK
		//{ LANG_VALENCIAN
		{ LANG_VIETNAMESE,			L"Vietnamese" },
		{ LANG_WELSH,				L"Welsh" },
		//{ LANG_WOLOF
		//{ LANG_XHOSA
		//{ LANG_YAKUT
		//{ LANG_YI
		//{ LANG_YORUBA
		{ LANG_ZULU,				L"Zulu" },
	};

	if (auto it = languages.find(l); it != languages.end())
		return it->second;
	return L"";
}

std::wstring PEStrings::UndecorateName(PCWSTR name) {
	WCHAR result[512];
	return 0 == ::UnDecorateSymbolNameW(name, result, _countof(result), 0) ? L"" : result;
}

std::string PEStrings::UndecorateName(PCSTR name) {
	char result[512];
	return 0 == ::UnDecorateSymbolName(name, result, _countof(result), 0) ? "" : result;
}

std::wstring PEStrings::VersionFileOSToString(uint32_t type) {
	static const std::unordered_map<uint32_t, std::wstring> types = {
		{ VOS_DOS,			L"MS-DOS" },
		{ VOS_NT,			L"Windows NT" },
		{ VOS__WINDOWS16,	L"16-bit Windows" },
		{ VOS__WINDOWS32,	L"32-bit Windows" },
		{ VOS_OS216,		L"16-bit OS/2" },
		{ VOS_OS232,		L"32-bit OS/2" },
		{ VOS__PM16,		L"16-bit Presentation Manager" },
		{ VOS__PM32,		L"32-bit Presentation Manager" },
		{ VOS_NT_WINDOWS32, L"Windows NT" },
		{ VOS_UNKNOWN,		L"Unknown" },
		{ VOS_DOS_WINDOWS32, L"32-bit Windows on MS-DOS" },
		{ VOS_DOS_WINDOWS16, L"16-bit Windows on MS-DOS" },
	};
	if (auto it = types.find(type); it != types.end())
		return it->second;
	return L"";
}

std::wstring PEStrings::LanguageToString(uint32_t l) {
	static const std::unordered_map<uint32_t, std::wstring> languages = {
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),				L"en-US" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK),				L"en-UK" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_AUS),			L"en-Australia" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_CAN),			L"en-Canada" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_NZ),				L"en-New Zealand" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_EIRE),			L"en-Eire" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_SOUTH_AFRICA),	L"en-South Africa" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_JAMAICA),		L"en-Jamaica" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_CARIBBEAN),		L"en-Caribbean" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_BELIZE),			L"en-Belize" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_TRINIDAD),		L"en-Trinidad" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_ZIMBABWE),		L"en-Zimbabwe" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_PHILIPPINES),	L"en-Philippines" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_INDIA),			L"en-India" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_MALAYSIA),		L"en-Malaysia" },
		{ MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_SINGAPORE),		L"en-Singapore" },
		{ MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),				L"Language Neutral" },
		{ MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),				L"User Default Language" },
		{ MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT),			L"System Default Language" },
		{ MAKELANGID(LANG_NEUTRAL, SUBLANG_CUSTOM_DEFAULT),			L"Default Custom Locale" },
		{ MAKELANGID(LANG_NEUTRAL, SUBLANG_CUSTOM_UNSPECIFIED),		L"Unspecified Custom Locale" },
		{ MAKELANGID(LANG_NEUTRAL, SUBLANG_UI_CUSTOM_DEFAULT),		L"Default custom MUI Locale" },
		{ MAKELANGID(LANG_HEBREW, SUBLANG_HEBREW_ISRAEL),			L"he-IL" },
	};

	if (auto it = languages.find(l); it != languages.end()) {
		return it->second;
	}
	return PrimaryLanguageToString(PRIMARYLANGID(l));
}

std::wstring PEStrings::FileTypeToString(uint32_t type) {
	switch (type) {
		case VFT_APP: return L"Application";
		case VFT_DLL: return L"Dynamic Link Library";
		case VFT_DRV: return L"Device Driver";
		case VFT_FONT: return L"Font";
		case VFT_STATIC_LIB: return L"Static Library";
		case VFT_VXD: return L"VxD";
	}
	return L"Unknown";
}

std::wstring PEStrings::FileSubTypeToString(uint32_t type, uint32_t subType) {
	return L"";
}

PCWSTR PEStrings::DebugTypeToString(uint32_t type) {
	switch (type) {
		case IMAGE_DEBUG_TYPE_UNKNOWN: return L"Unknown";
		case IMAGE_DEBUG_TYPE_COFF: return L"COFF";
		case IMAGE_DEBUG_TYPE_CODEVIEW: return L"Visual C++";
		case IMAGE_DEBUG_TYPE_FPO: return L"Frame Point Omission (FPO)";
		case IMAGE_DEBUG_TYPE_MISC: return L"Location";
		case IMAGE_DEBUG_TYPE_EXCEPTION: return L"Exception";
		case IMAGE_DEBUG_TYPE_FIXUP: return L"Fixup";
		case IMAGE_DEBUG_TYPE_OMAP_TO_SRC: return L"Image to Source";
		case IMAGE_DEBUG_TYPE_OMAP_FROM_SRC: return L"Source to Image";
		case IMAGE_DEBUG_TYPE_BORLAND: return L"Borland";
		case IMAGE_DEBUG_TYPE_CLSID: return L"CLSID";
		case IMAGE_DEBUG_TYPE_REPRO: return L"Repro";
		case IMAGE_DEBUG_TYPE_POGO: return L"POGO";
		case IMAGE_DEBUG_TYPE_ILTCG: return L"IL TCG";
		case IMAGE_DEBUG_TYPE_MPX: return L"MPX";
		case IMAGE_DEBUG_TYPE_VC_FEATURE: return L"Visual C++ Feature";
		case IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS: return L"Extended DLL Characteristics";
	}
	return L"(Reserved)";
}

PCWSTR PEStrings::x64RelocationTypeToString(BYTE type) {
	switch (type) {
		case IMAGE_REL_AMD64_ABSOLUTE:	return L"Absolute";
		case IMAGE_REL_AMD64_ADDR64:	return L"64-bit Address";
		case IMAGE_REL_AMD64_ADDR32:	return L"32-bit Address";
		case IMAGE_REL_AMD64_ADDR32NB:	return L"32-bit Address without Image Base";
		case IMAGE_REL_AMD64_REL32:		return L"32-bit relative";
		case IMAGE_REL_AMD64_REL32_1:	return L"32-bit relative (distance 1)";
		case IMAGE_REL_AMD64_REL32_2:	return L"32-bit relative (distance 2)";
		case IMAGE_REL_AMD64_REL32_3:	return L"32-bit relative (distance 3)";
		case IMAGE_REL_AMD64_REL32_4:	return L"32-bit relative (distance 4)";
		case IMAGE_REL_AMD64_REL32_5:	return L"32-bit relative (distance 5)";
		case IMAGE_REL_AMD64_SECTION:	return L"Section Index";
		case IMAGE_REL_AMD64_SECREL:	return L"32-bit Offset from Target Section";
		case IMAGE_REL_AMD64_SECREL7:	return L"7-bit Unsigned Offset from Target Section";
		case IMAGE_REL_AMD64_TOKEN:		return L"32-bit Metadata Token";
		case IMAGE_REL_AMD64_SREL32:	return L"32 bit Signed Span-Dependent";
		case IMAGE_REL_AMD64_PAIR:		return L"Pair";
		case IMAGE_REL_AMD64_SSPAN32:	return L"32 bit Signed Span-Dependent (Link Time)";
	}
	return L"";
}

std::wstring PEStrings::CFGFlagsToString(uint32_t flags) {
	static const struct {
		uint32_t value;
		PCWSTR text;
	} fflags[] = {
		{ IMAGE_GUARD_CF_INSTRUMENTED,						L"Instrumented" },
		{ IMAGE_GUARD_CFW_INSTRUMENTED,						L"Write Instrumented" },
		{ IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT,			L"Function Table Present" },
		{ IMAGE_GUARD_SECURITY_COOKIE_UNUSED,				L"Security Cookie Unused" },
		{ IMAGE_GUARD_PROTECT_DELAYLOAD_IAT,				L"Protect Delay-Load IAT" },
		{ IMAGE_GUARD_DELAYLOAD_IAT_IN_ITS_OWN_SECTION,		L"Delay-Load IAT in Own Section" },
		{ IMAGE_GUARD_CF_EXPORT_SUPPRESSION_INFO_PRESENT,	L"Export Suppression Info" },
		{ IMAGE_GUARD_CF_ENABLE_EXPORT_SUPPRESSION,			L"Export Suppression" },
		{ IMAGE_GUARD_CF_LONGJUMP_TABLE_PRESENT,			L"Longjmp Table Present" },
		{ IMAGE_GUARD_RF_INSTRUMENTED,						L"Return Flow Info" },
		{ IMAGE_GUARD_RF_ENABLE,							L"Return Flow Enable" },
		{ IMAGE_GUARD_RF_STRICT,							L"Strict Mode Return Flow Enable" },
	};
	std::wstring result;
	for (auto& flag : fflags)
		if (flag.value & flags)
			result += flag.text + std::wstring(L", ");
	if (!result.empty())
		result = result.substr(0, result.length() - 2);
	return result;
}

std::wstring PEStrings::GuidToString(GUID const& guid) {
	WCHAR sguid[64];
	if (::StringFromGUID2(guid, sguid, _countof(sguid)))
		return sguid;
	return std::wstring();
}

PCWSTR PEStrings::SymbolTagToString(SymbolTag tag) {
	static PCWSTR names[] = {
		L"Null",
		L"Exe",
		L"Compiland",
		L"Compiland Details",
		L"Compiland Env",
		L"Function",
		L"Block",
		L"Data",
		L"Annotation",
		L"Label",
		L"Public Symbol",
		L"UDT",
		L"Enum",
		L"Function Type",
		L"Pointer Type",
		L"Array Type",
		L"Base Type",
		L"Typedef",
		L"Base Class",
		L"Friend",
		L"Function Arg Type",
		L"Func Debug Start",
		L"Func Debug End",
		L"Using Namespace",
		L"VTable Shape",
		L"VTable",
		L"Custom",
		L"Thunk",
		L"Custom Type",
		L"Managed Type",
		L"Dimension",
		L"Call Site",
		L"Inline Site",
		L"Base Interface",
		L"Vector Type",
		L"Matrix Type",
		L"HLSL Type",
		L"Caller",
		L"Callee",
		L"Export",
		L"Heap Allocation Site",
		L"Coff Group",
		L"Inlinee",
	};

	return names[(int)tag];
}

PCWSTR PEStrings::SymbolLocationToString(LocationKind location) {
	static PCWSTR names[] = {
		L"Null",
		L"Static",
		L"TLS",
		L"RegRel",
		L"ThisRel",
		L"Enregistered",
		L"BitField",
		L"Slot",
		L"IlRel",
		L"MetaData",
		L"Constant",
		L"RegRelAliasIndir",
	};

	return names[(int)location];
}

std::wstring PEStrings::FileFlagsToString(uint32_t flags) {
	static const struct {
		uint32_t value;
		PCWSTR text;
	} fflags[] = {
		{ VS_FF_DEBUG, L"Debug Information" },
		{ VS_FF_PATCHED, L"Patched" },
		{ VS_FF_PRERELEASE, L"Development" },
		{ VS_FF_PRIVATEBUILD, L"Private Build" },
		{ VS_FF_SPECIALBUILD, L"Special Build" },
	};
	std::wstring result;
	for (auto& flag : fflags)
		if (flag.value & flags)
			result += flag.text + std::wstring(L", ");
	if (!result.empty())
		result = result.substr(0, result.length() - 2);
	return result;
}

//CString PEStrings::FormatInstruction(const cs_insn& inst) {
//	std::string sbytes;
//	for (int i = 0; i < inst.size; i++)
//		sbytes += std::format("{:02X} ", inst.bytes[i]);
//	CStringA text;
//	text.Format("%llX %-48s %-10s %s", inst.address, sbytes.c_str(), inst.mnemonic, inst.op_str);
//	text.Replace((char)0xcd, ' ');
//	return CString(text);
//}

#define WIN_CERT_TYPE_PKCS1_SIGN            (0x0009)   // Certificate is PKCS1 signature.

PCWSTR PEStrings::CertificateTypeToString(uint32_t type) {
	switch (type) {
		case WIN_CERT_TYPE_X509: return L"X.509";
		case WIN_CERT_TYPE_PKCS_SIGNED_DATA: return L"PKCS SignedData";
		case WIN_CERT_TYPE_TS_STACK_SIGNED: return L"Terminal Server Protocol Stack";
		case WIN_CERT_TYPE_PKCS1_SIGN: return L"PKCS1";
	}
	return L"";
}
