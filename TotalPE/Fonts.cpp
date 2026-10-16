#include "pch.h"
#include "Fonts.h"
#include "AppSettings.h"

namespace {
	PCWSTR const CodeProperty = L"TotalPE.CodeFont";

	// the fonts made of the settings (remade when they change)
	struct Made {
		LOGFONT Font{};
		HFONT Handle{}, Previous{};
		HFONT Get(LOGFONT const& font) {
			if (!Handle || memcmp(&font, &Font, sizeof(font)) != 0) {
				// (the one before stays until the controls have the new one: it is deleted with the next change)
				if (Previous)
					::DeleteObject(Previous);
				Previous = Handle;
				Font = font;
				Handle = ::CreateFontIndirect(&font);
			}
			return Handle;
		}
	};
	Made code, application;

	HFONT ApplicationFont() {
		LOGFONT font;
		return Fonts::Application(font) ? application.Get(font) : nullptr;
	}

	// a control of the application's windows that takes the application font: not one of a dialog (nor a dialog's child)
	bool TakesApplicationFont(HWND hWnd) {
		WCHAR name[64];
		if (!::GetClassName(hWnd, name, _countof(name)))
			return false;
		static PCWSTR const classes[] = { WC_LISTVIEW, WC_TREEVIEW, WC_EDIT, WC_COMBOBOX, WC_COMBOBOXEX, WC_LISTBOX, WC_STATIC, WC_BUTTON };
		if (std::ranges::none_of(classes, [&](PCWSTR c) { return _wcsicmp(name, c) == 0; }))
			return false;
		for (auto parent = ::GetParent(hWnd); parent; parent = ::GetParent(parent))
			if (::GetClassName(parent, name, _countof(name)) && wcscmp(name, L"#32770") == 0)
				return false;
		return true;
	}

	void ApplyTo(HWND hWnd, HFONT app) {
		if (::GetProp(hWnd, CodeProperty))
			::SendMessage(hWnd, WM_SETFONT, (WPARAM)Fonts::CodeFont(), TRUE);
		else if (app && TakesApplicationFont(hWnd))
			::SendMessage(hWnd, WM_SETFONT, (WPARAM)app, TRUE);
	}

	void CALLBACK OnCreate(HWINEVENTHOOK, DWORD, HWND hWnd, LONG object, LONG child, DWORD, DWORD) {
		if (object != OBJID_WINDOW || child != CHILDID_SELF || !hWnd || !::IsWindow(hWnd))
			return;
		if (auto app = ApplicationFont(); app && !::GetProp(hWnd, CodeProperty) && TakesApplicationFont(hWnd))
			::SendMessage(hWnd, WM_SETFONT, (WPARAM)app, FALSE);
	}
}

LOGFONT Fonts::Code() {
	auto font = AppSettings::Get().CodeFont();
	if (font.lfFaceName[0] && font.lfHeight)
		return font;
	font = {};
	wcscpy_s(font.lfFaceName, L"Consolas");
	font.lfWeight = FW_NORMAL;
	font.lfCharSet = DEFAULT_CHARSET;
	font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	CClientDC dc(nullptr);
	font.lfHeight = -::MulDiv(11, dc.GetDeviceCaps(LOGPIXELSY), 72);
	return font;
}

bool Fonts::Application(LOGFONT& font) {
	font = AppSettings::Get().ApplicationFont();
	return font.lfFaceName[0] && font.lfHeight;
}

int Fonts::Points(LOGFONT const& font) {
	CClientDC dc(nullptr);
	return std::max(1, ::MulDiv(std::abs(font.lfHeight), 72, dc.GetDeviceCaps(LOGPIXELSY)));
}

void Fonts::MarkCode(HWND hWnd) {
	::SetProp(hWnd, CodeProperty, (HANDLE)1);
}

HFONT Fonts::CodeFont() {
	return code.Get(Code());
}

void Fonts::Install() {
	// (out of context: the event comes to this thread's message loop, after the control is made and its window has set
	// what it sets, as the code font of a control of code)
	::SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, OnCreate, ::GetCurrentProcessId(), ::GetCurrentThreadId(),
		WINEVENT_OUTOFCONTEXT);
}

void Fonts::Apply() {
	auto app = ApplicationFont();
	::EnumThreadWindows(::GetCurrentThreadId(), [](HWND top, LPARAM app) {
		::EnumChildWindows(top, [](HWND hWnd, LPARAM app) {
			ApplyTo(hWnd, (HFONT)app);
			return TRUE;
			}, app);
		return TRUE;
		}, (LPARAM)app);
}
