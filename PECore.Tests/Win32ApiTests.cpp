#include "TestCommon.h"
#include <Win32Api.h>
#include <MetadataPackages.h>
#include <algorithm>

// The Win32 metadata (Windows.Win32.winmd, which GetWin32Metadata.ps1 gets and the build of TotalPE copies next to the
// executables): these are skipped without it
namespace {
	Win32Api* Api() {
		return Win32Api::Get(Win32Api::Machine::X64);
	}

	Win32Api::Namespace const* Child(Win32Api::Namespace const& ns, std::string const& path) {
		auto node = &ns;
		size_t start = 0;
		while (node && start <= path.size()) {
			auto dot = path.find('.', start);
			auto part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
			auto it = std::ranges::find(node->Children, part, &Win32Api::Namespace::Name);
			node = it == node->Children.end() ? nullptr : &*it;
			if (dot == std::string::npos)
				break;
			start = dot + 1;
		}
		return node;
	}

	size_t CountEntries(Win32Api::Namespace const& ns) {
		auto count = ns.Entries.size();
		for (auto const& child : ns.Children)
			count += CountEntries(child);
		return count;
	}
}

TEST_CASE("Win32 API: the namespaces of functions and types", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto function = api->Find("CreateFileW");
	REQUIRE(function);
	CHECK(function->Kind == Win32Api::Entry::Kinds::Function);
	CHECK(function->Namespace == "Windows.Win32.Storage.FileSystem");
	CHECK(_stricmp(function->Where.c_str(), "KERNEL32.dll") == 0);

	auto type = api->Find("SECURITY_ATTRIBUTES");
	REQUIRE(type);
	CHECK(type->Kind == Win32Api::Entry::Kinds::Struct);
	CHECK(type->Namespace == "Windows.Win32.Security");
	CHECK(type->Where == "Security");
}

TEST_CASE("Win32 API: constants", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto maxPath = api->Find("MAX_PATH");
	REQUIRE(maxPath);
	CHECK(maxPath->Kind == Win32Api::Entry::Kinds::Constant);
	CHECK(maxPath->Namespace == "Windows.Win32.Foundation");
	auto text = api->Declaration(*maxPath);
	INFO(text);
	CHECK(text.find("#define MAX_PATH 0x104") != std::string::npos);
	CHECK(text.find("(260)") != std::string::npos);

	auto fail = api->Find("E_FAIL");
	REQUIRE(fail);
	text = api->Declaration(*fail);
	INFO(text);
	CHECK(text.find("0x80004005") != std::string::npos);
	CHECK(text.find("HRESULT") != std::string::npos);

	auto folder = api->Find("FOLDERID_Desktop");
	REQUIRE(folder);
	CHECK(folder->Kind == Win32Api::Entry::Kinds::Constant);
	text = api->Declaration(*folder);
	INFO(text);
	CHECK(text.find("{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}") != std::string::npos);
	CHECK(text.find("DEFINE_GUID(FOLDERID_Desktop, 0xB4BFCC3A") != std::string::npos);

	// the lines of a declaration and what they refer to are as many
	std::vector<std::string> lines;
	text = api->Declaration(*fail, &lines);
	CHECK((size_t)std::ranges::count(text, '\n') == lines.size());
}

TEST_CASE("Win32 API: the tree of namespaces", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto const& root = api->Namespaces();
	CHECK(root.Name.empty());
	// each entry is in one namespace
	CHECK(CountEntries(root) == api->Entries().size());

	auto threading = Child(root, "Windows.Win32.System.Threading");
	REQUIRE(threading);
	CHECK(threading->Name == "Threading");
	CHECK(threading->FullName == "Windows.Win32.System.Threading");
	auto const& entries = api->Entries();
	CHECK(std::ranges::any_of(threading->Entries, [&](size_t i) { return entries[i].Name == "CreateThread"; }));
	for (auto i : threading->Entries)
		CHECK(entries[i].Namespace == threading->FullName);
	// by name
	auto system = Child(root, "Windows.Win32.System");
	REQUIRE(system);
	CHECK(std::ranges::is_sorted(system->Children, [](auto const& a, auto const& b) { return _stricmp(a.Name.c_str(), b.Name.c_str()) < 0; }));
}

TEST_CASE("Win32 API: the layout of a struct is of its machine", "[win32api]") {
	auto x64 = Win32Api::Get(Win32Api::Machine::X64);
	auto x86 = Win32Api::Get(Win32Api::Machine::X86);
	if (!x64 || !x86)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto text64 = x64->Declaration(*x64->Find("SECURITY_ATTRIBUTES"));
	auto text86 = x86->Declaration(*x86->Find("SECURITY_ATTRIBUTES"));
	INFO(text64);
	INFO(text86);
	CHECK(text64.find("0x18 bytes") != std::string::npos);
	CHECK(text86.find("0xC bytes") != std::string::npos);
	CHECK(text86.find("/* 0x004 */ void *lpSecurityDescriptor;") != std::string::npos);
}

TEST_CASE("Win32 API: what uses a type", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto const& entries = api->Entries();
	auto index = [&](char const* name) { return (size_t)(api->Find(name) - entries.data()); };
	auto names = [&](size_t entry) {
		std::vector<std::string> result;
		for (auto i : api->UsedBy(entry))
			result.push_back(entries[i].Name);
		return result;
	};
	auto users = names(index("SECURITY_ATTRIBUTES"));
	CHECK(std::ranges::find(users, "CreateFileW") != users.end());		// a parameter (a pointer)
	CHECK(std::ranges::find(users, "CreateProcessW") != users.end());
	CHECK(std::ranges::find(users, "SECURITY_ATTRIBUTES") == users.end());	// (not itself)
	// a field (of a nested union too): SYSTEM_INFO's dwOemId is in an anonymous union
	users = names(index("PROCESSOR_ARCHITECTURE"));
	CHECK(std::ranges::find(users, "SYSTEM_INFO") != users.end());
	// a base interface
	users = names(index("IUnknown"));
	CHECK(std::ranges::find(users, "IClassFactory") != users.end());
	// by name, once each
	auto const& used = api->UsedBy(index("HANDLE"));
	CHECK(std::ranges::is_sorted(used));
	CHECK(std::ranges::adjacent_find(used) == used.end());
	// a function uses, it is not used
	CHECK(api->UsedBy(index("CreateFileW")).empty());
}

TEST_CASE("Win32 API: what has a value", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto const& entries = api->Entries();
	auto has = [&](std::vector<Win32Api::NamedValue> const& found, std::string const& name, std::string const& entry) {
		return std::ranges::any_of(found, [&](auto const& v) { return v.Name == name && entries[v.Entry].Name == entry; });
	};
	// a constant of HRESULT, by its bits and as a negative number
	CHECK(has(api->FindValue(0x80070005), "E_ACCESSDENIED", "E_ACCESSDENIED"));
	CHECK(has(api->FindValue((uint64_t)(int64_t)-2147024891), "E_ACCESSDENIED", "E_ACCESSDENIED"));
	// (the error codes are an enum's)
	CHECK(has(api->FindValue(5), "ERROR_ACCESS_DENIED", "WIN32_ERROR"));
	// a value of an enum: its name, and the enum
	CHECK(has(api->FindValue(1), "FILE_SHARE_READ", "FILE_SHARE_MODE"));
	// a GUID: IID_IUnknown, of IUnknown
	GUID iunknown{ 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
	auto guid = api->FindGuid(iunknown);
	REQUIRE(guid);
	CHECK(guid->Name == "IID_IUnknown");
	CHECK(entries[guid->Entry].Name == "IUnknown");
}

TEST_CASE("Win32 API: what the attributes say, in the declarations", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto declaration = [&](char const* name) {
		auto entry = api->Find(name);
		REQUIRE(entry);
		return api->Declaration(*entry);
	};
	auto text = declaration("CreateFileW");
	INFO(text);
	CHECK(text.find("// KERNEL32.dll (fileapi.h)") != std::string::npos);
	CHECK(text.find("sets the last error") != std::string::npos);
	CHECK(text.find("Windows XP+") != std::string::npos);
	CHECK(text.find("PCWSTR lpFileName") != std::string::npos);
	text = declaration("ReadFile");
	INFO(text);
	CHECK(text.find("size in bytes: nNumberOfBytesToRead") != std::string::npos);
	text = declaration("CoCreateInstance");
	INFO(text);
	CHECK(text.find("const GUID *rclsid") != std::string::npos);
	CHECK(text.find("COM object out") != std::string::npos);
	text = declaration("HANDLE");
	INFO(text);
	CHECK(text.find("free with CloseHandle") != std::string::npos);
	CHECK(text.find("invalid: -1 or 0") != std::string::npos);
	CHECK(Win32Api::HeaderOf(L"https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-iocreatedevice") == "wdm.h");
	CHECK(Win32Api::HeaderOf(L"https://learn.microsoft.com/windows/win32/SecBioMet/winbio-setting-source-constants").empty());
	// the lines and what they refer to are as many
	std::vector<std::string> lines;
	text = api->Declaration(*api->Find("CreateFileW"), &lines);
	CHECK((size_t)std::ranges::count(text, '\n') == lines.size());
	// on a line of its own: its parameters without their notes
	text = api->Declaration(*api->Find("CreateFileW"), &lines, true);
	INFO(text);
	CHECK((size_t)std::ranges::count(text, '\n') == lines.size());
	CHECK(text.find("HANDLE CreateFileW(PCWSTR lpFileName, ") != std::string::npos);
	CHECK(text.find("hTemplateFile);\r\n") != std::string::npos);
	CHECK(text.find("// in") == std::string::npos);
	// an interface's methods: a line each
	text = api->Declaration(*api->Find("IUnknown"), nullptr, true);
	INFO(text);
	CHECK(text.find("\tHRESULT QueryInterface(const GUID *riid, void **ppvObject);\r\n") != std::string::npos);
}


TEST_CASE("Win32 API: the WDK's functions and types, with Windows.Win32's", "[win32api]") {
	auto api = Api();
	if (!api || ::GetFileAttributesW(Win32Api::WdkMetadataPath().c_str()) == INVALID_FILE_ATTRIBUTES)
		SKIP("no Windows.Wdk.winmd next to the executable");
	auto const& entries = api->Entries();
	auto create = api->Find("IoCreateDevice");
	REQUIRE(create);
	CHECK(create->Kind == Win32Api::Entry::Kinds::Function);
	CHECK(_stricmp(create->Where.c_str(), "ntoskrnl.exe") == 0);
	CHECK(create->Namespace.starts_with("Windows.Wdk."));
	auto text = api->Declaration(*create);
	INFO(text);
	// (the WDK's metadata has no pages of the documentation: no header)
	// a type of the WDK, and one of Windows.Win32 (UNICODE_STRING): not void
	CHECK(text.find("DRIVER_OBJECT *DriverObject") != std::string::npos);
	CHECK(text.find("UNICODE_STRING *DeviceName") != std::string::npos);
	CHECK(text.find("NTSTATUS IoCreateDevice(") != std::string::npos);
	// a struct of the WDK: as large as it is
	auto driver = api->Find("DRIVER_OBJECT");
	REQUIRE(driver);
	text = api->Declaration(*driver);
	INFO(text);
	CHECK(text.find("0x150 bytes") != std::string::npos);
	// what uses a type of Windows.Win32: the WDK's functions too
	auto users = api->UsedBy(api->Find("UNICODE_STRING") - entries.data());
	CHECK(std::ranges::any_of(users, [&](size_t i) { return entries[i].Name == "IoCreateDevice"; }));
	// the namespaces: Windows.Wdk with Windows.Win32
	auto windows = Child(api->Namespaces(), "Windows");
	REQUIRE(windows);
	CHECK(Child(*windows, "Wdk"));
	CHECK(Child(*windows, "Win32"));
	// the decompiler's: a function of the kernel, by its DLL
	uint32_t type = 0;
	std::vector<std::string> names;
	CHECK(api->Function("IoCreateDevice", "ntoskrnl.exe", type, names));
	CHECK(names.size() == 7);
}

TEST_CASE("Win32 API: the layout of a struct", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	using Kinds = Win32Api::LayoutItem::Kinds;
	auto layout = api->LayoutOf(*api->Find("SECURITY_ATTRIBUTES"));
	REQUIRE(layout);
	CHECK(layout->Size == 0x18);
	CHECK(layout->Alignment == 8);
	REQUIRE(layout->Items.size() == 5);
	CHECK((layout->Items[0].Name == "nLength" && layout->Items[0].Offset == 0 && layout->Items[0].Size == 4));
	CHECK((layout->Items[1].Kind == Kinds::Padding && layout->Items[1].Offset == 4 && layout->Items[1].Size == 4));
	CHECK((layout->Items[2].Name == "lpSecurityDescriptor" && layout->Items[2].Offset == 8 && layout->Items[2].Size == 8));
	CHECK((layout->Items[3].Name == "bInheritHandle" && layout->Items[3].Offset == 0x10));
	CHECK((layout->Items[4].Kind == Kinds::Padding && layout->Items[4].Offset == 0x14 && layout->Items[4].Size == 4));
	// a union in it, and a struct in that: their fields, where they are
	layout = api->LayoutOf(*api->Find("SYSTEM_INFO"));
	REQUIRE(layout);
	auto field = std::ranges::find(layout->Items, std::string("Anonymous.Anonymous.wProcessorArchitecture"), &Win32Api::LayoutItem::Path);
	REQUIRE(field != layout->Items.end());
	CHECK(field->Depth == 2);
	CHECK(field->Offset == 0);
	CHECK(field->Size == 2);
	// (the union and the struct in it, unnamed: by what they are)
	auto anonymous = std::ranges::find(layout->Items, std::string("Anonymous"), &Win32Api::LayoutItem::Path);
	REQUIRE(anonymous != layout->Items.end());
	CHECK(anonymous->Type == "union");
	auto inner = std::ranges::find(layout->Items, std::string("Anonymous.Anonymous"), &Win32Api::LayoutItem::Path);
	REQUIRE(inner != layout->Items.end());
	CHECK(inner->Type == "struct");
	auto pageSize = std::ranges::find(layout->Items, std::string("dwPageSize"), &Win32Api::LayoutItem::Path);
	REQUIRE(pageSize != layout->Items.end());
	CHECK(pageSize->Offset == 4);
	// not a struct
	CHECK(!api->LayoutOf(*api->Find("CreateFileW")));
}

TEST_CASE("Win32 API: a value of an enum by its names", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto shareMode = api->Find("FILE_SHARE_MODE");
	REQUIRE(shareMode);
	CHECK(api->DecodeValue(*shareMode, 1) == "FILE_SHARE_READ");
	CHECK(api->DecodeValue(*shareMode, 3) == "FILE_SHARE_READ | FILE_SHARE_WRITE");
	CHECK(api->DecodeValue(*shareMode, 7) == "FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE");
	// bits no name has
	CHECK(api->DecodeValue(*shareMode, 0x81) == "FILE_SHARE_READ | 0x80");
	// a value that is not of flags
	auto error = api->Find("WIN32_ERROR");
	REQUIRE(error);
	CHECK(api->DecodeValue(*error, 5) == "ERROR_ACCESS_DENIED");
	CHECK(api->DecodeValue(*api->Find("CreateFileW"), 1).empty());
}

TEST_CASE("Win32 API: the vtable of an interface", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto slots = api->VtableOf(*api->Find("IClassFactory"));
	REQUIRE(slots.size() == 5);
	CHECK((slots[0].Interface == "IUnknown" && slots[0].Method == "QueryInterface"));
	CHECK((slots[2].Interface == "IUnknown" && slots[2].Method == "Release"));
	CHECK((slots[3].Interface == "IClassFactory" && slots[3].Method == "CreateInstance"));
	INFO(slots[3].Signature);
	CHECK(slots[3].Signature.starts_with("HRESULT CreateInstance(IUnknown *pUnkOuter"));
	CHECK(slots[4].Method == "LockServer");
	CHECK(api->VtableOf(*api->Find("CreateFileW")).empty());
}

TEST_CASE("Win32 API: related entries", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	auto const& entries = api->Entries();
	auto names = [&](char const* name) {
		std::vector<std::string> result;
		for (auto i : api->RelatedOf(api->Find(name) - entries.data()))
			result.push_back(entries[i].Name);
		return result;
	};
	auto related = names("CreateFileW");
	CHECK(std::ranges::find(related, "CreateFileA") != related.end());
	CHECK(std::ranges::find(related, "CreateFile2") != related.end());
	CHECK(std::ranges::find(related, "CreateFileW") == related.end());
	CHECK(std::ranges::find(related, "CreateFileTransactedW") == related.end());
	related = names("CreateWindowExW");
	CHECK(std::ranges::find(related, "CreateWindowExA") != related.end());
	// types with types
	related = names("OSVERSIONINFOW");
	CHECK(std::ranges::find(related, "OSVERSIONINFOA") != related.end());
	CHECK(std::ranges::find(related, "OSVERSIONINFOEXW") != related.end());
	CHECK(std::ranges::none_of(related, [&](std::string const& n) { return api->Find(n)->Kind == Win32Api::Entry::Kinds::Function; }));
}

// (of nuget.org: hidden, run with [metadata-packages])
TEST_CASE("Win32 API: the metadata's packages", "[.][metadata-packages]") {
	auto versions = MetadataPackages::Versions(MetadataPackages::Win32Package);
	REQUIRE(!versions.empty());
	CHECK(std::ranges::find(versions, "70.0.11-preview") != versions.end());
	auto wdk = MetadataPackages::Versions(MetadataPackages::WdkPackage);
	REQUIRE(!wdk.empty());
	// a version installed in a folder: the files, as large as they are
	wchar_t temp[MAX_PATH];
	::GetTempPathW(MAX_PATH, temp);
	std::wstring folder = std::wstring(temp) + L"Win32MetaCoreTest";
	REQUIRE(MetadataPackages::Install("70.0.11-preview", wdk.back(), folder));
	WIN32_FILE_ATTRIBUTE_DATA info;
	REQUIRE(::GetFileAttributesExW((folder + L"\\Windows.Win32.winmd").c_str(), GetFileExInfoStandard, &info));
	CHECK(info.nFileSizeLow == 24328704);
	CHECK(::GetFileAttributesW((folder + L"\\Windows.Wdk.winmd").c_str()) != INVALID_FILE_ATTRIBUTES);
	CHECK(::GetFileAttributesW((folder + L"\\wdk_license.txt").c_str()) != INVALID_FILE_ATTRIBUTES);
	for (auto name : { L"Windows.Win32.winmd", L"Windows.Wdk.winmd", L"sdk_license.txt", L"wdk_license.txt" })
		::DeleteFileW((folder + L"\\" + name).c_str());
	::RemoveDirectoryW(folder.c_str());
}

TEST_CASE("Win32 API: declarations in other languages", "[win32api]") {
	auto api = Api();
	if (!api)
		SKIP("no Windows.Win32.winmd next to the executable");
	using L = Win32Api::Language;
	auto in = [&](char const* name, L language) {
		auto entry = api->Find(name);
		REQUIRE(entry);
		return api->DeclarationIn(*entry, language);
	};
	auto has = [](std::string const& text, char const* part) {
		INFO(text);
		CHECK(text.find(part) != std::string::npos);
	};
	// a function
	has(in("CreateFileW", L::CSharp), "[DllImport(\"KERNEL32.dll\", ExactSpelling = true, SetLastError = true)]");
	has(in("CreateFileW", L::CSharp), "public static extern unsafe HANDLE CreateFileW(PWSTR lpFileName, uint dwDesiredAccess");
	has(in("CreateFileW", L::Rust), "pub fn CreateFileW(lpFileName: PWSTR, dwDesiredAccess: u32");
	has(in("CreateFileW", L::Rust), "#[link(name = \"kernel32\")]");
	has(in("CreateFileW", L::Python), "CreateFileW = ctypes.WinDLL(\"kernel32\", use_last_error=True).CreateFileW");
	has(in("CreateFileW", L::Python), "ctypes.POINTER(SECURITY_ATTRIBUTES)");
	// what it uses
	has(in("CreateFileW", L::CSharp), "it uses: FILE_CREATION_DISPOSITION, FILE_FLAGS_AND_ATTRIBUTES, FILE_SHARE_MODE, HANDLE, PWSTR, SECURITY_ATTRIBUTES");
	// a struct, and the types nested in it
	has(in("SECURITY_ATTRIBUTES", L::CSharp), "public void* lpSecurityDescriptor;");
	has(in("SECURITY_ATTRIBUTES", L::Python), "(\"lpSecurityDescriptor\", ctypes.c_void_p),");
	has(in("SYSTEM_INFO", L::Rust), "pub union SYSTEM_INFO_Anonymous {");
	has(in("SYSTEM_INFO", L::Rust), "pub Anonymous: SYSTEM_INFO_Anonymous,");
	// an enum, a typedef, constants
	has(in("FILE_SHARE_MODE", L::CSharp), "[Flags]");
	has(in("FILE_SHARE_MODE", L::CSharp), "FILE_SHARE_WRITE = 0x2,");
	has(in("FILE_SHARE_MODE", L::Rust), "pub const FILE_SHARE_WRITE: FILE_SHARE_MODE = 0x2u32;");
	has(in("HANDLE", L::Rust), "pub type HANDLE = *mut c_void;");
	has(in("HANDLE", L::Python), "HANDLE = ctypes.c_void_p");
	has(in("E_FAIL", L::CSharp), "public const int E_FAIL = unchecked((int)0x80004005);");
	has(in("MAX_PATH", L::Python), "MAX_PATH = 0x104");
	has(in("FOLDERID_Desktop", L::CSharp), "new Guid(\"B4BFCC3A-DB2C-424C-B029-7FE99A87C641\")");
	// a callback, an interface
	has(in("WNDPROC", L::Python), "WNDPROC = ctypes.WINFUNCTYPE(LRESULT, HWND, ctypes.c_uint32, WPARAM, LPARAM)");
	has(in("IClassFactory", L::CSharp), "[ComImport, Guid(\"00000001-0000-0000-C000-000000000046\")");
	has(in("IClassFactory", L::Rust), "pub base__: IUnknown_Vtbl,");
}
