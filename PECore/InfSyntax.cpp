#include "pch.h"
#include "InfSyntax.h"
#include <algorithm>
#include <cctype>

namespace {
	bool IsWordChar(char ch) {
		return isalnum((uint8_t)ch) || ch == '_' || ch == '.' || ch == '!' || ch == '-' || ch == '$' || ch == '\\';
	}

	bool IsNumber(std::string_view word) {
		if (word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X'))
			return std::all_of(word.begin() + 2, word.end(), [](char c) { return isxdigit((uint8_t)c) != 0; });
		return std::all_of(word.begin(), word.end(), [](char c) { return isdigit((uint8_t)c) != 0; });
	}

	bool IsRootKey(std::string_view word) {
		for (auto key : { "HKLM", "HKCU", "HKCR", "HKU", "HKCC", "HKR" })
			if (word.size() == strlen(key) && std::equal(word.begin(), word.end(), key, [](char a, char b) { return toupper((uint8_t)a) == b; }))
				return true;
		return false;
	}

	void Fill(std::vector<InfStyle>& styles, size_t from, size_t to, InfStyle style) {
		std::fill(styles.begin() + from, styles.begin() + std::min(to, styles.size()), style);
	}

	// %Name% from a %: the position after the closing %, or from if there is none
	size_t StringKeyEnd(std::string_view line, size_t from, size_t end) {
		auto close = line.find('%', from + 1);
		return close == std::string_view::npos || close >= end || close == from + 1 ? from : close + 1;
	}
}

std::vector<InfStyle> InfLineStyles(std::string_view line) {
	std::vector<InfStyle> styles(line.size(), InfStyle::Default);
	auto end = line.find_first_of("\r\n");
	if (end == std::string_view::npos)
		end = line.size();

	auto i = line.find_first_not_of(" \t");
	if (i == std::string_view::npos || i >= end)
		return styles;

	if (line[i] == '[') {
		auto close = line.find(']', i);
		auto sectionEnd = close == std::string_view::npos || close >= end ? end : close + 1;
		Fill(styles, i, sectionEnd, InfStyle::Section);
		if (auto comment = line.find(';', sectionEnd); comment < end)
			Fill(styles, comment, end, InfStyle::Comment);
		return styles;
	}

	// the key of key=value: the text before an = that comes before any string, field or comment
	if (auto eq = line.find_first_of("=\",;", i); eq < end && line[eq] == '=') {
		auto keyEnd = line.find_last_not_of(" \t", eq - 1);
		if (keyEnd != std::string_view::npos && keyEnd >= i)
			Fill(styles, i, keyEnd + 1, InfStyle::Key);
		styles[eq] = InfStyle::Operator;
		i = eq + 1;
	}

	bool firstField = true;		// a root key is the first field of a line
	while (i < end) {
		auto ch = line[i];
		if (ch == ';') {
			Fill(styles, i, end, InfStyle::Comment);
			break;
		}
		if (ch == '"') {
			auto j = i + 1;
			while (j < end) {
				if (line[j] == '"') {
					if (j + 1 < end && line[j + 1] == '"') {
						j += 2;
						continue;
					}
					j++;
					break;
				}
				j++;
			}
			Fill(styles, i, j, InfStyle::String);
			for (auto k = i + 1; k < j; k++)
				if (line[k] == '%')
					if (auto keyEnd = StringKeyEnd(line, k, j); keyEnd != k) {
						Fill(styles, k, keyEnd, InfStyle::StringKey);
						k = keyEnd - 1;
					}
			i = j;
			firstField = false;
			continue;
		}
		if (ch == '%') {
			if (auto keyEnd = StringKeyEnd(line, i, end); keyEnd != i) {
				Fill(styles, i, keyEnd, InfStyle::StringKey);
				i = keyEnd;
				firstField = false;
				continue;
			}
		}
		if (ch == ',' || ch == '=') {
			styles[i] = InfStyle::Operator;
			firstField &= ch != ',';
			i++;
			continue;
		}
		if (IsWordChar(ch)) {
			auto j = i;
			while (j < end && IsWordChar(line[j]))
				j++;
			auto word = line.substr(i, j - i);
			if (IsNumber(word))
				Fill(styles, i, j, InfStyle::Number);
			else if (firstField && IsRootKey(word))
				Fill(styles, i, j, InfStyle::RootKey);
			i = j;
			firstField = false;
			continue;
		}
		i++;
	}
	return styles;
}
