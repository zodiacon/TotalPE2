#pragma once

#include <cstdint>
#include <span>
#include <string>

// Formatters for "Copy As" in the hex view.
namespace HexFormat {
	std::wstring ToHexString(std::span<const uint8_t> data, bool spaced);
	std::wstring ToCArray(std::span<const uint8_t> data);
	std::wstring ToCSharpArray(std::span<const uint8_t> data);
	std::wstring ToPythonBytes(std::span<const uint8_t> data);
	std::wstring ToBase64(std::span<const uint8_t> data);
}
