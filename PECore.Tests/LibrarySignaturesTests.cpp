#include "TestCommon.h"
#include <LibrarySignatures.h>
#include <LibArchive.h>
#include <filesystem>
#include <random>
#include <map>
#include <fstream>

namespace {
	// a static library of the newest MSVC there is (none: the tests that need one skip themselves)
	std::wstring MsvcLibrary(std::wstring const& name, std::wstring const& arch = L"x64") {
		std::wstring found;
		for (auto root : { L"C:\\Program Files\\Microsoft Visual Studio", L"C:\\Program Files (x86)\\Microsoft Visual Studio" }) {
			std::error_code ec;
			if (!std::filesystem::exists(root, ec))
				continue;
			for (auto const& version : std::filesystem::directory_iterator(root, ec))
				for (auto const& edition : std::filesystem::directory_iterator(version.path(), ec)) {
					auto tools = edition.path() / L"VC" / L"Tools" / L"MSVC";
					if (!std::filesystem::exists(tools, ec))
						continue;
					for (auto const& msvc : std::filesystem::directory_iterator(tools, ec)) {
						auto lib = msvc.path() / L"lib" / arch / name;
						if (std::filesystem::exists(lib, ec) && lib.wstring() > found)
							found = lib.wstring();
					}
				}
		}
		return found;
	}
}

TEST_CASE("The functions of a static library are found by their code", "[signatures]") {
	auto path = MsvcLibrary(L"libvcruntime.lib");
	if (path.empty())
		SKIP("No MSVC libraries");
	LibArchive lib;
	REQUIRE(lib.Open(path));
	LibrarySignatures signatures;
	REQUIRE(signatures.AddLibrary(lib) > 100);
	signatures.Finish();

	// each function (not ambiguous) is found at its code, whatever the linker writes in it
	std::mt19937 random(42);
	size_t checked = 0, withCalls = 0;
	for (auto const& s : signatures.Signatures()) {
		if (s.Ambiguous)
			continue;
		auto code = s.Code;
		for (auto const& [offset, count] : s.Masked)
			for (uint8_t i = 0; i < count; i++)
				code[offset + i] = (uint8_t)random();
		code.insert(code.end(), { 0xCC, 0xCC, 0xCC, 0xCC });	// (the padding after it)
		auto found = signatures.Find(s.Machine, code);
		REQUIRE(found);
		CHECK(found->Code.size() >= s.Code.size());
		checked++;
		// what it calls: the target from the code
		if (!s.Calls.empty() && s.Machine == IMAGE_FILE_MACHINE_AMD64) {
			auto offset = s.Calls[0].first;
			int32_t displacement = 0x1000;
			memcpy(code.data() + offset, &displacement, 4);
			auto calls = LibrarySignatures::CallsOf(s, code, 0x140001000);
			REQUIRE(!calls.empty());
			CHECK(calls[0].first == 0x140001000 + offset + 4 + 0x1000);
			CHECK(calls[0].second == s.Calls[0].second);
			withCalls++;
		}
	}
	CHECK(checked > 100);
	CHECK(withCalls > 10);

	// code that is not of it
	std::vector<uint8_t> other(64, 0x90);
	CHECK(signatures.Find(IMAGE_FILE_MACHINE_AMD64, other) == nullptr);
}

TEST_CASE("Signatures are saved and read back", "[signatures]") {
	auto path = MsvcLibrary(L"libvcruntime.lib");
	if (path.empty())
		SKIP("No MSVC libraries");
	LibArchive lib;
	REQUIRE(lib.Open(path));
	LibrarySignatures signatures;
	signatures.AddLibrary(lib);
	signatures.Finish();
	auto file = (std::filesystem::temp_directory_path() / std::format(L"TotalPE.{}.sig", ::GetCurrentProcessId())).wstring();
	REQUIRE(signatures.Save(file));
	LibrarySignatures read;
	REQUIRE(read.Load(file));
	std::filesystem::remove(file);
	REQUIRE(read.Size() == signatures.Size());
	for (size_t i = 0; i < read.Size(); i++) {
		auto const& a = signatures.Signatures()[i];
		auto const& b = read.Signatures()[i];
		CHECK((a.Name == b.Name && a.Code == b.Code && a.Masked == b.Masked && a.Calls == b.Calls && a.Ambiguous == b.Ambiguous));
	}
	// not one
	auto bad = (std::filesystem::temp_directory_path() / std::format(L"TotalPE.{}.bad", ::GetCurrentProcessId())).wstring();
	{
		std::ofstream out(bad, std::ios::binary);
		out << "not signatures";
	}
	LibrarySignatures none;
	CHECK_FALSE(none.Load(bad));
	std::filesystem::remove(bad);
}

// The file of signatures TotalPE has (TOTALPE_SIG_OUT), from static libraries (TOTALPE_SIG_LIBS: their paths, separated by ;)
TEST_CASE("Build a file of signatures", "[.][sigbuild]") {
	char out[MAX_PATH]{}, libs[32768]{};
	::GetEnvironmentVariableA("TOTALPE_SIG_OUT", out, sizeof(out));
	::GetEnvironmentVariableA("TOTALPE_SIG_LIBS", libs, sizeof(libs));
	REQUIRE(out[0]);
	LibrarySignatures signatures;
	std::string list = libs;
	for (size_t start = 0; start < list.size(); ) {
		auto end = list.find(';', start);
		if (end == std::string::npos)
			end = list.size();
		auto path = list.substr(start, end - start);
		start = end + 1;
		if (path.empty())
			continue;
		LibArchive lib;
		if (!lib.Open(std::wstring(path.begin(), path.end()))) {
			WARN("cannot open " << path);
			continue;
		}
		auto added = signatures.AddLibrary(lib);
		WARN(path << ": " << added);
	}
	signatures.Finish();
	size_t ambiguous = std::count_if(signatures.Signatures().begin(), signatures.Signatures().end(), [](auto const& s) { return s.Ambiguous; });
	WARN("signatures: " << signatures.Size() << ", ambiguous: " << ambiguous);
	REQUIRE(signatures.Save(std::wstring(out, out + strlen(out))));
}

// How many of the functions of an image (TOTALPE_SIG_EXE: those of its exception data) a file of signatures (TOTALPE_SIG_FILE)
// names, and how many more what they call names
TEST_CASE("Name the functions of an image", "[.][sigmatch]") {
	char file[MAX_PATH]{}, exe[MAX_PATH]{};
	::GetEnvironmentVariableA("TOTALPE_SIG_FILE", file, sizeof(file));
	::GetEnvironmentVariableA("TOTALPE_SIG_EXE", exe, sizeof(exe));
	LibrarySignatures signatures;
	REQUIRE(signatures.Load(std::wstring(file, file + strlen(file))));
	PEFile pe;
	REQUIRE(pe.Open(std::wstring(exe, exe + strlen(exe))));
	auto machine = pe.GetFileInfo()->IsPE64 ? pe.GetNTHeader()->NTHdr64.FileHeader.Machine : pe.GetNTHeader()->NTHdr32.FileHeader.Machine;
	auto base = pe.GetImageBase();
	std::map<uint32_t, std::string> named, called;
	size_t total = 0;
	for (auto const& e : *pe.GetExceptions()) {
		auto rva = e.RuntimeFuncEntry.BeginAddress;
		total++;
		auto offset = pe.GetOffsetFromRVA(rva);
		auto size = std::min<uint64_t>(0x4000, pe.GetFileSize() - offset);
		auto span = pe.GetSpan((uint32_t)offset, (uint32_t)size);
		std::span<const uint8_t> code((uint8_t const*)span.data(), span.size());
		if (auto s = signatures.Find(machine, code)) {
			named[rva] = s->Name;
			for (auto const& [target, name] : LibrarySignatures::CallsOf(*s, code, base + rva))
				called.try_emplace((uint32_t)(target - base), name);
		}
	}
	size_t more = 0;
	for (auto const& [rva, name] : called)
		if (!named.contains(rva))
			more++;
	WARN("functions " << total << ", named " << named.size() << ", and by calls " << more);
	// (TOTALPE_SIG_ALL: all of them, and those named by calls)
	bool all = ::GetEnvironmentVariableA("TOTALPE_SIG_ALL", nullptr, 0) != 0;
	if (all)
		for (auto const& [rva, name] : called)
			named.try_emplace(rva, name);
	int shown = 0;
	for (auto const& [rva, name] : named)
		if (all || shown++ < 15)
			WARN(std::format("{:X} {}", rva, name));
}
