#pragma once

#ifdef TOTALPE_LOGIC_TESTS
// The unit tests compile the logic sources (parsers, analysis) on their own, without WTL and the UI libraries
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wil\resource.h>
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <format>
#include <algorithm>
#include <numeric>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <span>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <cmath>
#include <ctime>
#include <WinTrust.h>
#else

// Change these values to use different versions
#define WINVER		0x0601
#define _WIN32_WINNT	0x0601
#define _WIN32_IE	0x0700
#define _RICHEDIT_VER	0x0500
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _DISABLE_VECTOR_ANNOTATION

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
#include <atlscrl.h>

#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <format>
#include <algorithm>
#include <wil\resource.h>
#include <dontuse.h>
#include <strsafe.h>
#include <cor.h>
#include <unordered_map>
#include <WinTrust.h>
#include <capstone\capstone.h>
#include <scintilla/Scintilla.h>
#include <scintilla/ILexer.h>

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif // TOTALPE_LOGIC_TESTS
