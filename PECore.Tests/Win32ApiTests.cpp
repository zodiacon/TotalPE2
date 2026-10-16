#include "TestCommon.h"
#include <Win32Api.h>
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
