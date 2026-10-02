#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <wil\resource.h>
#include <functional>
#include <PEFile.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_approx.hpp>

#include "SyntheticPE.h"

// INFO() streams into a narrow stream, which has no operator<< for std::wstring
inline std::string Narrow(std::wstring const& s) {
	int len = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
	std::string result(len, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), len, nullptr, nullptr);
	return result;
}
