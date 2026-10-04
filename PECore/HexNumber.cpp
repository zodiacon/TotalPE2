#include "pch.h"
#include "HexNumber.h"

bool ParseHexNumber(std::wstring const& input, uint64_t& value) {
	auto first = input.find_first_not_of(L" \t");
	if (first == std::wstring::npos)
		return false;
	auto text = input.substr(first, input.find_last_not_of(L" \t") - first + 1);

	if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X'))
		text = text.substr(2);
	else if (text.size() > 1 && (text.back() == L'h' || text.back() == L'H'))
		text.pop_back();
	// the digits only: wcstoull would also accept a sign or leading white space
	if (text.empty() || text.size() > 16 || !std::all_of(text.begin(), text.end(), [](wchar_t c) { return iswxdigit(c) != 0; }))
		return false;

	value = wcstoull(text.c_str(), nullptr, 16);
	return true;
}
