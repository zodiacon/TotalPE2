#include "pch.h"
#include "HexFormat.h"

namespace {
	constexpr wchar_t HexDigits[] = L"0123456789ABCDEF";

	// "0x4D, 0x5A, ..." in rows of 16 bytes
	std::wstring ByteList(std::span<const uint8_t> data) {
		std::wstring s;
		s.reserve(data.size() * 6 + 16);
		for (size_t i = 0; i < data.size(); i++) {
			if (i % 16 == 0)
				s += L"    ";
			s += L"0x";
			s += HexDigits[data[i] >> 4];
			s += HexDigits[data[i] & 15];
			if (i + 1 < data.size())
				s += L",";
			s += (i % 16 == 15 || i + 1 == data.size()) ? L"\r\n" : L" ";
		}
		return s;
	}
}

std::wstring HexFormat::ToHexString(std::span<const uint8_t> data, bool spaced) {
	std::wstring s;
	s.reserve(data.size() * 3);
	for (size_t i = 0; i < data.size(); i++) {
		if (spaced && i)
			s += L' ';
		s += HexDigits[data[i] >> 4];
		s += HexDigits[data[i] & 15];
	}
	return s;
}

std::wstring HexFormat::ToCArray(std::span<const uint8_t> data) {
	return std::format(L"unsigned char data[{}] = {{\r\n", data.size()) + ByteList(data) + L"};";
}

std::wstring HexFormat::ToCSharpArray(std::span<const uint8_t> data) {
	return L"byte[] data = new byte[] {\r\n" + ByteList(data) + L"};";
}

std::wstring HexFormat::ToPythonBytes(std::span<const uint8_t> data) {
	if (data.empty())
		return L"data = b\"\"";
	std::wstring s = L"data = (\r\n";
	for (size_t i = 0; i < data.size(); i++) {
		if (i % 16 == 0)
			s += L"    b\"";
		s += L"\\x";
		s += (wchar_t)towlower(HexDigits[data[i] >> 4]);
		s += (wchar_t)towlower(HexDigits[data[i] & 15]);
		if (i % 16 == 15 || i + 1 == data.size())
			s += L"\"\r\n";
	}
	return s + L")";
}

std::wstring HexFormat::ToBase64(std::span<const uint8_t> data) {
	static constexpr wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::wstring s;
	s.reserve((data.size() + 2) / 3 * 4);
	for (size_t i = 0; i < data.size(); i += 3) {
		uint32_t v = data[i] << 16;
		if (i + 1 < data.size())
			v |= data[i + 1] << 8;
		if (i + 2 < data.size())
			v |= data[i + 2];
		s += alphabet[(v >> 18) & 63];
		s += alphabet[(v >> 12) & 63];
		s += i + 1 < data.size() ? alphabet[(v >> 6) & 63] : L'=';
		s += i + 2 < data.size() ? alphabet[v & 63] : L'=';
	}
	return s;
}
