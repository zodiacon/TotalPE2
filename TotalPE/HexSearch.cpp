#include "pch.h"
#include "HexSearch.h"

namespace {
	bool IsLetter(uint8_t b) {
		return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
	}

	int NibbleValue(wchar_t c) {
		if (c >= L'0' && c <= L'9')
			return c - L'0';
		if (c >= L'a' && c <= L'f')
			return c - L'a' + 10;
		if (c >= L'A' && c <= L'F')
			return c - L'A' + 10;
		return -1;
	}

	inline bool Matches(const uint8_t* p, HexPattern const& pat, size_t n) {
		for (size_t i = 0; i < n; i++) {
			if (((p[i] ^ pat.Bytes[i]) & pat.Mask[i]) == 0)
				continue;
			if (pat.Fold[i] && (p[i] | 0x20) == (pat.Bytes[i] | 0x20))
				continue;
			return false;
		}
		return true;
	}
}

bool HexSearch::BuildPattern(HexFindOptions const& options, HexPattern& pattern, std::wstring& error) {
	pattern = {};
	if (options.Text.empty()) {
		error = L"Enter something to search for.";
		return false;
	}

	if (options.Type == HexSearchType::Hex) {
		// drop "0x" prefixes, whitespace and commas
		std::wstring text;
		for (size_t i = 0; i < options.Text.size(); i++) {
			auto c = options.Text[i];
			if (c == L' ' || c == L'\t' || c == L',' || c == L'\r' || c == L'\n')
				continue;
			if (c == L'0' && i + 1 < options.Text.size() && (options.Text[i + 1] == L'x' || options.Text[i + 1] == L'X')) {
				i++;
				continue;
			}
			text += c;
		}

		bool fixedNibble = false;
		std::vector<int> nibbles;	// -1 = wildcard
		for (auto c : text) {
			if (c == L'?') {
				nibbles.push_back(-1);
				continue;
			}
			auto v = NibbleValue(c);
			if (v < 0) {
				error = std::format(L"'{}' is not a valid hex digit. Use hex bytes such as \"4D 5A ?? 90\" (?? matches any byte).", c);
				return false;
			}
			nibbles.push_back(v);
			fixedNibble = true;
		}
		if (nibbles.size() % 2) {
			error = L"A hex pattern must have an even number of digits.";
			return false;
		}
		if (!fixedNibble) {
			error = L"The pattern must contain at least one non-wildcard digit.";
			return false;
		}
		for (size_t i = 0; i < nibbles.size(); i += 2) {
			int hi = nibbles[i], lo = nibbles[i + 1];
			pattern.Bytes.push_back((uint8_t)(((hi < 0 ? 0 : hi) << 4) | (lo < 0 ? 0 : lo)));
			pattern.Mask.push_back((uint8_t)((hi < 0 ? 0 : 0xF0) | (lo < 0 ? 0 : 0x0F)));
			pattern.Fold.push_back(0);
		}
		return true;
	}

	if (options.Type == HexSearchType::Ascii) {
		int len = ::WideCharToMultiByte(CP_UTF8, 0, options.Text.c_str(), (int)options.Text.size(), nullptr, 0, nullptr, nullptr);
		pattern.Bytes.resize(len);
		::WideCharToMultiByte(CP_UTF8, 0, options.Text.c_str(), (int)options.Text.size(), (PSTR)pattern.Bytes.data(), len, nullptr, nullptr);
		pattern.Mask.assign(len, 0xFF);
		for (auto b : pattern.Bytes)
			pattern.Fold.push_back(!options.MatchCase && IsLetter(b) ? 1 : 0);
	}
	else {
		for (auto c : options.Text) {
			pattern.Bytes.push_back((uint8_t)(c & 0xFF));
			pattern.Bytes.push_back((uint8_t)(c >> 8));
			// only the low byte of an ASCII code unit is a letter
			pattern.Fold.push_back(!options.MatchCase && c < 0x80 && IsLetter((uint8_t)c) ? 1 : 0);
			pattern.Fold.push_back(0);
		}
		pattern.Mask.assign(pattern.Bytes.size(), 0xFF);
	}
	return true;
}

int64_t HexSearch::Find(const uint8_t* data, int64_t size, HexPattern const& pattern, int64_t start, bool forward) {
	auto n = (int64_t)pattern.Bytes.size();
	if (!data || n == 0 || size < n)
		return -1;

	auto last = size - n;
	if (forward) {
		if (start < 0)
			start = 0;
		// use a fully specified, case-sensitive byte as an anchor so memchr can skip ahead
		int64_t anchor = -1;
		for (int64_t i = 0; i < n; i++)
			if (pattern.Mask[i] == 0xFF && !pattern.Fold[i]) {
				anchor = i;
				break;
			}

		for (int64_t p = start; p <= last; ) {
			if (anchor >= 0) {
				auto hit = (const uint8_t*)memchr(data + p + anchor, pattern.Bytes[anchor], (size_t)(last - p + 1));
				if (!hit)
					return -1;
				p = hit - data - anchor;
			}
			if (Matches(data + p, pattern, (size_t)n))
				return p;
			p++;
		}
		return -1;
	}

	for (int64_t p = std::min(start, last); p >= 0; p--)
		if (Matches(data + p, pattern, (size_t)n))
			return p;
	return -1;
}
