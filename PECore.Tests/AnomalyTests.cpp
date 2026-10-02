#include "TestCommon.h"
#include <PEAnomalies.h>

namespace {
	std::vector<Anomaly> Analyze(std::vector<uint8_t> const& bytes) {
		TempFile file(bytes, L".dll");
		PEFile pe;
		REQUIRE(pe.Open(file.Path()));
		return FindAnomalies(pe);
	}

	bool Has(std::vector<Anomaly> const& anomalies, AnomalySeverity severity, std::wstring const& category, std::wstring const& text) {
		for (auto& a : anomalies)
			if (a.Severity == severity && a.Category == category && a.Message.find(text) != std::wstring::npos)
				return true;
		return false;
	}

	std::string Describe(std::vector<Anomaly> const& anomalies) {
		std::string s;
		for (auto& a : anomalies)
			s += Narrow(std::wstring(AnomalySeverityToString(a.Severity)) + L" [" + a.Category + L"] " + a.Message) + "\n";
		return s;
	}

	// editing a built file in place
	struct Editor {
		SyntheticPE Spec;
		std::vector<uint8_t> Bytes;

		explicit Editor(SyntheticPE spec = {}) : Spec(spec), Bytes(spec.Build()) {}
		IMAGE_FILE_HEADER& FileHeader() { return *reinterpret_cast<IMAGE_FILE_HEADER*>(Bytes.data() + SyntheticPE::ELfanew + 4); }
		IMAGE_SECTION_HEADER& Section(int i) { return *reinterpret_cast<IMAGE_SECTION_HEADER*>(Bytes.data() + Spec.SectionHeaderOffset(i)); }
		IMAGE_DATA_DIRECTORY& Directory(int i) {
			auto base = Bytes.data() + Spec.OptionalHeaderOffset() + (Spec.Is64 ? 112 : 96);
			return reinterpret_cast<IMAGE_DATA_DIRECTORY*>(base)[i];
		}
		// the fields of the optional header that PE32 and PE32+ share at the same offset
		uint32_t& Field32(size_t offsetInOptionalHeader) { return *reinterpret_cast<uint32_t*>(Bytes.data() + Spec.OptionalHeaderOffset() + offsetInOptionalHeader); }
		uint32_t& EntryPoint() { return Field32(16); }
		uint32_t& SizeOfImage() { return Field32(56); }
		uint32_t& SizeOfHeaders() { return Field32(60); }
		uint32_t& CheckSum() { return Field32(64); }

		// pseudo random bytes: the entropy of compressed data
		void Randomize(size_t offset, size_t size) {
			uint32_t state = 12345;
			for (size_t i = 0; i < size; i++) {
				state = state * 1664525u + 1013904223u;
				Bytes[offset + i] = (uint8_t)(state >> 24);
			}
		}
	};
}

TEST_CASE("Entropy", "[anomalies]") {
	SECTION("constant data has none") {
		std::vector<uint8_t> zeros(1000, 0);
		CHECK(ComputeEntropy(zeros.data(), zeros.size()) == 0.0);
		CHECK(ComputeEntropy(nullptr, 0) == 0.0);
	}
	SECTION("two equally likely values carry one bit") {
		std::vector<uint8_t> data;
		for (int i = 0; i < 500; i++) { data.push_back(0); data.push_back(1); }
		CHECK(ComputeEntropy(data.data(), data.size()) == Catch::Approx(1.0));
	}
	SECTION("all byte values equally likely carry eight bits") {
		std::vector<uint8_t> data;
		for (int i = 0; i < 4 * 256; i++)
			data.push_back((uint8_t)i);
		CHECK(ComputeEntropy(data.data(), data.size()) == Catch::Approx(8.0));
	}
	SECTION("text is in between") {
		std::string text = "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog.";
		auto e = ComputeEntropy((const uint8_t*)text.data(), text.size());
		CHECK(e > 3.0);
		CHECK(e < 5.0);
	}
}

TEST_CASE("The PE checksum", "[anomalies]") {
	SECTION("sums the 16-bit words and adds the size") {
		std::vector<uint8_t> data{ 1, 0, 2, 0, 3, 0, 4, 0 };
		CHECK(ComputePEChecksum(data.data(), data.size(), 100) == 10 + 8);
	}
	SECTION("ignores the checksum field") {
		std::vector<uint8_t> data{ 1, 0, 2, 0, 3, 0, 4, 0 };
		CHECK(ComputePEChecksum(data.data(), data.size(), 4) == 3 + 8);
	}
	SECTION("an odd trailing byte counts as a word") {
		std::vector<uint8_t> data{ 1, 0, 2 };
		CHECK(ComputePEChecksum(data.data(), data.size(), 100) == 3 + 3);
	}
	SECTION("carries are folded back in") {
		std::vector<uint8_t> data(6, 0xFF);	// three words of 0xFFFF
		CHECK(ComputePEChecksum(data.data(), data.size(), 100) == 0xFFFF + 6);
	}
}

TEST_CASE("A clean file has no anomalies", "[anomalies]") {
	SyntheticPE spec;
	spec.Is64 = GENERATE(true, false);
	spec.Dll = GENERATE(true, false);
	CAPTURE(spec.Is64, spec.Dll);
	auto anomalies = Analyze(spec.Build());
	INFO(Describe(anomalies));
	CHECK(anomalies.empty());
}

TEST_CASE("A correct header checksum is accepted, a wrong one is reported", "[anomalies]") {
	Editor e;
	auto optional = e.Spec.OptionalHeaderOffset();

	e.CheckSum() = 0;
	auto checksum = ComputePEChecksum(e.Bytes.data(), e.Bytes.size(), optional + 64);
	e.CheckSum() = checksum;
	CHECK(Analyze(e.Bytes).empty());

	e.CheckSum() = checksum ^ 0x1234;
	auto anomalies = Analyze(e.Bytes);
	CHECK(Has(anomalies, AnomalySeverity::Warning, L"Checksum", L"checksum"));
	REQUIRE(anomalies.size() == 1);
	CHECK(anomalies[0].FileOffset == (int64_t)optional + 64);
}

TEST_CASE("Sections", "[anomalies]") {
	Editor e;

	SECTION("writable and executable") {
		e.Section(2).Characteristics |= IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Section", L"Section .rdata is both writable and executable"));
	}
	SECTION("the name of a packer") {
		memcpy(e.Section(0).Name, "UPX0\0\0\0\0", 8);
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Section", L"used by UPX"));
	}
	SECTION("raw data beyond the end of the file") {
		e.Section(2).SizeOfRawData = 0x10000;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Error, L"Section", L"beyond the end of the file"));
	}
	SECTION("overlapping memory ranges") {
		e.Section(1).VirtualAddress = e.Section(0).VirtualAddress + 0x100;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Section", L"memory ranges"));
	}
	SECTION("sections out of order") {
		std::swap(e.Section(0).VirtualAddress, e.Section(1).VirtualAddress);
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Section", L"ascending"));
	}
	SECTION("an RVA that is not aligned") {
		e.Section(1).VirtualAddress = 0x2010;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Section", L"not a multiple of the section alignment"));
	}
	SECTION("executable code with no data in the file") {
		e.Section(0).SizeOfRawData = 0;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Section", L"created at run time"));
	}
	SECTION("high entropy code") {
		e.Randomize(SyntheticPE::TextOffset, SyntheticPE::SectionSize);
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Entropy", L"packed or encrypted"));
	}
	SECTION("high entropy data is only noted") {
		e.Randomize(SyntheticPE::DataOffset, SyntheticPE::SectionSize);
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Info, L"Entropy", L"compressed or encrypted"));
		CHECK_FALSE(Has(a, AnomalySeverity::Warning, L"Entropy", L""));
	}
}

TEST_CASE("Entry point", "[anomalies]") {
	Editor e;

	SECTION("outside every section") {
		e.EntryPoint() = 0x9000;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Error, L"Entry point", L"outside every section"));
	}
	SECTION("inside the headers") {
		e.EntryPoint() = 0x80;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Entry point", L"inside the headers"));
	}
	SECTION("in a section that is not executable") {
		e.EntryPoint() = SyntheticPE::DataRva;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Entry point", L"not executable"));
	}
	SECTION("in the last section") {
		e.Section(2).Characteristics |= IMAGE_SCN_MEM_EXECUTE;
		e.EntryPoint() = SyntheticPE::RdataRva;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Info, L"Entry point", L"last section"));
	}
	SECTION("an executable without an entry point") {
		e.FileHeader().Characteristics &= ~IMAGE_FILE_DLL;
		e.EntryPoint() = 0;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Entry point", L"no entry point"));
	}
	SECTION("a DLL does not need one") {
		e.EntryPoint() = 0;
		CHECK(Analyze(e.Bytes).empty());
	}
}

TEST_CASE("Headers", "[anomalies]") {
	Editor e;

	SECTION("SizeOfImage that does not match the sections") {
		e.SizeOfImage() = 0x9000;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Header", L"SizeOfImage"));
	}
	SECTION("SizeOfHeaders that is too small") {
		e.SizeOfHeaders() = 0x100;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Error, L"Header", L"SizeOfHeaders"));
	}
	SECTION("a link time stamp in the future") {
		e.FileHeader().TimeDateStamp = 0x7FFFFFFF;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Time stamp", L"future"));
	}
	SECTION("a link time stamp before there were PE files") {
		e.FileHeader().TimeDateStamp = 0x1000;
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Info, L"Time stamp", L"1995"));
	}
	SECTION("a zero time stamp is normal") {
		e.FileHeader().TimeDateStamp = 0;
		CHECK(Analyze(e.Bytes).empty());
	}
}

TEST_CASE("Directories", "[anomalies]") {
	Editor e;
	e.Directory(IMAGE_DIRECTORY_ENTRY_EXPORT) = { 0x5000, 0x1000 };
	auto a = Analyze(e.Bytes);
	INFO(Describe(a));
	CHECK(Has(a, AnomalySeverity::Warning, L"Directory", L"Export directory"));
}

TEST_CASE("Overlay", "[anomalies]") {
	Editor e;

	SECTION("a plain overlay is information") {
		e.Bytes.resize(e.Bytes.size() + 100, 0);
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Info, L"Overlay", L"100 bytes"));
		REQUIRE(a.size() == 1);
		CHECK(a[0].FileOffset == SyntheticPE::FileSize);
	}
	SECTION("a random looking overlay is suspicious") {
		auto start = e.Bytes.size();
		e.Bytes.resize(start + 8192);
		e.Randomize(start, 8192);
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK(Has(a, AnomalySeverity::Warning, L"Overlay", L"entropy"));
	}
	SECTION("the certificate table is not an overlay") {
		auto start = e.Bytes.size();
		e.Bytes.resize(start + 512);
		e.Randomize(start, 512);
		e.Directory(IMAGE_DIRECTORY_ENTRY_SECURITY) = { (DWORD)start, 512 };
		auto a = Analyze(e.Bytes);
		INFO(Describe(a));
		CHECK_FALSE(Has(a, AnomalySeverity::Info, L"Overlay", L""));
	}
}

TEST_CASE("Imports", "[anomalies]") {
	Editor e;
	// the name of the only import: "ExitProcess" -> "LoadLibraryA" (the hint/name entry has room for it)
	auto name = SyntheticPE::RdataOffset + (0x3102 - SyntheticPE::RdataRva);
	memcpy(e.Bytes.data() + name, "LoadLibraryA\0", 13);
	auto a = Analyze(e.Bytes);
	INFO(Describe(a));
	CHECK(Has(a, AnomalySeverity::Warning, L"Imports", L"LoadLibrary or GetProcAddress"));
}

TEST_CASE("Results are ordered by severity", "[anomalies]") {
	Editor e;
	e.Bytes.resize(e.Bytes.size() + 100, 0);			// info
	e.Section(2).Characteristics |= IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE;	// warning
	e.EntryPoint() = 0x9000;							// error
	auto a = Analyze(e.Bytes);
	INFO(Describe(a));
	REQUIRE(a.size() >= 3);
	for (size_t i = 1; i < a.size(); i++)
		CHECK(a[i - 1].Severity >= a[i].Severity);
	CHECK(a.front().Severity == AnomalySeverity::Error);
	CHECK(a.back().Severity == AnomalySeverity::Info);
}

TEST_CASE("Windows binaries have no errors and valid checksums", "[anomalies][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	auto name = GENERATE(as<std::wstring>{}, L"kernel32.dll", L"user32.dll", L"cmd.exe", L"ntdll.dll");
	auto path = std::wstring(dir) + L"\\" + name;
	PEFile pe;
	if (!pe.Open(path))
		SKIP("the system file could not be opened");

	auto anomalies = FindAnomalies(pe);
	INFO(Narrow(name) << "\n" << Describe(anomalies));
	for (auto& a : anomalies) {
		CHECK(a.Severity != AnomalySeverity::Error);
		CHECK(a.Category != L"Checksum");
		CHECK(a.Category != L"Rich header");
	}
}
