#include "TestCommon.h"
#include <ResourceContent.h>
#include <TypeLibText.h>
#include <fstream>
#include <iterator>

namespace {
	using Bytes = std::vector<uint8_t>;

	std::span<const std::byte> AsBytes(Bytes const& b) { return std::as_bytes(std::span(b)); }
	std::span<const std::byte> AsBytes(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

	Bytes ReadFile(PCWSTR path) {
		std::ifstream in(path, std::ios::binary);
		return Bytes(std::istreambuf_iterator<char>(in), {});
	}

	Bytes Utf16(std::string_view text, bool bom) {
		Bytes b;
		if (bom) {
			b.push_back(0xFF);
			b.push_back(0xFE);
		}
		for (auto c : text) {
			b.push_back((uint8_t)c);
			b.push_back(0);
		}
		return b;
	}

	constexpr uint16_t RT_BITMAP_ = 2, RT_ICON_ = 3, RT_CURSOR_ = 1, RT_FONT_ = 8, RT_RCDATA_ = 10, RT_HTML_ = 23;
}

TEST_CASE("Images are recognized by their first bytes", "[resources]") {
	Bytes png{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0 };
	CHECK(DetectResourceContent(AsBytes(png), RT_RCDATA_, L"") == ResourceContent::Image);
	CHECK(DetectResourceContent(AsBytes(png), 0, L"PNG") == ResourceContent::Image);
	CHECK(MakeResourceFile(AsBytes(png), 0, L"PNG").Extension == L"png");

	Bytes jpeg{ 0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10 };
	CHECK(DetectResourceContent(AsBytes(jpeg), RT_RCDATA_, L"") == ResourceContent::Image);
	CHECK(MakeResourceFile(AsBytes(jpeg), RT_RCDATA_, L"").Extension == L"jpg");

	CHECK(DetectResourceContent(AsBytes("GIF89a....."), 0, L"GIF") == ResourceContent::Image);
}

TEST_CASE("Text resources: XML, HTML, registry scripts and plain text", "[resources]") {
	CHECK(DetectResourceContent(AsBytes("<?xml version=\"1.0\"?><root/>"), RT_RCDATA_, L"") == ResourceContent::Xml);
	CHECK(DetectResourceContent(AsBytes("\r\n<!DOCTYPE html><html><body>Hi</body></html>"), RT_RCDATA_, L"") == ResourceContent::Html);
	CHECK(DetectResourceContent(AsBytes("<p>some markup</p>"), RT_HTML_, L"") == ResourceContent::Html);
	CHECK(DetectResourceContent(AsBytes("HKCR\r\n{\r\n  NoRemove CLSID\r\n}"), 0, L"REGISTRY") == ResourceContent::RegistryScript);
	CHECK(DetectResourceContent(AsBytes("HKCR { }"), 0, L"SCRIPT") == ResourceContent::RegistryScript);
	CHECK(DetectResourceContent(AsBytes("Just some words\r\nand more"), RT_RCDATA_, L"") == ResourceContent::Text);
	CHECK(MakeResourceFile(AsBytes("HKCR { }"), 0, L"REGISTRY").Extension == L"rgs");

	// the INF scripts of Advanced INF Setup: by their type, or by the section that INF files start with
	CHECK(DetectResourceContent(AsBytes("[Version]\r\nSignature=\"$CHICAGO$\"\r\n"), 0, L"REGINST") == ResourceContent::Inf);
	CHECK(DetectResourceContent(AsBytes("[Reg]\r\nAddReg=X\r\n"), 0, L"REGINST") == ResourceContent::Inf);
	CHECK(DetectResourceContent(AsBytes("\r\n[version]\r\nSignature=\"$Windows NT$\"\r\n"), RT_RCDATA_, L"") == ResourceContent::Inf);
	CHECK(MakeResourceFile(AsBytes("[Version]\r\n"), 0, L"REGINST").Extension == L"inf");

	SECTION("UTF-16, with and without a byte order mark") {
		for (bool bom : { true, false }) {
			auto data = Utf16("<?xml version=\"1.0\"?><a/>", bom);
			CHECK(DetectResourceContent(AsBytes(data), RT_RCDATA_, L"") == ResourceContent::Xml);
			CHECK(ResourceTextToUtf8(AsBytes(data)) == "<?xml version=\"1.0\"?><a/>");
		}
	}
	SECTION("UTF-8 with a byte order mark, and a terminating zero") {
		std::string text = "\xEF\xBB\xBFHello\xC3\xA9";
		text += '\0';
		CHECK(ResourceTextToUtf8(AsBytes(text)) == "Hello\xC3\xA9");
	}
}

TEST_CASE("Binary data is not text", "[resources]") {
	Bytes data{ 0x01, 0x02, 0x00, 0xFF, 0x10, 0x20, 0x00, 0x00, 0x99, 0x88 };
	CHECK(DetectResourceContent(AsBytes(data), RT_RCDATA_, L"") == ResourceContent::Binary);
	CHECK(MakeResourceFile(AsBytes(data), RT_RCDATA_, L"").Extension == L"bin");
	CHECK(DetectResourceContent({}, RT_RCDATA_, L"") == ResourceContent::Binary);
}

TEST_CASE("Embedded executables", "[resources][system]") {
	auto dll = ReadFile(L"C:\\Windows\\System32\\version.dll");
	REQUIRE(dll.size() > 1024);
	CHECK(DetectResourceContent(AsBytes(dll), RT_RCDATA_, L"") == ResourceContent::Executable);
	CHECK(MakeResourceFile(AsBytes(dll), RT_RCDATA_, L"").Extension == L"dll");
	auto exe = ReadFile(L"C:\\Windows\\System32\\cmd.exe");
	CHECK(MakeResourceFile(AsBytes(exe), 0, L"BIN").Extension == L"exe");
}

TEST_CASE("A bitmap resource is saved with a file header", "[resources]") {
	// a 2x2, 24 bits per pixel DIB: the header and 2 rows of 8 bytes (rows are padded to 4 bytes)
	Bytes dib(40 + 16, 0);
	dib[0] = 40;
	dib[4] = 2;
	dib[8] = 2;
	dib[12] = 1;
	dib[14] = 24;
	auto file = MakeResourceFile(AsBytes(dib), RT_BITMAP_, L"");
	CHECK(file.Extension == L"bmp");
	REQUIRE(file.Data.size() == dib.size() + 14);
	CHECK(file.Data[0] == std::byte{ 'B' });
	CHECK(file.Data[1] == std::byte{ 'M' });
	CHECK(std::to_integer<uint32_t>(file.Data[2]) == dib.size() + 14);
	CHECK(std::to_integer<uint32_t>(file.Data[10]) == 14 + 40);	// no palette with 24 bits per pixel

	SECTION("with a palette") {
		dib[14] = 8;
		dib.resize(40 + 256 * 4 + 8);
		auto paletted = MakeResourceFile(AsBytes(dib), RT_BITMAP_, L"");
		CHECK((std::to_integer<uint32_t>(paletted.Data[10]) | std::to_integer<uint32_t>(paletted.Data[11]) << 8) == 14 + 40 + 1024);
	}
	SECTION("a saved bitmap is recognized as an image") {
		auto again = std::span<const std::byte>(file.Data);
		CHECK(DetectResourceContent(again, RT_RCDATA_, L"") == ResourceContent::Image);
	}
}

TEST_CASE("Icon and cursor images are saved as .ico and .cur files", "[resources]") {
	Bytes dib(40 + 64, 0);
	dib[0] = 40;
	dib[4] = 16;
	dib[8] = 32;	// twice the height: the image and the mask
	dib[12] = 1;
	dib[14] = 32;
	auto icon = MakeResourceFile(AsBytes(dib), RT_ICON_, L"");
	CHECK(icon.Extension == L"ico");
	REQUIRE(icon.Data.size() == 22 + dib.size());
	CHECK(std::to_integer<int>(icon.Data[2]) == 1);		// an icon
	CHECK(std::to_integer<int>(icon.Data[6]) == 16);	// width
	CHECK(std::to_integer<int>(icon.Data[7]) == 16);	// height
	CHECK(std::to_integer<int>(icon.Data[12]) == 32);	// bits per pixel
	CHECK(std::to_integer<int>(icon.Data[18]) == 22);	// where the image starts

	Bytes cursor{ 5, 0, 7, 0 };		// the hot spot
	cursor.insert(cursor.end(), dib.begin(), dib.end());
	auto cur = MakeResourceFile(AsBytes(cursor), RT_CURSOR_, L"");
	CHECK(cur.Extension == L"cur");
	REQUIRE(cur.Data.size() == 22 + dib.size());
	CHECK(std::to_integer<int>(cur.Data[2]) == 2);		// a cursor
	CHECK(std::to_integer<int>(cur.Data[10]) == 5);		// hot spot x
	CHECK(std::to_integer<int>(cur.Data[12]) == 7);		// hot spot y
}

TEST_CASE("Font names", "[resources][system]") {
	auto arial = ReadFile(L"C:\\Windows\\Fonts\\arial.ttf");
	REQUIRE(!arial.empty());
	CHECK(DetectResourceContent(AsBytes(arial), RT_RCDATA_, L"") == ResourceContent::Font);
	CHECK(GetFontFaceName(AsBytes(arial)) == L"Arial");
	CHECK(MakeResourceFile(AsBytes(arial), RT_RCDATA_, L"").Extension == L"ttf");

	auto cambria = ReadFile(L"C:\\Windows\\Fonts\\cambria.ttc");
	REQUIRE(!cambria.empty());
	CHECK(GetFontFaceName(AsBytes(cambria)) == L"Cambria");
	CHECK(MakeResourceFile(AsBytes(cambria), RT_RCDATA_, L"").Extension == L"ttc");

	SECTION("a Windows .fnt font") {
		Bytes fnt(0x80, 0);
		fnt[0] = 0x00;
		fnt[1] = 0x03;		// version 3.0
		fnt[0x69] = 0x76;	// dfFace
		std::string face = "MyFont";
		std::copy(face.begin(), face.end(), fnt.begin() + 0x76);
		CHECK(GetFontFaceName(AsBytes(fnt)) == L"MyFont");
		CHECK(DetectResourceContent(AsBytes(fnt), RT_FONT_, L"") == ResourceContent::Font);
		CHECK(MakeResourceFile(AsBytes(fnt), RT_FONT_, L"").Extension == L"fnt");
	}
}

TEST_CASE("Type libraries", "[resources][system]") {
	// stdole2.tlb is a DLL with the type library as a resource
	wil::unique_hmodule dll(::LoadLibraryEx(L"C:\\Windows\\System32\\stdole2.tlb", nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
	REQUIRE(dll);
	auto hRes = ::FindResource(dll.get(), MAKEINTRESOURCE(1), L"TYPELIB");
	REQUIRE(hRes);
	auto bytes = (const uint8_t*)::LockResource(::LoadResource(dll.get(), hRes));
	Bytes tlb(bytes, bytes + ::SizeofResource(dll.get(), hRes));
	REQUIRE(tlb.size() > 4);
	CHECK(DetectResourceContent(AsBytes(tlb), 10, L"") == ResourceContent::TypeLib);	// by its first bytes, whatever the type
	CHECK(DetectResourceContent(AsBytes(tlb), 0, L"TYPELIB") == ResourceContent::TypeLib);
	CHECK(MakeResourceFile(AsBytes(tlb), 0, L"TYPELIB").Extension == L"tlb");

	std::wstring error;
	auto text = DescribeTypeLib(AsBytes(tlb), error);
	INFO(Narrow(error));
	REQUIRE(error.empty());
	CHECK(text.find(L"library stdole") != std::wstring::npos);
	CHECK(text.find(L"uuid(00020430-0000-0000-C000-000000000046)") != std::wstring::npos);
	CHECK(text.find(L"interface IUnknown") != std::wstring::npos);
	CHECK(text.find(L"HRESULT QueryInterface(") != std::wstring::npos);
	CHECK(text.find(L"dispinterface Font") != std::wstring::npos);

	SECTION("not a type library") {
		std::wstring error2;
		CHECK(DescribeTypeLib(AsBytes("not a type library at all"), error2).empty());
		CHECK_FALSE(error2.empty());
	}
}

TEST_CASE("Icon and cursor groups are saved as multi-image files", "[resources]") {
	auto u16 = [](Bytes& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
	auto u32 = [&](Bytes& b, uint32_t v) { u16(b, (uint16_t)v); u16(b, (uint16_t)(v >> 16)); };
	auto read32 = [](std::vector<std::byte> const& b, size_t at) {
		uint32_t v;
		memcpy(&v, b.data() + at, 4);
		return v;
	};

	SECTION("icons, one of them missing") {
		Bytes group;
		u16(group, 0); u16(group, 1); u16(group, 3);
		auto entry = [&](uint8_t size, uint16_t bits, uint32_t bytes, uint16_t id) {
			group.push_back(size); group.push_back(size); group.push_back(0); group.push_back(0);
			u16(group, 1); u16(group, bits); u32(group, bytes); u16(group, id);
		};
		entry(16, 32, 10, 1);
		entry(0, 32, 20, 2);	// 256x256
		entry(32, 8, 30, 3);	// not in the file
		Bytes image1(10, 0x11), image2(20, 0x22);
		auto file = MakeIconGroupFile(AsBytes(group), [&](uint16_t id) -> std::span<const std::byte> {
			return id == 1 ? AsBytes(image1) : id == 2 ? AsBytes(image2) : std::span<const std::byte>();
		});
		REQUIRE(file.size() == 6 + 2 * 16 + 10 + 20);
		CHECK(std::to_integer<int>(file[2]) == 1);		// icons
		CHECK(std::to_integer<int>(file[4]) == 2);		// two of them
		CHECK(std::to_integer<int>(file[6]) == 16);		// the first one's width
		CHECK(std::to_integer<int>(file[12]) == 32);	// its bit count
		CHECK(read32(file, 6 + 8) == 10);				// its size
		CHECK(read32(file, 6 + 12) == 6 + 32);			// where it is
		CHECK(std::to_integer<int>(file[22]) == 0);		// the second: 256
		CHECK(read32(file, 22 + 12) == 6 + 32 + 10);
		CHECK(std::to_integer<int>(file[6 + 32]) == 0x11);
		CHECK(std::to_integer<int>(file[6 + 32 + 10]) == 0x22);
	}
	SECTION("a cursor: the hot spot moves from the image to the entry") {
		Bytes group;
		u16(group, 0); u16(group, 2); u16(group, 1);
		u16(group, 32); u16(group, 64);		// width, twice the height
		u16(group, 1); u16(group, 1); u32(group, 12); u16(group, 7);
		Bytes image{ 5, 0, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 };	// the hot spot, then the image
		auto file = MakeIconGroupFile(AsBytes(group), [&](uint16_t id) -> std::span<const std::byte> {
			return id == 7 ? AsBytes(image) : std::span<const std::byte>();
		});
		REQUIRE(file.size() == 6 + 16 + 8);
		CHECK(std::to_integer<int>(file[2]) == 2);		// cursors
		CHECK(std::to_integer<int>(file[6]) == 32);
		CHECK(std::to_integer<int>(file[7]) == 32);		// the image's height
		CHECK(std::to_integer<int>(file[10]) == 5);		// hot spot x
		CHECK(std::to_integer<int>(file[12]) == 9);		// hot spot y
		CHECK(read32(file, 14) == 8);
		CHECK(std::to_integer<int>(file[22]) == 1);		// the image, without the hot spot
	}
	SECTION("not a group") {
		Bytes junk{ 1, 2, 3 };
		CHECK(MakeIconGroupFile(AsBytes(junk), [](uint16_t) { return std::span<const std::byte>(); }).empty());
	}
}

TEST_CASE("An icon group of a system file makes an .ico file that Windows loads", "[resources][system]") {
	wil::unique_hmodule exe(::LoadLibraryEx(L"C:\\Windows\\explorer.exe", nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
	REQUIRE(exe);
	auto resource = [&](PCWSTR type, PCWSTR name) -> std::span<const std::byte> {
		auto hRes = ::FindResource(exe.get(), name, type);
		if (!hRes)
			return {};
		return { (const std::byte*)::LockResource(::LoadResource(exe.get(), hRes)), ::SizeofResource(exe.get(), hRes) };
	};
	// the first icon group
	PCWSTR first = nullptr;
	::EnumResourceNames(exe.get(), RT_GROUP_ICON, [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
		*(PCWSTR*)param = IS_INTRESOURCE(name) ? name : _wcsdup(name);
		return FALSE;
	}, (LONG_PTR)&first);
	REQUIRE(first);
	auto group = resource(RT_GROUP_ICON, first);
	REQUIRE(group.size() > 6);
	auto count = std::to_integer<int>(group[4]) | (std::to_integer<int>(group[5]) << 8);

	auto file = MakeIconGroupFile(group, [&](uint16_t id) { return resource(RT_ICON, MAKEINTRESOURCE(id)); });
	REQUIRE(!file.empty());
	CHECK((std::to_integer<int>(file[4]) | (std::to_integer<int>(file[5]) << 8)) == count);

	WCHAR dir[MAX_PATH], path[MAX_PATH];
	::GetTempPath(MAX_PATH, dir);
	::GetTempFileName(dir, L"ico", 0, path);
	{
		std::ofstream out(path, std::ios::binary);
		out.write((const char*)file.data(), file.size());
	}
	wil::unique_hicon icon((HICON)::LoadImage(nullptr, path, IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
	::DeleteFile(path);
	CHECK(icon);
}
