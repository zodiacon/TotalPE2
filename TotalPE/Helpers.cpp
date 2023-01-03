#include "pch.h"
#include "Helpers.h"
#include "Resource.h"

bool Helpers::ExtractResource(HMODULE hModule, UINT resId, PCWSTR type, PCWSTR path, bool overwrite) {
    auto hResource = ::FindResource(hModule, MAKEINTRESOURCE(resId), type);
    if (!hResource)
        return false;

    bool ok = false;
    auto hGlobal = ::LoadResource(hModule, hResource);
    if (hGlobal) {
        auto size = ::SizeofResource(hModule, hResource);
        if (size) {
            auto p = ::LockResource(hGlobal);
            if (p) {
                wil::unique_hfile hFile(::CreateFile(path, GENERIC_WRITE, 0, nullptr, overwrite ? OPEN_ALWAYS : CREATE_ALWAYS, 0, nullptr));
                if (hFile) {
                    DWORD written;
                    ok = ::WriteFile(hFile.get(), p, size, &written, nullptr);
                }
            }
        }
    }

    return ok;
}

bool Helpers::ExtractModules() {
    WCHAR path[MAX_PATH];
    ::GetModuleFileName(nullptr, path, _countof(path));
    *wcsrchr(path, L'\\') = 0;
    std::wstring spath(path);

    auto ok = ExtractResource(nullptr, IDR_DIA, L"BIN", (spath + L"\\msdia140.dll").c_str(), false);
    ok |= ExtractResource(nullptr, IDR_SYMSRV, L"BIN", (spath + L"\\symsrv.dll").c_str(), false);
    return ok;
}
