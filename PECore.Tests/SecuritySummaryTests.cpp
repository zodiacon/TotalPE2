#include "TestCommon.h"
#include <SecuritySummary.h>

namespace {
	SecurityFeature const* Feature(std::vector<SecurityFeature> const& features, std::wstring_view name) {
		auto it = std::find_if(features.begin(), features.end(), [&](auto& f) { return f.Name == name; });
		return it == features.end() ? nullptr : &*it;
	}

	SecurityState StateOf(std::vector<SecurityFeature> const& features, std::wstring_view name) {
		auto f = Feature(features, name);
		INFO(Narrow(std::wstring(name)));
		REQUIRE(f);
		return f->State;
	}

	std::vector<SecurityFeature> Analyze(std::vector<uint8_t> const& bytes) {
		TempFile file(bytes, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(file.Path()));
		return AnalyzeSecurity(pe, false);
	}

	void SetDllCharacteristics(SyntheticPE const& spec, std::vector<uint8_t>& bytes, uint16_t flags) {
		memcpy(bytes.data() + spec.OptionalHeaderOffset() + 70, &flags, 2);
	}
}

TEST_CASE("The flags of the headers", "[security]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	CAPTURE(spec.Is64);
	auto bytes = spec.Build();

	SECTION("none") {
		SetDllCharacteristics(spec, bytes, 0);
		auto f = Analyze(bytes);
		CHECK(StateOf(f, L"ASLR") == SecurityState::Disabled);
		CHECK(StateOf(f, L"High-entropy ASLR") == (spec.Is64 ? SecurityState::Disabled : SecurityState::NotApplicable));
		// DEP is always on in 64-bit processes
		CHECK(StateOf(f, L"DEP") == (spec.Is64 ? SecurityState::Enabled : SecurityState::Disabled));
		CHECK(StateOf(f, L"Control Flow Guard") == SecurityState::Disabled);
		CHECK(StateOf(f, L"CET shadow stack") == SecurityState::Disabled);
		CHECK(StateOf(f, L"SafeSEH") == (spec.Is64 ? SecurityState::NotApplicable : SecurityState::Disabled));
		CHECK(StateOf(f, L"Stack cookies (/GS)") == SecurityState::Disabled);
		CHECK(StateOf(f, L"Signature") == SecurityState::Disabled);
		CHECK(Feature(f, L"Force Integrity") == nullptr);
		CHECK(StateOf(f, L"No writable code") == SecurityState::Enabled);
		CHECK(Feature(f, L"Strong name") == nullptr);
	}
	SECTION("ASLR without relocations is partial") {
		SetDllCharacteristics(spec, bytes, IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA |
			IMAGE_DLLCHARACTERISTICS_NX_COMPAT | IMAGE_DLLCHARACTERISTICS_NO_SEH | IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY);
		auto f = Analyze(bytes);
		CHECK(StateOf(f, L"ASLR") == SecurityState::Partial);
		CHECK(StateOf(f, L"High-entropy ASLR") == (spec.Is64 ? SecurityState::Enabled : SecurityState::NotApplicable));
		CHECK(StateOf(f, L"DEP") == SecurityState::Enabled);
		CHECK(StateOf(f, L"SafeSEH") == (spec.Is64 ? SecurityState::NotApplicable : SecurityState::Enabled));
		CHECK(StateOf(f, L"Force Integrity") == SecurityState::Enabled);
	}
	SECTION("CFG without instrumented code is partial") {
		SetDllCharacteristics(spec, bytes, IMAGE_DLLCHARACTERISTICS_GUARD_CF);
		auto f = Analyze(bytes);
		CHECK(StateOf(f, L"Control Flow Guard") == SecurityState::Partial);
		CHECK(StateOf(f, L"Extended Flow Guard (XFG)") == SecurityState::Disabled);
	}
	SECTION("a writable code section") {
		DWORD c = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
		memcpy(bytes.data() + spec.SectionHeaderOffset(0) + 36, &c, 4);
		auto f = Analyze(bytes);
		CHECK(StateOf(f, L"No writable code") == SecurityState::Disabled);
		CHECK(Feature(f, L"No writable code")->Details.find(L".text") != std::wstring::npos);
	}
}

TEST_CASE("The strong name of a .NET file", "[security][clr]") {
	SyntheticPE spec;
	spec.Clr = true;
	auto f = Analyze(spec.Build());
	CHECK(StateOf(f, L"Strong name") == SecurityState::Disabled);
	// IL only: the protections of native code do not apply
	CHECK(StateOf(f, L"Control Flow Guard") == SecurityState::NotApplicable);
	CHECK(StateOf(f, L"Stack cookies (/GS)") == SecurityState::NotApplicable);
}

TEST_CASE("The execution level of a manifest", "[security]") {
	auto t = ParseManifestTrust(R"(<assembly><trustInfo><security><requestedPrivileges>
		<requestedExecutionLevel level="requireAdministrator" uiAccess='false'/></requestedPrivileges></security></trustInfo></assembly>)");
	CHECK(t.Level == L"requireAdministrator");
	CHECK(t.UiAccess == L"false");
	t = ParseManifestTrust(R"(<requestedExecutionLevel uiAccess = "true" level = "asInvoker" />)");
	CHECK(t.Level == L"asInvoker");
	CHECK(t.UiAccess == L"true");
	t = ParseManifestTrust(R"(<assembly><dpiAware>true</dpiAware></assembly>)");
	CHECK(t.Level.empty());
	// an attribute whose name ends with the one looked for is another attribute
	t = ParseManifestTrust(R"(<requestedExecutionLevel xlevel="no" level="highestAvailable"/>)");
	CHECK(t.Level == L"highestAvailable");
}

TEST_CASE("The protections of system files", "[security][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);

	SECTION("kernel32.dll") {
		PEFile pe;
		REQUIRE(pe.Open(std::wstring(dir) + L"\\kernel32.dll"));
		auto f = AnalyzeSecurity(pe, true);
		CHECK(StateOf(f, L"ASLR") == SecurityState::Enabled);
		CHECK(StateOf(f, L"High-entropy ASLR") == SecurityState::Enabled);
		CHECK(StateOf(f, L"DEP") == SecurityState::Enabled);
		CHECK(StateOf(f, L"Control Flow Guard") == SecurityState::Enabled);
		CHECK(StateOf(f, L"CET shadow stack") == SecurityState::Enabled);
		CHECK(StateOf(f, L"Stack cookies (/GS)") == SecurityState::Enabled);
		CHECK(StateOf(f, L"No writable code") == SecurityState::Enabled);
		CHECK(StateOf(f, L"Signature") == SecurityState::Enabled);
	}
	SECTION("a file signed in a catalog of the system") {
		PEFile pe;
		if (!pe.Open(std::wstring(dir) + L"\\alg.exe"))
			SKIP("alg.exe is not on this machine");
		auto f = AnalyzeSecurity(pe, true);
		auto sig = Feature(f, L"Signature");
		REQUIRE(sig);
		INFO(Narrow(sig->Details));
		CHECK(sig->State == SecurityState::Enabled);
		CHECK(sig->Details.find(L"catalog") != std::wstring::npos);
	}
	SECTION("a program that asks for administrator rights") {
		PEFile pe;
		if (!pe.Open(std::wstring(dir) + L"\\mmc.exe"))
			SKIP("mmc.exe is not on this machine");
		auto f = AnalyzeSecurity(pe, false);
		auto level = Feature(f, L"Execution level");
		REQUIRE(level);
		CHECK(level->Details.starts_with(L"highestAvailable"));
	}
}
