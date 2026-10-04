#include "pch.h"
#include "FileStrings.h"
#include <algorithm>

namespace {
	bool IsPrintable(uint8_t b) {
		return (b >= 0x20 && b < 0x7F) || b == '\t';
	}

	void FindAscii(std::span<const std::byte> data, uint32_t minLength, std::vector<FoundString>& result) {
		size_t const size = data.size();
		size_t i = 0;
		while (i < size) {
			if (!IsPrintable(std::to_integer<uint8_t>(data[i]))) {
				i++;
				continue;
			}
			size_t start = i;
			while (i < size && IsPrintable(std::to_integer<uint8_t>(data[i])))
				i++;
			if (i - start >= minLength) {
				auto length = (uint32_t)(i - start);
				std::wstring text;
				text.reserve(std::min(length, StringScanOptions::MaxTextLength));
				for (size_t j = start; j < start + std::min(length, StringScanOptions::MaxTextLength); j++)
					text += (wchar_t)std::to_integer<uint8_t>(data[j]);
				result.push_back({ (uint32_t)start, length, StringEncoding::Ascii, std::move(text) });
			}
		}
	}

	void FindUtf16(std::span<const std::byte> data, uint32_t minLength, std::vector<FoundString>& result) {
		auto isChar = [&](size_t i) {
			return i + 1 < data.size() && IsPrintable(std::to_integer<uint8_t>(data[i])) && data[i + 1] == std::byte{ 0 };
		};
		size_t const size = data.size();
		size_t i = 0;
		while (i < size) {
			if (!isChar(i)) {
				i++;
				continue;
			}
			size_t start = i;
			while (isChar(i))
				i += 2;
			auto length = (uint32_t)((i - start) / 2);
			// the odd bytes of the run are zeros, so no string at the other alignment starts inside it
			if (length >= minLength) {
				std::wstring text;
				text.reserve(std::min(length, StringScanOptions::MaxTextLength));
				for (size_t j = 0; j < std::min(length, StringScanOptions::MaxTextLength); j++)
					text += (wchar_t)std::to_integer<uint8_t>(data[start + j * 2]);
				result.push_back({ (uint32_t)start, length, StringEncoding::Utf16, std::move(text) });
			}
		}
	}
}

const wchar_t* StringEncodingToString(StringEncoding encoding) {
	switch (encoding) {
		case StringEncoding::Ascii: return L"ASCII";
		case StringEncoding::Utf16: return L"UTF-16";
	}
	return L"";
}

std::vector<FoundString> FindStrings(std::span<const std::byte> data, StringScanOptions const& options) {
	std::vector<FoundString> result;
	auto minLength = std::max(options.MinLength, 1u);
	if (options.Ascii)
		FindAscii(data, minLength, result);
	if (options.Utf16)
		FindUtf16(data, minLength, result);
	std::stable_sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.Offset < b.Offset; });
	return result;
}
