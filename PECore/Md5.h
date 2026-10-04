#pragma once

#include <cstddef>
#include <string>

// MD5 (RFC 1321) as a lowercase hexadecimal string. Used for the import hash, where the algorithm is fixed by convention.
std::string Md5Hex(const void* data, size_t size);
std::string Md5Hex(std::string const& text);
