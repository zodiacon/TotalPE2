// Win32ApiExplorer.cpp : the functions, types and constants of Windows, from the Win32 metadata
//
// Win32ApiExplorer.exe [name]: with a name, that function, type or constant is opened

#include "pch.h"
#include "resource.h"
#include "MainFrm.h"
#include "AppSettings.h"
#include <WTLHelper.h>

CAppModule _Module;
AppSettings g_Settings;

extern "C" int Scintilla_RegisterClasses(void* hInstance);

#pragma comment(lib, "imm32")
// (of a library vcpkg links with all: mbedcrypto)
#pragma comment(lib, "bcrypt")

namespace {
	// the name on the command line (quotes and spaces around it removed)
	std::wstring StartName(PCWSTR cmdLine) {
		std::wstring name = cmdLine ? cmdLine : L"";
		auto first = name.find_first_not_of(L" \t\"");
		if (first == std::wstring::npos)
			return {};
		auto last = name.find_last_not_of(L" \t\"");
		return name.substr(first, last - first + 1);
	}
}

int Run(PCWSTR cmdLine, int nCmdShow) {
	CMessageLoop loop;
	_Module.AddMessageLoop(&loop);

	CMainFrame frame(StartName(cmdLine));
	if (frame.CreateEx() == nullptr) {
		_Module.RemoveMessageLoop();
		return 0;
	}
	// where it was (not minimized)
	auto wp = AppSettings::Get().MainWindowPlacement();
	if (wp.length == sizeof(wp)) {
		if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE)
			wp.showCmd = SW_SHOWNORMAL;
		frame.SetWindowPlacement(&wp);
	}
	else
		frame.ShowWindow(nCmdShow);

	int result = loop.Run();
	_Module.RemoveMessageLoop();
	return result;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR cmdLine, int nCmdShow) {
	// (WebView2 needs an STA)
	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	ATLASSERT(SUCCEEDED(hr));

	AtlInitCommonControls(ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES);

	auto& settings = AppSettings::Get();
	settings.LoadFromKey(L"SOFTWARE\\ScorpioSoftware\\Win32ApiExplorer");

	hr = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hr));

	Scintilla_RegisterClasses(hInstance);
	WTLHelper::InitDarkMode(settings.DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light);

	int result = Run(cmdLine, nCmdShow);
	settings.Save();

	_Module.Term();
	::CoUninitialize();
	return result;
}
