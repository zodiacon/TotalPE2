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
	// The command line: [/new] [name]. /new: a window of its own (else the name goes to the window there is, if there is
	// one); the name: what is opened
	struct CommandLine {
		bool New{ false };
		std::wstring Name;
	};
	CommandLine ParseCommandLine(PCWSTR cmdLine) {
		CommandLine result;
		int count = 0;
		auto args = ::CommandLineToArgvW(cmdLine && *cmdLine ? cmdLine : L"", &count);
		// (CommandLineToArgvW of an empty line: the executable's path, which is not an argument here)
		for (int i = (cmdLine && *cmdLine) ? 0 : count; i < count; i++) {
			std::wstring arg = args[i];
			if (_wcsicmp(arg.c_str(), L"/new") == 0 || _wcsicmp(arg.c_str(), L"-new") == 0)
				result.New = true;
			else if (result.Name.empty())
				result.Name = arg;
		}
		::LocalFree(args);
		return result;
	}

	// The window of another instance: the name to it, and it to the front; false if there is none
	bool SendToRunning(std::wstring const& name) {
		auto hWnd = ::FindWindow(CMainFrame::GetWndClassInfo().m_wc.lpszClassName, nullptr);
		if (!hWnd)
			return false;
		COPYDATASTRUCT data{ CMainFrame::CopyDataName, (DWORD)(name.size() * sizeof(WCHAR)), (PVOID)name.c_str() };
		DWORD_PTR result = 0;
		// (it may bring itself to the front: this one is in front, it can let it)
		DWORD pid = 0;
		::GetWindowThreadProcessId(hWnd, &pid);
		::AllowSetForegroundWindow(pid);
		return ::SendMessageTimeout(hWnd, WM_COPYDATA, 0, (LPARAM)&data, SMTO_ABORTIFHUNG, 5000, &result) && result;
	}
}

int Run(std::wstring startName, int nCmdShow) {
	CMessageLoop loop;
	_Module.AddMessageLoop(&loop);

	CMainFrame frame(std::move(startName));
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
	// one window, unless asked for another: the name goes to the one there is
	auto commandLine = ParseCommandLine(cmdLine);
	if (!commandLine.New && SendToRunning(commandLine.Name))
		return 0;

	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	ATLASSERT(SUCCEEDED(hr));

	AtlInitCommonControls(ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES);

	auto& settings = AppSettings::Get();
	settings.LoadFromKey(L"SOFTWARE\\ScorpioSoftware\\Win32ApiExplorer");

	hr = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hr));

	Scintilla_RegisterClasses(hInstance);
	WTLHelper::InitDarkMode(settings.DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light);

	int result = Run(std::move(commandLine.Name), nCmdShow);
	settings.Save();

	_Module.Term();
	::CoUninitialize();
	return result;
}
