#pragma once

#define WINVER		0x0A00
#define _WIN32_WINNT	0x0A00
#define _WIN32_IE	0x0A00
#define _RICHEDIT_VER	0x0500
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlstr.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlctrlx.h>
#include <atltypes.h>
#include <atlsplit.h>
#include <atltheme.h>
#include <atlgdi.h>
#include <atlcrack.h>

#include <algorithm>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <wil\resource.h>
#include <scintilla/Scintilla.h>
#include <scintilla/ILexer.h>
#include <lexilla/Lexilla.h>
#include <lexilla/SciLexer.h>

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
