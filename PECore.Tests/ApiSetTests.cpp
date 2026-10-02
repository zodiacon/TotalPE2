#include "TestCommon.h"
#include <ApiSet.h>
#include "ByteWriter.h"

namespace {
	struct SchemaEntry {
		std::wstring Name;
		std::wstring Host;				// empty: no host on this system
		std::wstring Alias, AliasHost;	// a second value that applies only to one importing module
	};

	// A version 6 schema: header, entries, values, strings.
	ByteWriter BuildSchema(std::vector<SchemaEntry> const& entries, uint32_t version = 6) {
		const uint32_t headerSize = 28, entrySize = 24, valueSize = 20;
		uint32_t valuesStart = headerSize + (uint32_t)entries.size() * entrySize;
		uint32_t totalValues = 0;
		for (auto& e : entries)
			totalValues += e.Alias.empty() ? 1 : 2;
		uint32_t stringsStart = valuesStart + totalValues * valueSize;

		// the strings, in UTF-16 without terminators
		ByteWriter strings;
		auto addString = [&](std::wstring const& s) {
			uint32_t offset = stringsStart + (uint32_t)strings.Bytes.size();
			for (auto c : s)
				strings.U16(c);
			return std::pair<uint32_t, uint32_t>{ offset, (uint32_t)s.size() * 2 };
		};

		ByteWriter w, entriesW, valuesW;
		uint32_t valueIndex = 0;
		for (auto& e : entries) {
			auto name = addString(e.Name);
			uint32_t count = e.Alias.empty() ? 1 : 2;
			entriesW.U32(1).U32(name.first).U32(name.second).U32(name.second - 6).U32(valuesStart + valueIndex * valueSize).U32(count);
			if (!e.Alias.empty()) {
				auto alias = addString(e.Alias);
				auto host = addString(e.AliasHost);
				valuesW.U32(0).U32(alias.first).U32(alias.second).U32(host.first).U32(host.second);
			}
			auto host = addString(e.Host);
			valuesW.U32(0).U32(0).U32(0).U32(host.first).U32(host.second);	// the default: no importer name
			valueIndex += count;
		}
		w.U32(version).U32(stringsStart + (uint32_t)strings.Bytes.size()).U32(0).U32((uint32_t)entries.size()).U32(headerSize).U32(0).U32(31);
		w.Bytes.insert(w.Bytes.end(), entriesW.Bytes.begin(), entriesW.Bytes.end());
		w.Bytes.insert(w.Bytes.end(), valuesW.Bytes.begin(), valuesW.Bytes.end());
		w.Bytes.insert(w.Bytes.end(), strings.Bytes.begin(), strings.Bytes.end());
		return w;
	}

	ByteWriter SampleSchema() {
		return BuildSchema({
			{ L"api-ms-win-core-heap-l1-1-0", L"kernelbase.dll" },
			{ L"api-ms-win-core-heap-l1-2-0", L"kernelbase.dll" },
			{ L"ext-ms-win-ntuser-window-l1-1-6", L"user32.dll", L"someimporter.dll", L"other.dll" },
			{ L"api-ms-win-gone-l1-1-0", L"" },
			{ L"API-MS-Win-Core-File-L1-1-0", L"KernelBase.dll" },
		});
	}
}

TEST_CASE("API set names", "[apiset]") {
	CHECK(ApiSetMap::IsApiSetName("api-ms-win-core-heap-l1-1-0.dll"));
	CHECK(ApiSetMap::IsApiSetName("EXT-MS-WIN-NTUSER-WINDOW-L1-1-0.DLL"));
	CHECK(ApiSetMap::IsApiSetName("api-ms-win-core-heap-l1-1-0"));
	CHECK_FALSE(ApiSetMap::IsApiSetName("kernel32.dll"));
	CHECK_FALSE(ApiSetMap::IsApiSetName("api"));
	CHECK_FALSE(ApiSetMap::IsApiSetName("api-"));
	CHECK_FALSE(ApiSetMap::IsApiSetName(""));
	CHECK_FALSE(ApiSetMap::IsApiSetName("capi-ms-win.dll"));
}

TEST_CASE("The contract name leaves out the extension and the version", "[apiset]") {
	CHECK(ApiSetMap::ContractName("api-ms-win-core-heap-l1-1-0.dll") == "api-ms-win-core-heap-l1-1");
	CHECK(ApiSetMap::ContractName("API-MS-WIN-CORE-HEAP-L1-1-0.DLL") == "api-ms-win-core-heap-l1-1");
	CHECK(ApiSetMap::ContractName("api-ms-win-core-heap-l1-1-12") == "api-ms-win-core-heap-l1-1");
	CHECK(ApiSetMap::ContractName("api-ms-win-core-heap-l1-1") == "api-ms-win-core-heap-l1");	// the last number is always the version
	CHECK(ApiSetMap::ContractName("kernel32.dll") == "kernel32");
	CHECK(ApiSetMap::ContractName("name-without-number") == "name-without-number");
	CHECK(ApiSetMap::ContractName("trailing-") == "trailing-");
	CHECK(ApiSetMap::ContractName("") == "");
}

TEST_CASE("A schema is parsed and names are resolved", "[apiset]") {
	ApiSetMap map;
	REQUIRE(map.Parse(SampleSchema().Span()));
	CHECK(map.Loaded());
	CHECK(map.Count() == 4);	// the API set without a host is not included

	SECTION("with and without the extension") {
		CHECK(map.Resolve("api-ms-win-core-heap-l1-1-0.dll") == "kernelbase.dll");
		CHECK(map.Resolve("api-ms-win-core-heap-l1-1-0") == "kernelbase.dll");
	}
	SECTION("a different minor version resolves to the same host") {
		CHECK(map.Resolve("api-ms-win-core-heap-l1-1-1.dll") == "kernelbase.dll");
		CHECK(map.Resolve("api-ms-win-core-heap-l1-1-7.dll") == "kernelbase.dll");
	}
	SECTION("the contract matters, the case does not") {
		CHECK(map.Resolve("API-MS-WIN-CORE-HEAP-L1-2-0.DLL") == "kernelbase.dll");
		CHECK(map.Resolve("api-ms-win-core-file-l1-1-0.dll") == "kernelbase.dll");	// and hosts are lowercase
	}
	SECTION("the default host is used when there are importer specific values") {
		CHECK(map.Resolve("ext-ms-win-ntuser-window-l1-1-6.dll") == "user32.dll");
	}
	SECTION("names that do not resolve") {
		CHECK_FALSE(map.Resolve("api-ms-win-core-heap-l1-3-0.dll").has_value());	// another contract
		CHECK_FALSE(map.Resolve("api-ms-win-gone-l1-1-0.dll").has_value());			// no host
		CHECK_FALSE(map.Resolve("kernel32.dll").has_value());						// not an API set
		CHECK_FALSE(map.Resolve("").has_value());
	}
}

TEST_CASE("Schemas that are not understood are rejected", "[apiset]") {
	ApiSetMap map;

	SECTION("empty, tiny") {
		CHECK_FALSE(map.Parse({}));
		ByteWriter w;
		w.U32(6);
		CHECK_FALSE(map.Parse(w.Span()));
	}
	SECTION("other versions") {
		CHECK_FALSE(map.Parse(BuildSchema({ { L"api-ms-win-core-heap-l1-1-0", L"kernelbase.dll" } }, 4).Span()));
		CHECK_FALSE(map.Parse(BuildSchema({ { L"api-ms-win-core-heap-l1-1-0", L"kernelbase.dll" } }, 2).Span()));
		CHECK_FALSE(map.Loaded());
	}
	SECTION("a count that promises more entries than there are") {
		auto w = SampleSchema();
		w.Bytes[12] = 0xFF;
		w.Bytes[13] = 0xFF;
		CHECK_FALSE(map.Parse(w.Span()));
	}
	SECTION("a parse replaces what was loaded before") {
		REQUIRE(map.Parse(SampleSchema().Span()));
		CHECK_FALSE(map.Parse(BuildSchema({}, 6).Span()));
		CHECK_FALSE(map.Resolve("api-ms-win-core-heap-l1-1-0.dll").has_value());
	}
}

TEST_CASE("Truncated and damaged schemas never read outside the data", "[apiset]") {
	auto schema = SampleSchema();

	SECTION("every truncation") {
		for (size_t length = 0; length < schema.Bytes.size(); length++) {
			ApiSetMap map;
			map.Parse(schema.Span().first(length));
		}
	}
	SECTION("every byte damaged") {
		for (size_t i = 0; i < schema.Bytes.size(); i++)
			for (uint8_t value : { (uint8_t)0x00, (uint8_t)0xFF, (uint8_t)0x80 }) {
				auto copy = schema;
				copy.Bytes[i] = value;
				ApiSetMap map;
				map.Parse(copy.Span());
				map.Resolve("api-ms-win-core-heap-l1-1-0.dll");
			}
	}
}

TEST_CASE("The API set schema of this Windows", "[apiset][system]") {
	auto& map = ApiSetMap::System();
	if (!map.Loaded())
		SKIP("the API set schema of this system could not be read");

	CHECK(map.Count() > 100);
	// these contracts have been hosted by the same DLLs since Windows 10
	CHECK(map.Resolve("api-ms-win-core-heap-l1-1-0.dll") == "kernelbase.dll");
	CHECK(map.Resolve("API-MS-WIN-CORE-HEAP-L1-1-0.DLL") == "kernelbase.dll");
	CHECK(map.Resolve("api-ms-win-core-file-l1-1-0.dll") == "kernelbase.dll");
	CHECK(map.Resolve("api-ms-win-core-synch-l1-2-0.dll") == "kernelbase.dll");
	CHECK_FALSE(map.Resolve("kernel32.dll").has_value());
	CHECK_FALSE(map.Resolve("api-ms-win-this-does-not-exist-l1-1-0.dll").has_value());
}

TEST_CASE("API sets are described for the views", "[apiset]") {
	CHECK(DescribeApiSet("kernel32.dll").empty());			// not an API set
	CHECK(DescribeApiSet("").empty());

	auto& system = ApiSetMap::System();
	if (!system.Loaded())
		SKIP("the API set schema of this system could not be read");
	CHECK(DescribeApiSet("api-ms-win-core-heap-l1-1-0.dll") == L"kernelbase.dll");
	CHECK(DescribeApiSet("api-ms-win-core-heap-l1-1-0") == L"kernelbase.dll");		// forwarders name the module without ".dll"
	CHECK(DescribeApiSet("api-ms-win-this-does-not-exist-l1-1-0.dll") == L"(not present on this system)");
}
