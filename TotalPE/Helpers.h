#pragma once

struct Helpers abstract final {
	static bool ExtractResource(HMODULE hModule, UINT resId, PCWSTR type, PCWSTR path, bool overwrite = true);
	static bool ExtractModules();
};

