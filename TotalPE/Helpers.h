#pragma once

class DiaSymbol;
class DiaSession;
class CTreeListView;
enum class SimpleType;
struct IMainFrame;

struct Helpers abstract final {
	static bool ExtractResource(HMODULE hModule, UINT resId, PCWSTR type, PCWSTR path, bool overwrite = true);
	static bool ExtractModules();
	static void FillTreeListView(IMainFrame* frame, CTreeListView& tv, HTREEITEM hRoot, DiaSymbol const& sym, DiaSession const& session, PVOID address);
	static std::wstring GetSpecialValue(DiaSymbol const& field, DiaSymbol const& type, PVOID address, bool& isString);
	static ULONG ReadVirtual(PVOID address, ULONG size, PVOID buffer);
	static std::wstring ValueToString(DiaSymbol const& value, PVOID address);
	static std::wstring FormatSimpleValue(PVOID value, ULONG size, SimpleType type);

	template<typename T> requires std::is_trivially_constructible_v<T>
	static T ReadValue(PVOID address) {
		T value;
		return ReadVirtual(address, sizeof(T), &value) ? value : (T)0;
	}
};

