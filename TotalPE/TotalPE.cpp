// TotalPE.cpp : main source file for TotalPE.exe
//

#include "pch.h"
#include "resource.h"
#include "MainFrm.h"
#include "Helpers.h"
#include "AppSettings.h"
#include "Win32Api.h"
#include <WTLHelper.h>
#include "DecompilerThread.h"

CAppModule _Module;
AppSettings g_Settings;

//#ifdef _DEBUG
//#pragma comment(lib, "../External/Capstone/Capstoned.lib")
//#else
//#pragma comment(lib, "../External/Capstone/Capstone.lib")
//#endif

extern "C" int Scintilla_RegisterClasses(void* hInstance);

#pragma comment(lib, "imm32")

int Run(LPCTSTR /*lpstrCmdLine*/ = nullptr, int nCmdShow = SW_SHOWDEFAULT) {
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	auto wndMain = new CMainFrame ;
	if (wndMain->CreateEx() == nullptr) {
		ATLTRACE(_T("Main window creation failed!\n"));
		return 0;
	}

	wndMain->ShowWindow(nCmdShow);

	int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();
	return nRet;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow) {
	HRESULT hRes = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	ATLASSERT(SUCCEEDED(hRes));

	AtlInitCommonControls(ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES);

	auto& settings = AppSettings::Get();
	settings.LoadFromKey(L"SOFTWARE\\ScorpioSoftware\\TotalPE");
	// of the names of a value of Windows, those the user chose to see
	{
		std::vector<std::string> names;
		std::wstring text = settings.PreferredConstants();
		for (size_t start = 0, end; start < text.size(); start = end + 1) {
			end = text.find(L';', start);
			if (end == std::wstring::npos)
				end = text.size();
			if (end > start)
				names.emplace_back(CW2A(text.substr(start, end - start).c_str()));
		}
		Win32Api::SetPreferredNames(std::move(names));
	}

	hRes = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	Helpers::ExtractModules();

	Scintilla_RegisterClasses(hInstance);

	WTLHelper::InitDarkMode(settings.DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light);

	int nRet = Run(lpstrCmdLine, nCmdShow);
	settings.Save();

	// a function that is still decompiled (the decompiler cannot be stopped, and its objects go with the process's): the
	// process ends without it
	if (!DecompilerThread::Shutdown(2000))
		::ExitProcess(nRet);

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
