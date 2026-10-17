#pragma once

#include <Settings.h>

class AppSettings : public Settings {
public:
	BEGIN_SETTINGS(AppSettings)
	SETTING(MainWindowPlacement, WINDOWPLACEMENT{}, SettingType::Binary);
	SETTING(Win32ApiWindowPlacement, WINDOWPLACEMENT{}, SettingType::Binary);
	SETTING(CodeFont, LOGFONT{}, SettingType::Binary);			// of the views of code (Fonts.h)
	SETTING(ApplicationFont, LOGFONT{}, SettingType::Binary);	// of everything else
	SETTING(AlwaysOnTop, 0, SettingType::Bool);
	SETTING(ViewToolBar, 1, SettingType::Bool);
	SETTING(ViewStatusBar, 1, SettingType::Bool);
	SETTING(TreeIconSize, 0, SettingType::Int32);
	SETTING(DarkMode, 0, SettingType::Bool);
	SETTING(SymbolServerEnabled, 0, SettingType::Bool);
	SETTING_STRING(SymbolServerUrl, L"https://msdl.microsoft.com/download/symbols");
	SETTING_STRING(SymbolCache, L"%LOCALAPPDATA%\\TotalPE\\Symbols");
	SETTING_STRING(SymbolExtraPaths, L"");
	SETTING(SymbolUseEnvPath, 1, SettingType::Bool);
	SETTING_STRING(VirusTotalApiKey, L"");	// encrypted with DPAPI: see vt::ProtectKey
	SETTING_STRING(PreferredConstants, L"");	// of the names of a value of Windows, those shown (separated by ;)
	SETTING(Win32ApiDocumentation, 1, SettingType::Bool);	// Microsoft's documentation shown in the Windows API window
	// the rest of the Windows API window's (CApiBrowser::State)
	SETTING(Win32ApiDocumentationPercent, 45, SettingType::Int32);
	SETTING(Win32ApiNavigatorWidth, 0, SettingType::Int32);
	SETTING(Win32ApiNavigatorPage, 0, SettingType::Int32);
	SETTING(Win32ApiShowMode, 0, SettingType::Int32);
	SETTING(Win32ApiActiveTab, -1, SettingType::Int32);
	SETTING_STRING(CodeColorsLight, L"");	// the colors of code changed from their defaults (CodeColors.h), of the light theme
	SETTING_STRING(CodeColorsDark, L"");	// and of the dark one
	END_SETTINGS

	DEF_SETTING(AlwaysOnTop, bool)
	DEF_SETTING(MainWindowPlacement, WINDOWPLACEMENT)
	DEF_SETTING(Win32ApiWindowPlacement, WINDOWPLACEMENT)
	DEF_SETTING(CodeFont, LOGFONT)
DEF_SETTING(ApplicationFont, LOGFONT)
	DEF_SETTING(TreeIconSize, int)
	DEF_SETTING(DarkMode, bool)
	DEF_SETTING(ViewToolBar, bool)
	DEF_SETTING(ViewStatusBar, bool)
	DEF_SETTING(SymbolServerEnabled, bool)
	DEF_SETTING_STRING(SymbolServerUrl)
	DEF_SETTING_STRING(SymbolCache)
	DEF_SETTING_STRING(SymbolExtraPaths)
	DEF_SETTING(SymbolUseEnvPath, bool)
	DEF_SETTING_STRING(VirusTotalApiKey)
	DEF_SETTING_STRING(PreferredConstants)
	DEF_SETTING(Win32ApiDocumentation, bool)
	DEF_SETTING(Win32ApiDocumentationPercent, int)
	DEF_SETTING(Win32ApiNavigatorWidth, int)
	DEF_SETTING(Win32ApiNavigatorPage, int)
	DEF_SETTING(Win32ApiShowMode, int)
	DEF_SETTING(Win32ApiActiveTab, int)
	DEF_SETTING_MULTI(Win32ApiTabs)		// the names of the tabs kept open, in their order
	DEF_SETTING_MULTI(Win32ApiFavorites)
	DEF_SETTING_MULTI(Win32ApiHistory)	// the last first
DEF_SETTING_STRING(CodeColorsLight)
DEF_SETTING_STRING(CodeColorsDark)

	static constexpr PCWSTR DefaultSymbolServerUrl = L"https://msdl.microsoft.com/download/symbols";
	static constexpr PCWSTR DefaultSymbolCache = L"%LOCALAPPDATA%\\TotalPE\\Symbols";

	// Builds the search path handed to DIA ("extra paths;_NT_SYMBOL_PATH;srv*cache*url") from the settings
	std::wstring BuildSymbolSearchPath() const;
	static std::wstring BuildSymbolSearchPath(bool useServer, std::wstring const& url, std::wstring const& cache,
		std::wstring const& extra, bool useEnv);
	DEF_SETTING_MULTI(RecentFiles)
};

