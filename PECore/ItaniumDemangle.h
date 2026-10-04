#pragma once

#include <string>
#include <string_view>

// Demangles a C++ name in the Itanium ABI (GCC, Clang: Linux, the BSDs, macOS...), as GNU c++filt prints it:
// "_ZNSt6vectorIiSaIiEE9push_backERKi" -> "std::vector<int, std::allocator<int> >::push_back(int const&)".
// Returns an empty string for a name that is not mangled, or that uses what the demangler does not know.
std::string DemangleItanium(std::string_view mangled);
