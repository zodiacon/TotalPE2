#pragma once

#include <Settings.h>

class AppSettings : public Settings {
public:
	BEGIN_SETTINGS(AppSettings)
	SETTING(MainWindowPlacement, WINDOWPLACEMENT{}, SettingType::Binary);
	SETTING(DarkMode, 0, SettingType::Bool);
	SETTING(ShowDocumentation, 1, SettingType::Bool);
	SETTING(Machine, -1, SettingType::Int32);			// Win32Api::Machine (-1: this computer's)
	SETTING(NavigatorWidth, 0, SettingType::Int32);		// in pixels (0: the default)
	SETTING(DocumentationPercent, 45, SettingType::Int32);	// of the height of the right side
	SETTING(NavigatorPage, 0, SettingType::Int32);		// 0: Search, 1: Namespaces
	SETTING(ActiveTab, -1, SettingType::Int32);
	SETTING(CodeFont, LOGFONT{}, SettingType::Binary);	// of the declarations (no name: the default)
	SETTING(UIFont, LOGFONT{}, SettingType::Binary);
	SETTING(ShowMode, 0, SettingType::Int32);		// of structs: CDeclarationView::Mode	// of the rest: lists, tree, tabs (no name: the default)
	END_SETTINGS

	DEF_SETTING(MainWindowPlacement, WINDOWPLACEMENT)
	DEF_SETTING(DarkMode, bool)
	DEF_SETTING(ShowDocumentation, bool)
	DEF_SETTING(Machine, int)
	DEF_SETTING(NavigatorWidth, int)
	DEF_SETTING(DocumentationPercent, int)
	DEF_SETTING(NavigatorPage, int)
	DEF_SETTING(ActiveTab, int)
	DEF_SETTING(CodeFont, LOGFONT)
	DEF_SETTING(UIFont, LOGFONT)
	DEF_SETTING(ShowMode, int)
	DEF_SETTING_MULTI(OpenTabs)		// the names of the tabs kept open, in their order
};
