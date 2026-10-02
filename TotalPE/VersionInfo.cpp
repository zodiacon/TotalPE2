#include "pch.h"
#include "VersionInfo.h"

namespace {
	// One node of the tree of a version resource: {length, value length, type, key, padding, value, padding, children}.
	// Offsets are relative to the start of the resource, where the DWORD alignment is counted from.
	struct Node {
		size_t Start{ 0 }, End{ 0 };				// the whole node
		std::wstring Key;
		size_t ValueStart{ 0 }, ValueSize{ 0 };		// in bytes
		uint16_t Type{ 0 };
		size_t ChildrenStart{ 0 };
	};

	uint16_t Read16(std::span<const std::byte> d, size_t pos) {
		return (uint16_t)(std::to_integer<uint8_t>(d[pos]) | (std::to_integer<uint8_t>(d[pos + 1]) << 8));
	}

	size_t Align4(size_t x) {
		return (x + 3) & ~(size_t)3;
	}

	bool ReadNode(std::span<const std::byte> d, size_t pos, size_t limit, Node& node) {
		if (pos + 6 > limit || limit > d.size())
			return false;
		node.Start = pos;
		size_t length = Read16(d, pos);
		auto valueLength = (size_t)Read16(d, pos + 2);
		node.Type = Read16(d, pos + 4);
		if (length < 6)
			return false;
		node.End = std::min(pos + length, limit);

		// the key: a zero terminated UTF-16 string
		size_t p = pos + 6;
		node.Key.clear();
		for (;; p += 2) {
			if (p + 2 > node.End)
				return false;
			auto c = Read16(d, p);
			if (c == 0)
				break;
			node.Key += (wchar_t)c;
		}
		p += 2;
		node.ValueStart = Align4(p);
		// the length of a text value is in characters, the length of a binary value in bytes
		node.ValueSize = node.Type == 1 ? valueLength * 2 : valueLength;
		if (node.ValueStart > node.End)
			node.ValueStart = node.End;
		if (node.ValueStart + node.ValueSize > node.End)
			node.ValueSize = node.End - node.ValueStart;
		node.ChildrenStart = Align4(node.ValueStart + node.ValueSize);
		return true;
	}

	std::wstring ReadText(std::span<const std::byte> d, Node const& n) {
		std::wstring text;
		for (size_t p = n.ValueStart; p + 2 <= n.ValueStart + n.ValueSize; p += 2) {
			auto c = Read16(d, p);
			if (c == 0)
				break;
			text += (wchar_t)c;
		}
		return text;
	}

	bool ParseHex(std::wstring const& s, uint32_t& value) {
		if (s.size() != 8)
			return false;
		value = 0;
		for (auto c : s) {
			int digit = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
			if (digit < 0)
				return false;
			value = value << 4 | digit;
		}
		return true;
	}

	// calls 'f' for every child of the node
	template<typename F>
	void ForEachChild(std::span<const std::byte> d, Node const& parent, F f) {
		size_t pos = parent.ChildrenStart;
		while (pos + 6 <= parent.End) {
			Node child;
			if (!ReadNode(d, pos, parent.End, child))
				break;
			f(child);
			auto next = Align4(child.End);
			if (next <= pos)
				break;
			pos = next;
		}
	}
}

VersionInfo ParseVersionInfo(std::span<const std::byte> data) {
	VersionInfo info;
	Node root;
	if (!ReadNode(data, 0, data.size(), root) || root.Key != L"VS_VERSION_INFO")
		return info;
	info.Valid = true;

	if (root.ValueSize >= sizeof(VS_FIXEDFILEINFO)) {
		memcpy(&info.Fixed, data.data() + root.ValueStart, sizeof(VS_FIXEDFILEINFO));
		info.HasFixedInfo = info.Fixed.dwSignature == 0xFEEF04BD;
	}

	ForEachChild(data, root, [&](Node const& child) {
		if (child.Key == L"StringFileInfo") {
			ForEachChild(data, child, [&](Node const& table) {
				VersionInfo::StringTable t;
				t.LangCodePage = table.Key;
				if (uint32_t value; ParseHex(table.Key, value)) {
					t.Language = (uint16_t)(value >> 16);
					t.CodePage = (uint16_t)(value & 0xFFFF);
				}
				ForEachChild(data, table, [&](Node const& str) {
					t.Strings.push_back({ str.Key, ReadText(data, str) });
				});
				info.Tables.push_back(std::move(t));
			});
		}
		else if (child.Key == L"VarFileInfo") {
			ForEachChild(data, child, [&](Node const& var) {
				if (var.Key != L"Translation")
					return;
				for (size_t p = var.ValueStart; p + 4 <= var.ValueStart + var.ValueSize; p += 4)
					info.Translations.push_back({ Read16(data, p), Read16(data, p + 2) });
			});
		}
	});
	return info;
}

std::wstring VersionInfo::GetString(std::wstring const& name, std::wstring const& langCodePage) const {
	for (auto const& t : Tables) {
		if (!langCodePage.empty() && _wcsicmp(t.LangCodePage.c_str(), langCodePage.c_str()))
			continue;
		for (auto const& [key, value] : t.Strings)
			if (key == name)
				return value;
		if (langCodePage.empty())
			break;	// only the first table
	}
	return L"";
}

std::wstring LanguageName(uint16_t langId) {
	if (langId == 0)
		return L"Neutral";
	WCHAR name[128]{};
	if (::GetLocaleInfoW(MAKELCID(langId, SORT_DEFAULT), LOCALE_SENGLISHDISPLAYNAME, name, _countof(name)) > 0)
		return name;
	return L"";
}

std::wstring CodePageName(uint16_t codePage) {
	if (codePage == 0)
		return L"Neutral";
	if (codePage == 1200)
		return L"Unicode (1200)";
	CPINFOEXW info{};
	if (::GetCPInfoExW(codePage, 0, &info)) {
		// "1252  (ANSI - Latin I)": keep what is in the parentheses
		std::wstring name = info.CodePageName;
		if (auto open = name.find(L'('); open != std::wstring::npos && name.back() == L')')
			return name.substr(open + 1, name.size() - open - 2) + L" (" + std::to_wstring(codePage) + L")";
	}
	return std::to_wstring(codePage);
}
