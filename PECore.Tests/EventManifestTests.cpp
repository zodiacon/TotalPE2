#include "TestCommon.h"
#include <EventManifest.h>

namespace {
	using Bytes = std::vector<uint8_t>;

	std::span<const std::byte> AsBytes(Bytes const& b) {
		return { (const std::byte*)b.data(), b.size() };
	}

	// the WEVT_TEMPLATE resource of a file of the system, empty if there is none
	Bytes Manifest(PCWSTR path) {
		wil::unique_hmodule dll(::LoadLibraryEx(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
		if (!dll)
			return {};
		auto hRes = ::FindResource(dll.get(), MAKEINTRESOURCE(1), L"WEVT_TEMPLATE");
		if (!hRes)
			return {};
		auto bytes = (const uint8_t*)::LockResource(::LoadResource(dll.get(), hRes));
		return Bytes(bytes, bytes + ::SizeofResource(dll.get(), hRes));
	}
}

TEST_CASE("What is not an event manifest", "[eventmanifest]") {
	CHECK_FALSE(ParseEventManifest({}));
	Bytes text{ 'h', 'e', 'l', 'l', 'o', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	CHECK_FALSE(ParseEventManifest(AsBytes(text)));

	// a header with more providers than there is room for
	Bytes header{ 'C', 'R', 'I', 'M', 16, 0, 0, 0, 5, 0, 1, 0, 3, 0, 0, 0 };
	CHECK_FALSE(ParseEventManifest(AsBytes(header)));
	// no providers
	header[12] = 0;
	auto empty = ParseEventManifest(AsBytes(header));
	REQUIRE(empty);
	CHECK(empty->MajorVersion == 5);
	CHECK(empty->MinorVersion == 1);
	CHECK(empty->Providers.empty());
}

TEST_CASE("The event manifest of System Restore", "[eventmanifest][system]") {
	auto data = Manifest(L"C:\\Windows\\System32\\SrEvents.dll");
	if (data.empty())
		SKIP("SrEvents.dll has no WEVT_TEMPLATE resource");
	auto m = ParseEventManifest(AsBytes(data));
	REQUIRE(m);
	CHECK(m->MajorVersion == 5);
	REQUIRE(m->Providers.size() == 1);
	auto const& p = m->Providers[0];
	CHECK(p.Guid == "{126CDB97-D346-4894-8A34-658DA5EEA1B6}");
	CHECK(p.Name == L"Microsoft-Windows-System-Restore");

	REQUIRE(p.Channels.size() == 1);
	CHECK(p.Channels[0].Name == L"Application");
	CHECK(p.Channels[0].Value == 9);
	CHECK(p.Channels[0].Flags == 1);

	auto opcode = [&](std::wstring const& name) {
		for (auto const& o : p.Opcodes)
			if (o.Name == name)
				return (int)o.Value;
		return -1;
	};
	CHECK(opcode(L"win:Info") == 0);
	CHECK(opcode(L"win:Start") == 1);
	CHECK(opcode(L"win:Stop") == 2);

	REQUIRE(p.Events.size() >= 4);
	auto const& start = p.Events[0];
	CHECK(start.Id == 8300);
	CHECK(start.Channel == 9);
	REQUIRE(start.ChannelIndex == 0);
	REQUIRE(start.OpcodeIndex >= 0);
	CHECK(p.Opcodes[start.OpcodeIndex].Name == L"win:Start");
	REQUIRE(start.LevelIndex >= 0);
	CHECK(p.Levels[start.LevelIndex].Name == L"win:Informational");
	CHECK(p.Levels[start.LevelIndex].Value == start.Level);
	CHECK(start.TaskIndex == -1);
	REQUIRE(start.KeywordIndices.size() == 2);
	CHECK(p.Keywords[start.KeywordIndices[0]].Name == L"Performance");
	CHECK(p.Keywords[start.KeywordIndices[1]].Name == L"win:ResponseTime");
	for (auto k : start.KeywordIndices)
		CHECK((start.Keywords & p.Keywords[k].Mask) != 0);

	REQUIRE(start.Template >= 0);
	auto const& temp = p.Templates[start.Template];
	REQUIRE(temp.Fields.size() == 1);
	CHECK(temp.Fields[0].Name == L"SnapshotPath");
	CHECK(std::wstring(EventInTypeName(temp.Fields[0].InType)) == L"win:UnicodeString");
	CHECK(std::wstring(EventOutTypeName(temp.Fields[0].OutType)) == L"xs:string");
	CHECK(temp.XmlSize > 0);

	auto const& stop = p.Events[1];
	REQUIRE(stop.Template >= 0);
	auto const& fields = p.Templates[stop.Template].Fields;
	REQUIRE(fields.size() == 8);
	CHECK(fields[1].Name == L"ErrorCode");
	CHECK(std::wstring(EventInTypeName(fields[1].InType)) == L"win:HexInt32");
	CHECK(std::wstring(EventOutTypeName(fields[1].OutType)) == L"win:HexInt32");
}

TEST_CASE("Value maps and bitmaps", "[eventmanifest][system]") {
	auto data = Manifest(L"C:\\Windows\\System32\\HostGuardianServiceClientResources.dll");
	if (data.empty())
		SKIP("no HostGuardianServiceClientResources.dll");
	auto m = ParseEventManifest(AsBytes(data));
	REQUIRE(m);
	REQUIRE(m->Providers.size() == 1);
	auto const& p = m->Providers[0];
	REQUIRE(p.Maps.size() == 7);
	auto map = [&](std::wstring const& name) -> EventMap const* {
		for (auto const& x : p.Maps)
			if (x.Name == name)
				return &x;
		return nullptr;
	};
	auto status = map(L"AttestationStatus");
	REQUIRE(status);
	CHECK_FALSE(status->Bitmap);
	CHECK(status->Entries.size() == 8);
	auto sub = map(L"AttestationSubstatus");
	REQUIRE(sub);
	CHECK(sub->Bitmap);
	CHECK(sub->Entries.size() == 16);

	// the channels of the provider itself
	REQUIRE(p.Channels.size() == 4);
	CHECK(p.Channels[0].Name == L"Microsoft-Windows-HostGuardianService-Client/Admin");
	CHECK(p.Channels[0].Value == 16);
	CHECK(p.Channels[0].Flags == 0);

	// fields that are shown with a map
	size_t mapped = 0;
	for (auto const& t : p.Templates)
		for (auto const& f : t.Fields)
			if (f.Map >= 0) {
				CHECK(f.Map < (int)p.Maps.size());
				mapped++;
			}
	CHECK(mapped == 10);
	CHECK(p.Tasks.size() == 5);
	CHECK(p.Events.size() == 117);
}

TEST_CASE("A truncated event manifest is read as far as it goes", "[eventmanifest][system]") {
	auto data = Manifest(L"C:\\Windows\\System32\\HostGuardianServiceClientResources.dll");
	if (data.empty())
		SKIP("no HostGuardianServiceClientResources.dll");
	for (size_t size = 0; size < data.size(); size += 7) {
		Bytes part(data.begin(), data.begin() + size);
		auto m = ParseEventManifest(AsBytes(part));
		if (m)
			for (auto const& p : m->Providers)
				for (auto const& ev : p.Events) {
					CHECK(ev.Template < (int)p.Templates.size());
					CHECK(ev.ChannelIndex < (int)p.Channels.size());
				}
	}
}
