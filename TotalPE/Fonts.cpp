#include "pch.h"
#include "Fonts.h"
#include "AppSettings.h"
#include <DpiHelper.h>
#include <map>

namespace {
	PCWSTR const CodeProperty = L"TotalPE.CodeFont";

	// the fonts made of the settings, one for each DPI there are controls at (remade when the settings change)
	struct Made {
		LOGFONT Font{};		// at 96 DPI
		std::map<UINT, HFONT> Handles;
		std::vector<HFONT> Previous;
		HFONT Get(LOGFONT const& font, UINT dpi) {
			if (memcmp(&font, &Font, sizeof(font)) != 0) {
				// (those before stay until the controls have the new ones: they are deleted with the next change)
				for (auto h : Previous)
					::DeleteObject(h);
				Previous.clear();
				for (auto& [d, h] : Handles)
					Previous.push_back(h);
				Handles.clear();
				Font = font;
			}
			auto& handle = Handles[dpi];
			if (!handle) {
				auto scaled = Fonts::ToDpi(font, dpi);
				handle = ::CreateFontIndirect(&scaled);
			}
			return handle;
		}
	};
	Made code, application;

	HFONT ApplicationFont(UINT dpi) {
		LOGFONT font;
		return Fonts::Application(font) ? application.Get(font, dpi) : nullptr;
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

	LRESULT CALLBACK FollowDpiProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	// the font of a control at its DPI (false if it keeps its own); it gets the font for its DPI again when that changes
	bool ApplyTo(HWND hWnd, BOOL redraw) {
		HFONT font = nullptr;
		if (::GetProp(hWnd, CodeProperty))
			font = Fonts::CodeFont(hWnd);
		else if (TakesApplicationFont(hWnd))
			font = ApplicationFont(DpiHelper::GetDpi(hWnd));
		if (!font)
			return false;
		::SendMessage(hWnd, WM_SETFONT, (WPARAM)font, redraw);
		::SetWindowSubclass(hWnd, FollowDpiProc, 1, 0);
		return true;
	}

	// (the control scales the font it made itself, not one it was given)
	LRESULT CALLBACK FollowDpiProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
		switch (msg) {
			case WM_DPICHANGED_AFTERPARENT:
			{
				auto result = ::DefSubclassProc(hWnd, msg, wp, lp);
				ApplyTo(hWnd, TRUE);
				return result;
			}
			case WM_NCDESTROY:
				::RemoveWindowSubclass(hWnd, FollowDpiProc, id);
				break;
		}
		return ::DefSubclassProc(hWnd, msg, wp, lp);
	}

	void CALLBACK OnCreate(HWINEVENTHOOK, DWORD, HWND hWnd, LONG object, LONG child, DWORD, DWORD) {
		if (object != OBJID_WINDOW || child != CHILDID_SELF || !hWnd || !::IsWindow(hWnd))
			return;
		// (a control of code has its font already)
		if (!::GetProp(hWnd, CodeProperty))
			ApplyTo(hWnd, FALSE);
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
	font.lfHeight = -::MulDiv(11, USER_DEFAULT_SCREEN_DPI, 72);
	return font;
}

bool Fonts::Application(LOGFONT& font) {
	font = AppSettings::Get().ApplicationFont();
	return font.lfFaceName[0] && font.lfHeight;
}

int Fonts::Points(LOGFONT const& font) {
	return std::max(1, ::MulDiv(std::abs(font.lfHeight), 72, USER_DEFAULT_SCREEN_DPI));
}

LOGFONT Fonts::ToDpi(LOGFONT font, UINT dpi) {
	font.lfHeight = DpiHelper::Scale(font.lfHeight, dpi, USER_DEFAULT_SCREEN_DPI);
	font.lfWidth = DpiHelper::Scale(font.lfWidth, dpi, USER_DEFAULT_SCREEN_DPI);
	return font;
}

LOGFONT Fonts::FromDpi(LOGFONT font, UINT dpi) {
	font.lfHeight = DpiHelper::Scale(font.lfHeight, USER_DEFAULT_SCREEN_DPI, dpi);
	font.lfWidth = DpiHelper::Scale(font.lfWidth, USER_DEFAULT_SCREEN_DPI, dpi);
	return font;
}

void Fonts::MarkCode(HWND hWnd) {
	::SetProp(hWnd, CodeProperty, (HANDLE)1);
	ApplyTo(hWnd, TRUE);
}

HFONT Fonts::CodeFont(HWND hWnd) {
	return code.Get(Code(), DpiHelper::GetDpi(hWnd));
}

void Fonts::Install() {
	// (out of context: the event comes to this thread's message loop, after the control is made and its window has set
	// what it sets, as the code font of a control of code)
	::SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, OnCreate, ::GetCurrentProcessId(), ::GetCurrentThreadId(),
		WINEVENT_OUTOFCONTEXT);
}

void Fonts::Apply() {
	::EnumThreadWindows(::GetCurrentThreadId(), [](HWND top, LPARAM) {
		::EnumChildWindows(top, [](HWND hWnd, LPARAM) {
			ApplyTo(hWnd, TRUE);
			return TRUE;
			}, 0);
		return TRUE;
		}, 0);
}
