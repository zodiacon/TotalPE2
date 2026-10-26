#pragma once

#include <TreeListView.h>

class DiaSymbol;
class DiaSession;
enum class SimpleType;
struct IMainFrame;

struct Helpers abstract final {
	static bool ExtractResource(HMODULE hModule, UINT resId, PCWSTR type, PCWSTR path, bool overwrite = true);
	static void FillTreeListView(IMainFrame* frame, CTreeListViewCtrl& tv, HTLITEM hRoot, DiaSymbol const& sym, DiaSession const& session, PVOID address);
	static std::wstring GetSpecialValue(DiaSymbol const& field, DiaSymbol const& type, PVOID address, bool& isString);
	static ULONG ReadVirtual(PVOID address, ULONG size, PVOID buffer);
	static std::wstring ValueToString(DiaSymbol const& value, PVOID address);
	static std::wstring FormatSimpleValue(PVOID value, ULONG size, SimpleType type);
	// a filter box in a band of a view's rebar (width, height: at 96 DPI) gets the message font at its DPI, and that font
	// and the size of its band again when the DPI changes
	static void FilterFollowsDpi(HWND hFilter, int width, int height = 20);

	template<typename T> requires std::is_trivially_constructible_v<T>
	static T ReadValue(PVOID address) {
		T value;
		return ReadVirtual(address, sizeof(T), &value) ? value : (T)0;
	}
};

