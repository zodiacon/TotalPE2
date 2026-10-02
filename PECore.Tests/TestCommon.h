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

#include "SyntheticPE.h"
