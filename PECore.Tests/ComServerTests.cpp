#include "TestCommon.h"
#include <ComServer.h>
#include <FrameworkBinding.h>
#include <TypeLibText.h>
#include <algorithm>

namespace {
	PEFile OpenSystem(std::wstring const& name) {
		WCHAR dir[MAX_PATH];
		::GetWindowsDirectoryW(dir, MAX_PATH);
		PEFile pe;
		REQUIRE(pe.Open(std::wstring(dir) + L"\\" + name));
		return pe;
	}

	// the TYPELIB resource of a file
	std::vector<std::byte> TypeLibOf(PCWSTR path) {
		wil::unique_hmodule dll(::LoadLibraryEx(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
		REQUIRE(dll);
		auto hRes = ::FindResource(dll.get(), MAKEINTRESOURCE(1), L"TYPELIB");
		REQUIRE(hRes);
		auto bytes = (std::byte const*)::LockResource(::LoadResource(dll.get(), hRes));
		return { bytes, bytes + ::SizeofResource(dll.get(), hRes) };
	}

	TypeLibInterface const* Find(std::vector<TypeLibInterface> const& interfaces, std::wstring const& name) {
		auto it = std::ranges::find(interfaces, name, &TypeLibInterface::Name);
		return it == interfaces.end() ? nullptr : &*it;
	}
}

TEST_CASE("A COM server: a DLL that exports DllGetClassObject, an EXE that registers its classes", "[com][system]") {
	CHECK(IsComServer(OpenSystem(LR"(System32\wiaaut.dll)")));
	CHECK(IsComServer(OpenSystem(LR"(System32\wuapihost.exe)")));		// (it imports CoRegisterClassObject)
	CHECK_FALSE(IsComServer(OpenSystem(LR"(System32\kernel32.dll)")));
}

TEST_CASE("The classes an EXE server registers: the CLSIDs it passes CoRegisterClassObject", "[com][system]") {
	auto pe = OpenSystem(LR"(System32\wuapihost.exe)");
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));
	auto classes = FindComServerClasses(pe, xrefs, [](DWORD) { return false; });
	REQUIRE(!classes.empty());
	// (it registers them in a loop, from an array of CLSIDs)
	CHECK(std::ranges::all_of(classes, [](auto const& c) { return c.Source == L"CoRegisterClassObject"; }));
	for (auto const& c : classes) {
		auto offset = pe.GetOffsetFromRVA(c.Guid);
		REQUIRE(offset);
		CHECK(LooksLikeGuid((uint8_t const*)pe.GetSpan((uint32_t)offset, 16).data()));
	}
}

TEST_CASE("The classes a DLL serves: those DllGetClassObject compares rclsid with, or of its tables", "[com][system]") {
	auto pe = OpenSystem(LR"(System32\wiaaut.dll)");
	XrefMap xrefs;
	REQUIRE(xrefs.Build(pe));
	auto classes = FindComServerClasses(pe, xrefs, [](DWORD) { return false; });
	REQUIRE(classes.size() > 3);
	// the coclasses of its type library are among them (ImageFile, DeviceManager...)
	std::vector<std::string> served;
	for (auto const& c : classes) {
		auto offset = pe.GetOffsetFromRVA(c.Guid);
		served.emplace_back((char const*)pe.GetSpan((uint32_t)offset, 16).data(), 16);
	}
	auto guids = TypeLibGuids(TypeLibOf(LR"(C:\Windows\System32\wiaaut.dll)"));
	// (the creatable ones: the others it declares, the objects its methods return, it does not serve)
	for (auto name : { L"ImageFile", L"DeviceManager", L"CommonDialog" }) {
		INFO(Narrow(name));
		auto coclass = std::ranges::find_if(guids, [&](auto const& g) { return g.Kind == TypeLibGuid::Kinds::Class && g.Name == name; });
		REQUIRE(coclass != guids.end());
		CHECK(std::ranges::find(served, coclass->Bytes) != served.end());
	}
}

TEST_CASE("The methods of a type library's interfaces, in the order of their vtables", "[com][typelib][system]") {
	SECTION("stdole2: IDispatch, and IFont's properties as MIDL names their accessors") {
		auto interfaces = TypeLibInterfaces(TypeLibOf(LR"(C:\Windows\System32\stdole2.tlb)"));
		auto dispatch = Find(interfaces, L"IDispatch");
		REQUIRE(dispatch);
		CHECK(dispatch->Methods == std::vector<std::wstring>{ L"QueryInterface", L"AddRef", L"Release", L"GetTypeInfoCount", L"GetTypeInfo",
			L"GetIDsOfNames", L"Invoke" });
		auto font = Find(interfaces, L"IFont");
		REQUIRE(font);
		REQUIRE(font->Methods.size() > 8);
		CHECK(font->Methods[0] == L"QueryInterface");
		CHECK(font->Methods[3] == L"get_Name");
		CHECK(font->Methods[4] == L"put_Name");
		CHECK(font->Methods[5] == L"get_Size");
	}
	SECTION("wiaaut: a dual interface, after IDispatch's methods") {
		auto interfaces = TypeLibInterfaces(TypeLibOf(LR"(C:\Windows\System32\wiaaut.dll)"));
		auto manager = Find(interfaces, L"IDeviceManager");
		REQUIRE(manager);
		REQUIRE(manager->Methods.size() > 7);
		CHECK(manager->Methods[6] == L"Invoke");
		CHECK(manager->Methods[7].starts_with(L"get_"));
	}
}

TEST_CASE("The binding of a driver of the framework (KMDF): its version and its table of functions", "[kmdf][system]") {
	// (a reader of the virtual addresses of an image, by its file)
	auto reader = [](PEFile const& pe) {
		return [&pe](uint64_t va, uint8_t* buffer, size_t size) {
			auto base = pe.GetImageBase();
			auto offset = va >= base ? pe.GetOffsetFromRVA((uint32_t)(va - base)) : 0;
			if (!offset || offset + size > pe.GetFileSize())
				return false;
			memcpy(buffer, pe.GetSpan((uint32_t)offset, (uint32_t)size).data(), size);
			return true;
		};
	};
	auto pe = OpenSystem(LR"(System32\drivers\WpdUpFltr.sys)");
	auto base = pe.GetImageBase();
	auto binding = FindFrameworkBinding(pe, reader(pe));
	CHECK(binding.Major == 1);
	CHECK(binding.Minor >= 15);
	CHECK(binding.Count > 400);
	REQUIRE(binding.Table > base);
	// (of 1.15 and later, a pointer to the table that WdfVersionBind sets: in the image, not the table itself)
	CHECK_FALSE(FrameworkArray(binding));
	CHECK(FrameworkArray({ .Major = 1, .Minor = 9 }));
	// not one of the framework's
	auto other = OpenSystem(LR"(System32\drivers\null.sys)");
	CHECK(FindFrameworkBinding(other, reader(other)).Table == 0);
}
