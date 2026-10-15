#include "TestCommon.h"
#include <Rtti.h>
#include <algorithm>

TEST_CASE("The name of a type of the run-time type information", "[rtti]") {
	CHECK(RttiTypeName(".?AVexception@std@@") == "std::exception");
	CHECK(RttiTypeName(".?AUIUnknown@@") == "IUnknown");
	CHECK(RttiTypeName("exception").empty());
}

// The classes of a file (TOTALPE_RTTI_FILE)
TEST_CASE("The classes of a file", "[.][rtti-file]") {
	char file[MAX_PATH]{};
	::GetEnvironmentVariableA("TOTALPE_RTTI_FILE", file, sizeof(file));
	PEFile pe;
	REQUIRE(pe.Open(std::wstring(file, file + strlen(file))));
	auto classes = ReadRtti(pe);
	WARN("classes: " << classes.size());
	int shown = 0;
	for (auto const& c : classes) {
		if (shown++ >= 12)
			break;
		std::string bases;
		for (auto const& b : c.Bases)
			bases += " " + b;
		WARN(c.Name << " vtables " << c.Vtables.size() << " functions " << c.Vtables[0].Functions.size() << " at " << std::hex << c.Vtables[0].Rva << " bases:" << bases);
	}
}

TEST_CASE("The classes of a program with run-time type information", "[rtti]") {
	PEFile pe;
	REQUIRE(pe.Open(LR"(C:\Windows\System32\msvcp_win.dll)"));
	auto classes = ReadRtti(pe);
	REQUIRE(classes.size() > 10);
	// std::_Facet_base: the base of the facets, with its vtable of functions in the code
	auto facet = std::ranges::find_if(classes, [](auto const& c) { return c.Name == "std::_Facet_base"; });
	REQUIRE(facet != classes.end());
	REQUIRE(!facet->Vtables.empty());
	CHECK(!facet->Vtables[0].Functions.empty());
	CHECK(facet->Vtables[0].Offset == 0);
	auto derived = std::ranges::count_if(classes, [](auto const& c) { return std::ranges::find(c.Bases, "std::_Facet_base") != c.Bases.end(); });
	CHECK(derived > 3);
	// in the order of their names
	CHECK(std::ranges::is_sorted(classes, {}, &RttiClass::Name));
}
