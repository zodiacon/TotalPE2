#pragma once

#include <Settings.h>

class AppSettings : public Settings {
public:
	BEGIN_SETTINGS(AppSettings)
	SETTING(MainWindowPlacement, WINDOWPLACEMENT{}, SettingType::Binary);
	SETTING(Font, LOGFONT{}, SettingType::Binary);
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
	END_SETTINGS

	DEF_SETTING(AlwaysOnTop, bool)
	DEF_SETTING(MainWindowPlacement, WINDOWPLACEMENT)
	DEF_SETTING(Font, LOGFONT)
	DEF_SETTING(TreeIconSize, int)
	DEF_SETTING(DarkMode, bool)
	DEF_SETTING(ViewToolBar, bool)
	DEF_SETTING(ViewStatusBar, bool)
	DEF_SETTING(SymbolServerEnabled, bool)
	DEF_SETTING_STRING(SymbolServerUrl)
	DEF_SETTING_STRING(SymbolCache)
	DEF_SETTING_STRING(SymbolExtraPaths)
	DEF_SETTING(SymbolUseEnvPath, bool)

	static constexpr PCWSTR DefaultSymbolServerUrl = L"https://msdl.microsoft.com/download/symbols";
	static constexpr PCWSTR DefaultSymbolCache = L"%LOCALAPPDATA%\\TotalPE\\Symbols";

	// Builds the search path handed to DIA ("extra paths;_NT_SYMBOL_PATH;srv*cache*url") from the settings
	std::wstring BuildSymbolSearchPath() const;
	static std::wstring BuildSymbolSearchPath(bool useServer, std::wstring const& url, std::wstring const& cache,
		std::wstring const& extra, bool useEnv);
	DEF_SETTING_MULTI(RecentFiles)
};

