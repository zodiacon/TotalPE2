#include "TestCommon.h"
#include <WinRtServer.h>
#include <algorithm>
#include <format>
#include <iostream>

// The runtime classes of a file (TOTALPE_WINRT_FILE), and the interfaces of the first ones
TEST_CASE("The runtime classes of a file", "[.][winrt-file]") {
	char file[MAX_PATH]{};
	::GetEnvironmentVariableA("TOTALPE_WINRT_FILE", file, sizeof(file));
	std::wstring path(file, file + strlen(file));
	PEFile pe;
	REQUIRE(pe.Open(path));
	std::cout << "server: " << IsWinRtServer(pe) << "\n";
	int shown = 0;
	for (auto const& c : FindWinRtClasses(pe, path)) {
		std::cout << std::format("{} | {} | entry {:X} creator {:X} name {:X} trust {:X} | type {} threading {} trust {} | {}\n", Narrow(c.Name),
			Narrow(c.Source), c.Entry, c.Creator, c.NameFunction, c.TrustFunction, c.ActivationType, c.Threading, c.TrustLevel, Narrow(c.Server));
		if (shown++ < 3) {
			auto md = FindWinRtClassMetadata(c.Name);
			std::cout << std::format("  {} activatable {}\n", Narrow(md.File), md.Activatable);
			for (auto const& i : md.Interfaces) {
				std::cout << std::format("  {} {} {}\n", Narrow(i.Role), Narrow(i.Name), Narrow(i.Guid));
				for (auto const& m : i.Methods)
					std::cout << std::format("    {} {}\n", m.Slot, Narrow(m.Declaration));
			}
		}
	}
}

TEST_CASE("A server of runtime classes: it exports DllGetActivationFactory", "[winrt][system]") {
	for (auto [name, server] : { std::pair{ LR"(C:\Windows\System32\Windows.Globalization.dll)", true }, { LR"(C:\Windows\System32\kernel32.dll)", false } }) {
		PEFile pe;
		REQUIRE(pe.Open(name));
		CHECK(IsWinRtServer(pe) == server);
	}
}

TEST_CASE("The runtime classes of WRL's creator map, and of the registry", "[winrt][system]") {
	// (x86: the creator map is in the code section, its names' functions are mov eax, offset; ret)
	for (auto path : { LR"(C:\Windows\System32\Windows.Globalization.dll)", LR"(C:\Windows\SysWOW64\Windows.Globalization.dll)" }) {
		INFO(Narrow(path));
		PEFile pe;
		REQUIRE(pe.Open(path));
		auto classes = FindWinRtClasses(pe, path);
		REQUIRE(classes.size() > 10);
		CHECK(std::ranges::is_sorted(classes, {}, &WinRtClass::Name));
		auto calendar = std::ranges::find(classes, std::wstring(L"Windows.Globalization.Calendar"), &WinRtClass::Name);
		REQUIRE(calendar != classes.end());
		CHECK(calendar->Source.starts_with(L"creator map (WRL)"));
		CHECK(calendar->Entry != 0);
		CHECK(calendar->Creator != 0);
		CHECK(calendar->NameFunction != 0);
		CHECK(calendar->TrustFunction != 0);
		// (the registry's DLL is System32's: the 64-bit one is registered, not the other)
		bool system32 = std::wstring_view(path).find(L"System32") != std::wstring_view::npos;
		CHECK((calendar->ActivationType == 0) == system32);
		CHECK(calendar->Source.ends_with(L", registry") == system32);
	}
}

TEST_CASE("The interfaces of a runtime class, of the metadata of Windows", "[winrt][system]") {
	auto md = FindWinRtClassMetadata(L"Windows.Globalization.Calendar");
	CHECK(md.File == L"Windows.Globalization.winmd");
	// ([Activatable(1)]: new Calendar(); [Activatable(typeof(ICalendarFactory))], [Activatable(typeof(ICalendarFactory2))])
	CHECK(md.Activatable);
	auto factory = std::ranges::find(md.Interfaces, std::wstring(L"Windows.Globalization.ICalendarFactory"), &WinRtInterface::Name);
	REQUIRE(factory != md.Interfaces.end());
	CHECK(factory->Role == L"factory");
	CHECK(factory->Guid == L"{83F58412-E56B-4C75-A66E-0F63D57758A6}");
	REQUIRE(!factory->Methods.empty());
	CHECK(factory->Methods[0].Name == L"CreateCalendarDefaultCalendarAndClock");
	CHECK(factory->Methods[0].Slot == 6);
	CHECK(factory->Methods[0].Declaration ==
		L"Windows.Globalization.Calendar CreateCalendarDefaultCalendarAndClock(Windows.Foundation.Collections.IIterable<string> languages)");
	auto calendar = std::ranges::find(md.Interfaces, std::wstring(L"Windows.Globalization.ICalendar"), &WinRtInterface::Name);
	REQUIRE(calendar != md.Interfaces.end());
	CHECK(calendar->Role == L"instance");
	CHECK(calendar->Methods.size() > 50);
	// statics; a class of no file
	auto languages = FindWinRtClassMetadata(L"Windows.Globalization.ApplicationLanguages");
	CHECK(std::ranges::any_of(languages.Interfaces, [](auto const& i) { return i.Role == L"statics"; }));
	CHECK_FALSE(languages.Activatable);
	CHECK(FindWinRtClassMetadata(L"Contoso.NoSuchClass").File.empty());
}
