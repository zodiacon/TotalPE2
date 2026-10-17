#pragma once

#include <charconv>

//
// Numbers as the user types them: decimal (with a sign) or hex (0x)
//
namespace Values {
	inline std::wstring Trimmed(std::wstring const& text) {
		auto first = text.find_first_not_of(L" \t");
		if (first == std::wstring::npos)
			return {};
		return text.substr(first, text.find_last_not_of(L" \t") - first + 1);
	}

	// a number (not a name with digits); a negative one as its bits (-1: 0xFFFFFFFFFFFFFFFF)
	inline std::optional<uint64_t> Parse(std::wstring const& text) {
		std::string s(CW2A(Trimmed(text).c_str()));
		bool negative = s.starts_with('-');
		if (negative)
			s = s.substr(1);
		int base = 10;
		if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
			base = 16;
			s = s.substr(2);
		}
		uint64_t value = 0;
		if (s.empty())
			return std::nullopt;
		auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value, base);
		if (error != std::errc() || end != s.data() + s.size())
			return std::nullopt;
		return negative ? (uint64_t)-(int64_t)value : value;
	}
}
