#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX                        // Prevent Windows min/max macros from conflicting with std::min/max

#include <Windows.h>
#include <WinTrust.h>
#include <delayimp.h>
#include <string>
#include <vector>
#include <memory>
#include <wil\resource.h>
#include <string_view>
#include <unordered_map>
#include <span>
#include <format>
#include <algorithm>
#include <numeric>
#include <functional>
#include <unordered_set>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <cmath>
#include <ctime>
