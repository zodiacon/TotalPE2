#include "pch.h"
#include "ResourceContent.h"
#include <algorithm>

namespace {
	using Bytes = std::span<const std::byte>;

	// the resource types that have a number (winuser.h has them as pointers)
	enum : uint16_t {
		TypeCursor = 1, TypeBitmap = 2, TypeIcon = 3, TypeMenu = 4, TypeDialog = 5, TypeString = 6, TypeFontDir = 7, TypeFont = 8,
		TypeAccelerator = 9, TypeRcData = 10, TypeMessageTable = 11, TypeGroupCursor = 12, TypeGroupIcon = 14, TypeVersion = 16,
		TypeAniCursor = 21, TypeAniIcon = 22, TypeHtml = 23, TypeManifest = 24,
	};

	uint8_t At(Bytes d, size_t i) {
		return i < d.size() ? std::to_integer<uint8_t>(d[i]) : 0;
	}
	uint16_t Le16(Bytes d, size_t i) { return (uint16_t)(At(d, i) | (At(d, i + 1) << 8)); }
	uint32_t Le32(Bytes d, size_t i) { return Le16(d, i) | ((uint32_t)Le16(d, i + 2) << 16); }
	uint16_t Be16(Bytes d, size_t i) { return (uint16_t)((At(d, i) << 8) | At(d, i + 1)); }
	uint32_t Be32(Bytes d, size_t i) { return ((uint32_t)Be16(d, i) << 16) | Be16(d, i + 2); }

	bool Starts(Bytes d, std::initializer_list<uint8_t> magic) {
		if (d.size() < magic.size())
			return false;
		size_t i = 0;
		for (auto b : magic)
			if (At(d, i++) != b)
				return false;
		return true;
	}

	bool IEquals(std::wstring_view a, std::wstring_view b) {
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return towupper(x) == towupper(y); });
	}

	bool IsSfnt(Bytes d) {
		return Starts(d, { 0, 1, 0, 0 }) || Starts(d, { 'O', 'T', 'T', 'O' }) || Starts(d, { 't', 'r', 'u', 'e' }) || Starts(d, { 't', 't', 'c', 'f' });
	}

	bool IsBitmapFile(Bytes d) {
		return d.size() >= 26 && Starts(d, { 'B', 'M' }) && Le32(d, 10) < d.size() && Le32(d, 14) >= 12 && Le32(d, 14) <= 124;
	}

	// a PE file: "MZ" and a "PE\0\0" where the DOS header says it is
	bool IsExecutable(Bytes d) {
		if (!Starts(d, { 'M', 'Z' }) || d.size() < 0x40)
			return false;
		auto lfanew = Le32(d, 0x3C);
		return lfanew + 24 <= d.size() && Le32(d, lfanew) == 0x00004550;
	}

	bool IsUtf16(Bytes d) {
		if (Starts(d, { 0xFF, 0xFE }))
			return true;
		// ASCII text in UTF-16: the second byte of every character is zero
		size_t n = std::min<size_t>(d.size() & ~1, 128);
		if (n < 8)
			return false;
		for (size_t i = 0; i < n; i += 2)
			if (At(d, i + 1) != 0 || At(d, i) == 0)
				return false;
		return true;
	}

	// text: no control characters besides white space, in the first part
	bool LooksLikeText(std::string_view utf8) {
		size_t n = std::min<size_t>(utf8.size(), 4096);
		if (n == 0)
			return false;
		size_t bad = 0;
		for (size_t i = 0; i < n; i++) {
			auto c = (uint8_t)utf8[i];
			if (c < 0x20 && c != '\r' && c != '\n' && c != '\t' && c != '\f')
				bad++;
		}
		return bad == 0 || (bad * 100 / n < 1 && utf8.find('\0') == std::string_view::npos);
	}

	std::string_view TrimStart(std::string_view s) {
		while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '\n'))
			s.remove_prefix(1);
		return s;
	}

	bool IStarts(std::string_view s, std::string_view prefix) {
		return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin(), [](char a, char b) { return toupper((uint8_t)a) == toupper((uint8_t)b); });
	}

	bool IContains(std::string_view s, std::string_view what) {
		return std::search(s.begin(), s.end(), what.begin(), what.end(), [](char a, char b) { return toupper((uint8_t)a) == toupper((uint8_t)b); }) != s.end();
	}

	bool LooksLikeRegistryScript(std::string_view text) {
		auto t = TrimStart(text);
		for (auto key : { "HKCR", "HKLM", "HKCU", "HKU", "HKEY_", "NoRemove", "ForceRemove" })
			if (IStarts(t, key))
				return true;
		return false;
	}

	ResourceContent DetectText(Bytes data) {
		auto text = ResourceTextToUtf8(data.subspan(0, std::min<size_t>(data.size(), 8192)));
		if (!LooksLikeText(text))
			return ResourceContent::Binary;
		auto t = TrimStart(text);
		if (IStarts(t, "<?xml"))
			return IContains(t.substr(0, 512), "<html") ? ResourceContent::Html : ResourceContent::Xml;
		if (IStarts(t, "<!DOCTYPE html") || IStarts(t, "<html") || IStarts(t, "<head") || IStarts(t, "<body") || (IStarts(t, "<!--") && IContains(t.substr(0, 2048), "<html")))
			return ResourceContent::Html;
		if (LooksLikeRegistryScript(t))
			return ResourceContent::RegistryScript;
		return ResourceContent::Text;
	}

	std::wstring FromUtf16Be(Bytes d, size_t offset, size_t length) {
		std::wstring s;
		for (size_t i = 0; i + 1 < length; i += 2)
			s += (wchar_t)Be16(d, offset + i);
		return s;
	}

	std::wstring SfntFamilyName(Bytes d) {
		size_t base = 0;
		if (Starts(d, { 't', 't', 'c', 'f' })) {
			if (Be32(d, 8) == 0)
				return {};
			base = Be32(d, 12);		// the first font of the collection
		}
		auto numTables = Be16(d, base + 4);
		for (size_t t = 0; t < numTables; t++) {
			auto rec = base + 12 + t * 16;
			if (rec + 16 > d.size())
				return {};
			if (Be32(d, rec) != 0x6E616D65)	// 'name'
				continue;
			size_t table = Be32(d, rec + 8);
			auto count = Be16(d, table + 2);
			size_t strings = table + Be16(d, table + 4);
			std::wstring mac, windows;
			for (size_t i = 0; i < count; i++) {
				auto r = table + 6 + i * 12;
				if (r + 12 > d.size())
					break;
				auto platform = Be16(d, r), encoding = Be16(d, r + 2), language = Be16(d, r + 4), nameId = Be16(d, r + 6);
				size_t length = Be16(d, r + 8), offset = strings + Be16(d, r + 10);
				if (nameId != 1 || offset + length > d.size())
					continue;
				if (platform == 3 && (encoding == 1 || encoding == 0) && (windows.empty() || language == 0x409))
					windows = FromUtf16Be(d, offset, length);
				else if (platform == 1 && encoding == 0 && mac.empty())
					for (size_t c = 0; c < length; c++)
						mac += (wchar_t)At(d, offset + c);
			}
			return windows.empty() ? mac : windows;
		}
		return {};
	}

	// a Windows .fnt font: dfFace is the offset of the face name
	std::wstring FntFaceName(Bytes d) {
		auto version = Le16(d, 0);
		if ((version != 0x200 && version != 0x300) || d.size() < 0x71)
			return {};
		size_t face = Le32(d, 0x69);
		std::wstring name;
		for (size_t i = face; i < d.size() && At(d, i) && name.size() < 64; i++)
			name += (wchar_t)At(d, i);
		return name;
	}

	// a single image .ico or .cur file for the image of an RT_ICON or RT_CURSOR resource
	std::vector<std::byte> MakeIconFile(Bytes image, bool cursor) {
		uint16_t hotX = 0, hotY = 0;
		if (cursor) {
			hotX = Le16(image, 0);
			hotY = Le16(image, 2);
			image = image.subspan(std::min<size_t>(4, image.size()));
		}
		uint32_t width = 0, height = 0;
		uint16_t bitCount = 0;
		if (Starts(image, { 0x89, 'P', 'N', 'G' })) {
			width = Be32(image, 16);
			height = Be32(image, 20);
			bitCount = 32;
		}
		else {
			width = Le32(image, 4);
			height = Le32(image, 8) / 2;	// the color and the mask bitmaps
			bitCount = Le16(image, 14);
		}
		std::vector<std::byte> file;
		auto add16 = [&](uint16_t v) { file.push_back((std::byte)(v & 0xFF)); file.push_back((std::byte)(v >> 8)); };
		auto add32 = [&](uint32_t v) { add16((uint16_t)v); add16((uint16_t)(v >> 16)); };
		add16(0);
		add16(cursor ? 2 : 1);
		add16(1);
		file.push_back((std::byte)(width >= 256 ? 0 : width));
		file.push_back((std::byte)(height >= 256 ? 0 : height));
		file.push_back((std::byte)0);	// colors
		file.push_back((std::byte)0);
		add16(cursor ? hotX : 1);		// planes, or the hot spot of a cursor
		add16(cursor ? hotY : bitCount);
		add32((uint32_t)image.size());
		add32(6 + 16);
		file.insert(file.end(), image.begin(), image.end());
		return file;
	}

	// a bitmap resource is a bitmap file without its BITMAPFILEHEADER
	std::vector<std::byte> MakeBitmapFile(Bytes dib) {
		uint32_t headerSize = Le32(dib, 0);
		uint16_t bitCount = Le16(dib, 14);
		uint32_t compression = Le32(dib, 16), colorsUsed = Le32(dib, 32);
		uint32_t colors = headerSize == 12 ? (bitCount <= 8 ? 1u << bitCount : 0) : (colorsUsed ? colorsUsed : (bitCount <= 8 ? 1u << bitCount : 0));
		uint32_t palette = colors * (headerSize == 12 ? 3 : 4);
		if (headerSize == 40 && (compression == 3 || compression == 6))	// BI_BITFIELDS, BI_ALPHABITFIELDS: the masks follow the header
			palette += compression == 3 ? 12 : 16;
		uint32_t offBits = 14 + headerSize + palette;
		uint32_t size = 14 + (uint32_t)dib.size();

		std::vector<std::byte> file;
		auto add16 = [&](uint16_t v) { file.push_back((std::byte)(v & 0xFF)); file.push_back((std::byte)(v >> 8)); };
		auto add32 = [&](uint32_t v) { add16((uint16_t)v); add16((uint16_t)(v >> 16)); };
		file.push_back((std::byte)'B');
		file.push_back((std::byte)'M');
		add32(size);
		add32(0);
		add32(offBits);
		file.insert(file.end(), dib.begin(), dib.end());
		return file;
	}

	std::wstring ImageExtension(Bytes d) {
		if (Starts(d, { 0x89, 'P', 'N', 'G' }))
			return L"png";
		if (Starts(d, { 0xFF, 0xD8, 0xFF }))
			return L"jpg";
		if (Starts(d, { 'G', 'I', 'F', '8' }))
			return L"gif";
		return L"bmp";
	}

	std::wstring FontExtension(Bytes d) {
		if (Starts(d, { 't', 't', 'c', 'f' }))
			return L"ttc";
		if (Starts(d, { 'O', 'T', 'T', 'O' }))
			return L"otf";
		return IsSfnt(d) ? L"ttf" : L"fnt";
	}
}

const wchar_t* ResourceContentToString(ResourceContent content) {
	switch (content) {
		case ResourceContent::Binary: return L"Binary";
		case ResourceContent::Image: return L"Image";
		case ResourceContent::Xml: return L"XML";
		case ResourceContent::Html: return L"HTML";
		case ResourceContent::Text: return L"Text";
		case ResourceContent::RegistryScript: return L"Registry Script";
		case ResourceContent::TypeLib: return L"Type Library";
		case ResourceContent::Font: return L"Font";
		case ResourceContent::Executable: return L"Executable";
	}
	return L"";
}

ResourceContent DetectResourceContent(std::span<const std::byte> data, uint16_t typeId, std::wstring_view typeName) {
	if (data.empty())
		return ResourceContent::Binary;

	// what the first bytes say is more reliable than the name of the type
	if (Starts(data, { 0x89, 'P', 'N', 'G' }) || Starts(data, { 0xFF, 0xD8, 0xFF }) || Starts(data, { 'G', 'I', 'F', '8' }) || IsBitmapFile(data))
		return ResourceContent::Image;
	if (Starts(data, { 'M', 'S', 'F', 'T' }) || Starts(data, { 'S', 'L', 'T', 'G' }))
		return ResourceContent::TypeLib;
	if (IsSfnt(data) || (typeId == TypeFont && FntFaceName(data).size()))
		return ResourceContent::Font;
	if (typeId == 0 && IEquals(typeName, L"TYPELIB"))
		return ResourceContent::TypeLib;
	if (IsExecutable(data))
		return ResourceContent::Executable;

	auto text = DetectText(data);
	if (text == ResourceContent::Binary)
		return text;
	if (typeId == TypeHtml && text != ResourceContent::Xml)
		return ResourceContent::Html;
	if (typeId == 0 && IEquals(typeName, L"REGISTRY"))
		return ResourceContent::RegistryScript;
	return text;
}

std::string ResourceTextToUtf8(std::span<const std::byte> data) {
	std::string result;
	if (IsUtf16(data)) {
		size_t skip = Starts(data, { 0xFF, 0xFE }) ? 2 : 0;
		auto chars = (data.size() - skip) / 2;
		std::wstring text(chars, L'\0');
		memcpy(text.data(), data.data() + skip, chars * 2);
		int len = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		result.resize(len);
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), len, nullptr, nullptr);
	}
	else if (Starts(data, { 0xEF, 0xBB, 0xBF }))
		result.assign((const char*)data.data() + 3, data.size() - 3);
	else {
		std::string_view raw((const char*)data.data(), data.size());
		// valid UTF-8 stays as is; anything else is in the ANSI code page
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(), nullptr, 0) > 0 || raw.empty())
			result = raw;
		else {
			int wlen = ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
			std::wstring wide(wlen, L'\0');
			::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), wide.data(), wlen);
			int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
			result.resize(len);
			::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, result.data(), len, nullptr, nullptr);
		}
	}
	// a terminating zero is common in resources
	while (!result.empty() && result.back() == '\0')
		result.pop_back();
	return result;
}

ResourceFile MakeResourceFile(std::span<const std::byte> data, uint16_t typeId, std::wstring_view typeName) {
	ResourceFile file;
	file.Data.assign(data.begin(), data.end());
	switch (typeId) {
		case TypeBitmap:
			if (!IsBitmapFile(data) && data.size() >= 16) {
				file.Data = MakeBitmapFile(data);
				file.Extension = L"bmp";
				return file;
			}
			break;
		case TypeIcon:
		case TypeCursor:
			if (data.size() >= 16) {
				file.Data = MakeIconFile(data, typeId == TypeCursor);
				file.Extension = typeId == TypeCursor ? L"cur" : L"ico";
				return file;
			}
			break;
		case TypeManifest: file.Extension = L"manifest"; return file;
		case TypeAniCursor: file.Extension = L"ani"; return file;
		case TypeAniIcon: file.Extension = L"ani"; return file;
		case TypeFont: file.Extension = FontExtension(data); return file;
	}

	switch (DetectResourceContent(data, typeId, typeName)) {
		case ResourceContent::Image: file.Extension = ImageExtension(data); break;
		case ResourceContent::Xml: file.Extension = L"xml"; break;
		case ResourceContent::Html: file.Extension = L"html"; break;
		case ResourceContent::Text: file.Extension = L"txt"; break;
		case ResourceContent::RegistryScript: file.Extension = L"rgs"; break;
		case ResourceContent::TypeLib: file.Extension = L"tlb"; break;
		case ResourceContent::Font: file.Extension = FontExtension(data); break;
		case ResourceContent::Executable:
		{
			auto lfanew = Le32(data, 0x3C);
			file.Extension = (Le16(data, lfanew + 4 + 18) & IMAGE_FILE_DLL) ? L"dll" : L"exe";
			break;
		}
		default: file.Extension = L"bin"; break;
	}
	return file;
}

// The group has a 6-byte header (reserved, type: 1 icons, 2 cursors, count) and 14-byte entries. An icon's entry has the
// width, height, colors, reserved, planes, bit count, size and ID; a cursor's has a 16-bit width and height (twice the
// image's height: the color and the mask) instead of the first four. A file has 16-byte entries with the offset of each
// image instead of its ID; a cursor file has the hot spot in place of the planes and the bit count, and its images do not
// start with the hot spot as cursor resources do.
std::vector<std::byte> MakeIconGroupFile(std::span<const std::byte> group, std::function<std::span<const std::byte>(uint16_t id)> const& image) {
	if (group.size() < 6 || Le16(group, 0) != 0)
		return {};
	auto type = Le16(group, 2);
	if (type != 1 && type != 2)
		return {};
	bool cursor = type == 2;
	auto count = Le16(group, 4);
	if (group.size() < 6 + (size_t)count * 14)
		return {};

	struct Entry {
		uint8_t Width, Height, Colors;
		uint16_t PlanesOrX, BitsOrY;
		std::span<const std::byte> Data;
	};
	std::vector<Entry> entries;
	for (uint16_t i = 0; i < count; i++) {
		size_t at = 6 + (size_t)i * 14;
		auto data = image(Le16(group, at + 12));
		if (data.empty())
			continue;
		Entry e{};
		if (cursor) {
			if (data.size() <= 4)
				continue;
			auto width = Le16(group, at), height = (uint16_t)(Le16(group, at + 2) / 2);
			e.Width = (uint8_t)(width >= 256 ? 0 : width);
			e.Height = (uint8_t)(height >= 256 ? 0 : height);
			e.PlanesOrX = Le16(data, 0);	// the hot spot
			e.BitsOrY = Le16(data, 2);
			e.Data = data.subspan(4);
		}
		else {
			e.Width = At(group, at);
			e.Height = At(group, at + 1);
			e.Colors = At(group, at + 2);
			e.PlanesOrX = Le16(group, at + 4);
			e.BitsOrY = Le16(group, at + 6);
			e.Data = data;
		}
		entries.push_back(e);
	}
	if (entries.empty())
		return {};

	std::vector<std::byte> file;
	auto add16 = [&](uint16_t v) { file.push_back((std::byte)(v & 0xFF)); file.push_back((std::byte)(v >> 8)); };
	auto add32 = [&](uint32_t v) { add16((uint16_t)v); add16((uint16_t)(v >> 16)); };
	add16(0);
	add16(type);
	add16((uint16_t)entries.size());
	uint32_t offset = 6 + (uint32_t)entries.size() * 16;
	for (auto const& e : entries) {
		file.push_back((std::byte)e.Width);
		file.push_back((std::byte)e.Height);
		file.push_back((std::byte)e.Colors);
		file.push_back((std::byte)0);
		add16(e.PlanesOrX);
		add16(e.BitsOrY);
		add32((uint32_t)e.Data.size());
		add32(offset);
		offset += (uint32_t)e.Data.size();
	}
	for (auto const& e : entries)
		file.insert(file.end(), e.Data.begin(), e.Data.end());
	return file;
}

std::wstring GetFontFaceName(std::span<const std::byte> data) {
	return IsSfnt(data) ? SfntFamilyName(data) : FntFaceName(data);
}
