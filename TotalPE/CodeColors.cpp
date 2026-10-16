#include "pch.h"
#include "CodeColors.h"
#include "AppSettings.h"

namespace {
	CodeColorElement const elements[] = {
		{ "Common.Related", L"All", L"Related line (side by side)", RGB(255, 240, 170), RGB(70, 60, 20) },
		{ "Common.Bookmark", L"All", L"Bookmark", RGB(60, 130, 230), RGB(60, 130, 230) },

		{ "Asm.Instruction", L"Disassembly", L"Instruction", RGB(160, 0, 0), RGB(240, 0, 0) },
		{ "Asm.Register", L"Disassembly", L"Register", RGB(255, 128, 0), RGB(128, 128, 0) },
		{ "Asm.Number", L"Disassembly", L"Number", RGB(0, 0, 255), RGB(0, 255, 255) },
		{ "Asm.String", L"Disassembly", L"String", RGB(128, 0, 64), RGB(128, 0, 128) },
		{ "Asm.Comment", L"Disassembly", L"Comment", RGB(0, 128, 0), RGB(0, 128, 0) },
		{ "Asm.Directive", L"Disassembly", L"Directive", RGB(128, 128, 128), RGB(128, 128, 128) },

		{ "C.Keyword", L"C/C++/C#/IDL", L"Keyword", RGB(0, 0, 255), RGB(86, 156, 214) },
		{ "C.Type", L"C/C++/C#/IDL", L"Type", RGB(43, 145, 175), RGB(78, 201, 176) },
		{ "C.Number", L"C/C++/C#/IDL", L"Number", RGB(9, 134, 88), RGB(181, 206, 168) },
		{ "C.String", L"C/C++/C#/IDL", L"String", RGB(163, 21, 21), RGB(214, 157, 133) },
		{ "C.Comment", L"C/C++/C#/IDL", L"Comment", RGB(0, 128, 0), RGB(87, 166, 74) },
		{ "C.Preprocessor", L"C/C++/C#/IDL", L"Preprocessor", RGB(128, 128, 128), RGB(128, 128, 128) },
		{ "C.Operator", L"C/C++/C#/IDL", L"Operator", RGB(64, 64, 64), RGB(180, 180, 180) },
		{ "C.Guid", L"C/C++/C#/IDL", L"GUID (IDL)", RGB(9, 134, 88), RGB(181, 206, 168) },

		{ "IL.Opcode", L"IL", L"Opcode", RGB(160, 0, 0), RGB(240, 100, 100) },
		{ "IL.Keyword", L"IL", L"Keyword", RGB(0, 0, 255), RGB(86, 156, 214) },
		{ "IL.Type", L"IL", L"Type", RGB(43, 145, 175), RGB(78, 201, 176) },
		{ "IL.Directive", L"IL", L"Directive", RGB(128, 0, 128), RGB(200, 120, 255), true },
		{ "IL.Number", L"IL", L"Number", RGB(9, 134, 88), RGB(181, 206, 168) },
		{ "IL.String", L"IL", L"String", RGB(163, 21, 21), RGB(214, 157, 133) },
		{ "IL.Label", L"IL", L"Label", RGB(128, 128, 128), RGB(128, 128, 128) },
		{ "IL.Comment", L"IL", L"Comment", RGB(0, 128, 0), RGB(87, 166, 74) },

		{ "Xml.Tag", L"XML/HTML", L"Tag", RGB(0, 0, 240), RGB(0, 128, 255) },
		{ "Xml.Attribute", L"XML/HTML", L"Attribute", RGB(128, 0, 0), RGB(240, 128, 128) },

		{ "Inf.Section", L"INF", L"Section", RGB(0, 0, 240), RGB(0, 128, 255), true },
		{ "Inf.Key", L"INF", L"Key", RGB(128, 0, 0), RGB(240, 128, 128) },
		{ "Inf.String", L"INF", L"String", RGB(163, 21, 21), RGB(214, 157, 133) },
		{ "Inf.StringKey", L"INF", L"String key (%name%)", RGB(128, 0, 128), RGB(200, 120, 255) },
		{ "Inf.Number", L"INF", L"Number", RGB(0, 0, 255), RGB(0, 255, 255) },
		{ "Inf.RootKey", L"INF", L"Registry root key", RGB(192, 96, 0), RGB(255, 160, 0), true },
		{ "Inf.Operator", L"INF", L"Operator", RGB(128, 128, 128), RGB(128, 128, 128) },
		{ "Inf.Comment", L"INF", L"Comment", RGB(0, 128, 0), RGB(0, 128, 0) },
	};

	CodeColorElement const* Find(std::string_view key) {
		for (auto const& e : elements)
			if (key == e.Key)
				return &e;
		return nullptr;
	}

	// "Asm.Comment=008000;C.Keyword=0000FF,b" (RRGGBB; ",b": bold)
	std::map<std::string, CodeColor> Parse(std::wstring const& text) {
		std::map<std::string, CodeColor> result;
		size_t start = 0;
		while (start < text.size()) {
			auto end = text.find(L';', start);
			auto item = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
			start = end == std::wstring::npos ? text.size() : end + 1;
			auto equal = item.find(L'=');
			if (equal == std::wstring::npos || item.size() < equal + 7)
				continue;
			std::string key(CW2A(item.substr(0, equal).c_str()));
			if (!Find(key))
				continue;
			auto rgb = wcstoul(item.substr(equal + 1, 6).c_str(), nullptr, 16);
			result[key] = { RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF), item.substr(equal + 7) == L",b" };
		}
		return result;
	}

	std::wstring Format(std::map<std::string, CodeColor> const& colors) {
		std::wstring text;
		for (auto const& [key, c] : colors)
			text += std::format(L"{}{}={:02X}{:02X}{:02X}{}", text.empty() ? L"" : L";", std::wstring(CA2W(key.c_str())),
				GetRValue(c.Color), GetGValue(c.Color), GetBValue(c.Color), c.Bold ? L",b" : L"");
		return text;
	}

	// (the settings' text read once, until it changes)
	std::map<std::string, CodeColor> const& Cached(bool dark) {
		static std::wstring text[2];
		static std::map<std::string, CodeColor> colors[2];
		auto& s = AppSettings::Get();
		auto current = dark ? s.CodeColorsDark() : s.CodeColorsLight();
		if (current != text[dark]) {
			text[dark] = current;
			colors[dark] = Parse(current);
		}
		return colors[dark];
	}
}

std::span<CodeColorElement const> CodeColors::Elements() {
	return elements;
}

CodeColor CodeColors::Default(CodeColorElement const& element, bool dark) {
	return { dark ? element.Dark : element.Light, element.Bold };
}

CodeColor CodeColors::Get(char const* key, bool dark) {
	auto const& changed = Cached(dark);
	if (auto it = changed.find(key); it != changed.end())
		return it->second;
	auto element = Find(key);
	ATLASSERT(element);
	return element ? Default(*element, dark) : CodeColor{ RGB(0, 0, 0), false };
}

std::map<std::string, CodeColor> CodeColors::Changed(bool dark) {
	return Cached(dark);
}

void CodeColors::SetChanged(bool dark, std::map<std::string, CodeColor> const& changed) {
	// (only those that differ from their defaults)
	std::map<std::string, CodeColor> kept;
	for (auto const& [key, c] : changed)
		if (auto element = Find(key); element && c != Default(*element, dark))
			kept[key] = c;
	auto& s = AppSettings::Get();
	if (dark)
		s.CodeColorsDark(Format(kept));
	else
		s.CodeColorsLight(Format(kept));
}
