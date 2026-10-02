#include "TestCommon.h"
#include <LibArchive.h>
#include <cstring>

namespace {
	using Bytes = std::vector<std::byte>;

	void Put(Bytes& b, const void* p, size_t n) {
		auto c = static_cast<const std::byte*>(p);
		b.insert(b.end(), c, c + n);
	}
	template<typename T> void Put(Bytes& b, T v) { Put(b, &v, sizeof(v)); }
	void PutText(Bytes& b, std::string const& s) { Put(b, s.data(), s.size()); }
	void PutBE(Bytes& b, uint32_t v) { v = _byteswap_ulong(v); Put(b, v); }

	std::string Field(std::string text, size_t width) {
		text.resize(width, ' ');
		return text;
	}

	struct Member {
		std::string RawName;
		Bytes Data;
	};

	// appends a member (header, data, padding) and returns the offset of its header
	size_t AddMember(Bytes& ar, std::string const& rawName, Bytes const& data) {
		if (ar.empty())
			PutText(ar, "!<arch>\n");
		auto offset = ar.size();
		PutText(ar, Field(rawName, 16) + Field("1700000000", 12) + Field("0", 6) + Field("0", 6) + Field("100644", 8) +
			Field(std::to_string(data.size()), 10) + "`\n");
		Put(ar, data.data(), data.size());
		if (ar.size() & 1)
			PutText(ar, "\n");
		return offset;
	}

	Bytes ObjectData(uint16_t machine, uint16_t sections, uint32_t symbols, size_t size = 100) {
		Bytes b;
		Put<uint16_t>(b, machine);
		Put<uint16_t>(b, sections);
		Put<uint32_t>(b, 0x12345678);
		Put<uint32_t>(b, 40);		// symbol table
		Put<uint32_t>(b, symbols);
		Put<uint16_t>(b, 0);
		Put<uint16_t>(b, 0);
		b.resize(size);
		return b;
	}

	Bytes ImportData(uint16_t machine, std::string const& symbol, std::string const& dll, uint16_t hint, uint16_t type, uint16_t nameType) {
		Bytes b;
		Put<uint16_t>(b, 0);
		Put<uint16_t>(b, 0xFFFF);
		Put<uint16_t>(b, 0);
		Put<uint16_t>(b, machine);
		Put<uint32_t>(b, 77);
		Put<uint32_t>(b, (uint32_t)(symbol.size() + dll.size() + 2));
		Put<uint16_t>(b, hint);
		Put<uint16_t>(b, (uint16_t)(type | (nameType << 2)));
		PutText(b, symbol);
		Put<uint8_t>(b, 0);
		PutText(b, dll);
		Put<uint8_t>(b, 0);
		return b;
	}

	// An archive in the layout of the Microsoft linker: the two linker members, the long names, then the objects
	struct TestLib {
		Bytes Data;
		std::vector<std::pair<std::string, size_t>> Offsets;		// name, header offset
	};

	TestLib BuildLib(std::vector<std::pair<std::string, Bytes>> const& objects, std::vector<std::pair<std::string, int>> const& symbols) {
		// the layout must be known before the linker members can be written: reserve their sizes first
		std::string longNames;
		std::vector<std::string> raw;
		for (auto& [name, data] : objects) {
			if (name.size() > 15) {
				raw.push_back("/" + std::to_string(longNames.size()));
				longNames += name + "/\n";
			}
			else {
				raw.push_back(name + "/");
			}
		}

		auto firstSize = 4 + symbols.size() * 4;
		for (auto& [s, m] : symbols)
			firstSize += s.size() + 1;
		auto secondSize = 4 + objects.size() * 4 + 4 + symbols.size() * 2;
		for (auto& [s, m] : symbols)
			secondSize += s.size() + 1;

		auto pad = [](size_t n) { return n + (n & 1); };
		size_t pos = 8 + 60 + pad(firstSize) + 60 + pad(secondSize);
		if (!longNames.empty())
			pos += 60 + pad(longNames.size());
		std::vector<size_t> offsets;
		for (auto& [name, data] : objects) {
			offsets.push_back(pos);
			pos += 60 + pad(data.size());
		}

		TestLib lib;
		Bytes first;
		PutBE(first, (uint32_t)symbols.size());
		for (auto& [s, m] : symbols)
			PutBE(first, (uint32_t)offsets[m]);
		for (auto& [s, m] : symbols)
			PutText(first, s), Put<uint8_t>(first, 0);
		AddMember(lib.Data, "/", first);

		Bytes second;
		Put<uint32_t>(second, (uint32_t)objects.size());
		for (auto o : offsets)
			Put<uint32_t>(second, (uint32_t)o);
		Put<uint32_t>(second, (uint32_t)symbols.size());
		for (auto& [s, m] : symbols)
			Put<uint16_t>(second, (uint16_t)(m + 1));
		for (auto& [s, m] : symbols)
			PutText(second, s), Put<uint8_t>(second, 0);
		AddMember(lib.Data, "/", second);

		if (!longNames.empty()) {
			Bytes ln;
			PutText(ln, longNames);
			AddMember(lib.Data, "//", ln);
		}
		for (size_t i = 0; i < objects.size(); i++) {
			auto at = AddMember(lib.Data, raw[i], objects[i].second);
			REQUIRE(at == offsets[i]);
			lib.Offsets.push_back({ objects[i].first, at });
		}
		return lib;
	}

	std::span<const std::byte> View(Bytes const& b) { return b; }
}

TEST_CASE("The signature of an archive is recognized", "[lib]") {
	Bytes ar;
	PutText(ar, "!<arch>\n");
	CHECK(LibArchive::IsArchive(ar));
	Bytes pe;
	PutText(pe, "MZ\x90\x00 not an archive");
	CHECK_FALSE(LibArchive::IsArchive(pe));
	CHECK_FALSE(LibArchive::IsArchive(View({})));
}

TEST_CASE("An empty archive is valid", "[lib]") {
	Bytes ar;
	PutText(ar, "!<arch>\n");
	LibArchive lib;
	REQUIRE(lib.Open(ar));
	CHECK(lib.Members().empty());
	CHECK(lib.Symbols().empty());
}

TEST_CASE("The members and the symbols of a library", "[lib]") {
	auto lib = BuildLib({
		{ "a.obj", ObjectData(0x8664, 3, 12) },
		{ "b.obj", ObjectData(0x8664, 5, 20) },
		{ "a_member_with_a_very_long_name.obj", ObjectData(0x14c, 1, 2, 51) },
	}, {
		{ "alpha", 0 }, { "beta", 1 }, { "gamma", 1 }, { "delta", 2 },
	});

	LibArchive ar;
	REQUIRE(ar.Open(lib.Data));
	auto& m = ar.Members();
	REQUIRE(m.size() == 6);
	CHECK(m[0].Kind == ArchiveMemberKind::LinkerMember);
	CHECK(m[1].Kind == ArchiveMemberKind::LinkerMember);
	CHECK(m[2].Kind == ArchiveMemberKind::LongNames);
	CHECK(m[3].Name == "a.obj");
	CHECK(m[4].Name == "b.obj");
	CHECK(m[5].Name == "a_member_with_a_very_long_name.obj");

	CHECK(m[3].Kind == ArchiveMemberKind::Object);
	CHECK(m[3].Machine == 0x8664);
	CHECK(m[3].Sections == 3);
	CHECK(m[3].Symbols == 12);
	CHECK(m[3].Size == 100);
	CHECK(m[3].Date == 1700000000);
	CHECK(m[3].Mode == 0100644);
	CHECK(m[5].Machine == 0x14c);
	CHECK(m[5].Size == 51);
	CHECK(ar.MemberData(5).size() == 51);		// the data is at the right place, and without the padding
	CHECK(ar.MemberData(99).empty());

	auto& s = ar.Symbols();
	REQUIRE(s.size() == 4);
	CHECK(s[0].Name == "alpha");
	CHECK(s[0].Member == 3);
	CHECK(s[1].Name == "beta");
	CHECK(s[1].Member == 4);
	CHECK(s[2].Member == 4);
	CHECK(s[3].Name == "delta");
	CHECK(s[3].Member == 5);
}

TEST_CASE("The symbols of the first linker member are used when there is no second one", "[lib]") {
	Bytes ar;
	// objects first, so the offsets are known
	Bytes first;
	size_t firstSize = 4 + 4 + 6;
	size_t obj = 8 + 60 + firstSize;
	PutBE(first, 1u);
	PutBE(first, (uint32_t)obj);
	PutText(first, "_main");
	Put<uint8_t>(first, 0);
	AddMember(ar, "/", first);
	AddMember(ar, "m.obj/", ObjectData(0x14c, 1, 1));

	LibArchive lib;
	REQUIRE(lib.Open(ar));
	REQUIRE(lib.Members().size() == 2);
	REQUIRE(lib.Symbols().size() == 1);
	CHECK(lib.Symbols()[0].Name == "_main");
	CHECK(lib.Symbols()[0].Member == 1);
}

TEST_CASE("The records of an import library", "[lib]") {
	auto lib = BuildLib({
		{ "d000.dll", ImportData(0x8664, "CreateFileW", "KERNEL32.dll", 123, 0, 1) },
		{ "d001.dll", ImportData(0x14c, "_gData", "MY.dll", 7, 1, 3) },
		{ "d002.dll", ImportData(0x8664, "Ord", "MY.dll", 45, 2, 0) },
	}, { { "CreateFileW", 0 }, { "__imp_CreateFileW", 0 } });

	LibArchive ar;
	REQUIRE(ar.Open(lib.Data));
	auto& imps = ar.Imports();
	REQUIRE(imps.size() == 3);
	CHECK(ar.Members()[2].Kind == ArchiveMemberKind::ImportRecord);
	CHECK(imps[0].Symbol == "CreateFileW");
	CHECK(imps[0].Dll == "KERNEL32.dll");
	CHECK(imps[0].OrdinalOrHint == 123);
	CHECK(imps[0].Type == ImportType::Code);
	CHECK(imps[0].NameType == ImportNameType::Name);
	CHECK(imps[0].Machine == 0x8664);
	CHECK(imps[0].TimeDateStamp == 77);
	CHECK(imps[0].Member == 2);
	CHECK(imps[1].Type == ImportType::Data);
	CHECK(imps[1].NameType == ImportNameType::Undecorate);
	CHECK(imps[2].Type == ImportType::Const);
	CHECK(imps[2].NameType == ImportNameType::Ordinal);
}

TEST_CASE("A bigobj member", "[lib]") {
	Bytes big;
	Put<uint16_t>(big, 0);
	Put<uint16_t>(big, 0xFFFF);
	Put<uint16_t>(big, 2);
	Put<uint16_t>(big, 0x8664);
	Put<uint32_t>(big, 0);
	big.resize(big.size() + 16);	// class id
	Put<uint32_t>(big, 0);
	Put<uint32_t>(big, 0);
	Put<uint32_t>(big, 0);
	Put<uint32_t>(big, 0);
	Put<uint32_t>(big, 70000);	// sections
	Put<uint32_t>(big, 100);
	Put<uint32_t>(big, 123456);	// symbols
	big.resize(120);
	Bytes data;
	AddMember(data, "big.obj/", big);

	LibArchive ar;
	REQUIRE(ar.Open(data));
	REQUIRE(ar.Members().size() == 1);
	CHECK(ar.Members()[0].Kind == ArchiveMemberKind::Object);
	CHECK(ar.Members()[0].BigObj);
	CHECK(ar.Members()[0].Sections == 70000);
	CHECK(ar.Members()[0].Symbols == 123456);
}

TEST_CASE("Members that are not objects", "[lib]") {
	Bytes text;
	PutText(text, "just some text, nothing more to see here.");
	Bytes data;
	AddMember(data, "notes.txt/", text);
	AddMember(data, "tiny/", Bytes(3));
	LibArchive ar;
	REQUIRE(ar.Open(data));
	REQUIRE(ar.Members().size() == 2);
	CHECK(ar.Members()[0].Kind == ArchiveMemberKind::Other);
	CHECK(ar.Members()[0].Name == "notes.txt");
	CHECK(ar.Members()[1].Kind == ArchiveMemberKind::Other);
}

TEST_CASE("BSD style long names", "[lib]") {
	Bytes name;
	PutText(name, "a_name_that_is_rather_long.o");
	auto obj = ObjectData(0x8664, 1, 0, 60);
	Bytes member = name;
	Put(member, obj.data(), obj.size());
	Bytes data;
	AddMember(data, "#1/" + std::to_string(name.size()), member);
	LibArchive ar;
	REQUIRE(ar.Open(data));
	REQUIRE(ar.Members().size() == 1);
	CHECK(ar.Members()[0].Name == "a_name_that_is_rather_long.o");
	CHECK(ar.Members()[0].Size == 60);
	CHECK(ar.Members()[0].Kind == ArchiveMemberKind::Object);
}

TEST_CASE("A damaged archive does not crash the reader", "[lib]") {
	auto good = BuildLib({
		{ "a.obj", ObjectData(0x8664, 3, 12) },
		{ "b_very_long_object_name.obj", ImportData(0x8664, "F", "D.dll", 1, 0, 1) },
	}, { { "alpha", 0 }, { "beta", 1 } });

	SECTION("truncated at every length") {
		for (size_t n = 0; n < good.Data.size(); n++) {
			Bytes cut(good.Data.begin(), good.Data.begin() + n);
			LibArchive ar;
			if (ar.Open(cut)) {
				for (size_t i = 0; i < ar.Members().size(); i++)
					(void)ar.MemberData(i);
			}
		}
	}

	SECTION("every byte changed") {
		for (size_t i = 0; i < good.Data.size(); i++) {
			for (auto v : { std::byte{ 0 }, std::byte{ 0xFF }, std::byte{ '9' } }) {
				auto bad = good.Data;
				bad[i] = v;
				LibArchive ar;
				if (ar.Open(bad)) {
					for (size_t m = 0; m < ar.Members().size(); m++)
						(void)ar.MemberData(m);
					for (auto& s : ar.Symbols())
						CHECK(s.Member < (int)ar.Members().size());
				}
			}
		}
	}

	SECTION("a symbol count that is larger than the member") {
		auto bad = good.Data;
		auto first = 8 + 60;
		uint32_t huge = _byteswap_ulong(0x7FFFFFFF);
		memcpy(&bad[first], &huge, 4);
		LibArchive ar;
		REQUIRE(ar.Open(bad));
		CHECK((ar.Symbols().empty() || ar.Symbols().size() < 3));
	}
}

TEST_CASE("A library in a file", "[lib]") {
	auto lib = BuildLib({ { "a.obj", ObjectData(0x8664, 3, 12) } }, { { "alpha", 0 } });
	std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t*>(lib.Data.data()), reinterpret_cast<const uint8_t*>(lib.Data.data()) + lib.Data.size());
	TempFile temp(bytes, L".lib");
	CHECK(LibArchive::IsArchiveFile(temp.Path()));
	LibArchive ar;
	REQUIRE(ar.Open(temp.Path()));
	CHECK(ar.Members().size() == 3);
	CHECK(ar.Symbols().size() == 1);
	CHECK(ar.Path() == temp.Path());
	ar.Close();
	CHECK_FALSE(ar);
	CHECK_FALSE(LibArchive::IsArchiveFile(L"C:\\this\\does\\not\\exist.lib"));
}

namespace {
	std::wstring FindSdkLib(PCWSTR relative) {
		WCHAR root[MAX_PATH];
		auto n = ::ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%\\Windows Kits\\10\\Lib\\", root, MAX_PATH);
		if (n == 0)
			return L"";
		std::wstring dir = root;
		WIN32_FIND_DATAW fd;
		auto find = ::FindFirstFileW((dir + L"10.*").c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE)
			return L"";
		std::wstring result;
		do {
			auto candidate = dir + fd.cFileName + L"\\" + relative;
			if (::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				result = candidate;
		} while (::FindNextFileW(find, &fd));
		::FindClose(find);
		return result;
	}
}

TEST_CASE("An import library of the SDK", "[lib][system]") {
	auto path = FindSdkLib(L"um\\x64\\kernel32.lib");
	if (path.empty())
		SKIP("no SDK");
	REQUIRE(LibArchive::IsArchiveFile(path));
	LibArchive ar;
	REQUIRE(ar.Open(path));
	CHECK(ar.Members().size() > 1000);
	CHECK(ar.Imports().size() > 1000);
	CHECK(ar.Symbols().size() >= ar.Imports().size());

	for (auto& s : ar.Symbols())
		REQUIRE((s.Member >= 0 && s.Member < (int)ar.Members().size()));
	// every symbol of a short import record names a member that is one
	bool found = false;
	for (auto& imp : ar.Imports()) {
		REQUIRE(ar.Members()[imp.Member].Kind == ArchiveMemberKind::ImportRecord);
		REQUIRE_FALSE(imp.Symbol.empty());
		REQUIRE_FALSE(imp.Dll.empty());
		found |= imp.Symbol == "CreateFileW" && _stricmp(imp.Dll.c_str(), "kernel32.dll") == 0 && imp.Type == ImportType::Code;
	}
	CHECK(found);
}

TEST_CASE("A static library of the C runtime", "[lib][system]") {
	std::wstring found;
	WIN32_FIND_DATAW fd;
	std::wstring dir = L"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC\\";
	auto find = ::FindFirstFileW((dir + L"*").c_str(), &fd);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			auto candidate = dir + fd.cFileName + L"\\lib\\x64\\libcmt.lib";
			if (fd.cFileName[0] != L'.' && ::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				found = candidate;
		} while (::FindNextFileW(find, &fd));
		::FindClose(find);
	}
	if (found.empty())
		SKIP("no C runtime library");

	LibArchive ar;
	REQUIRE(ar.Open(found));
	size_t objects = 0, others = 0;
	for (auto& m : ar.Members()) {
		if (m.Kind == ArchiveMemberKind::Object) {
			objects++;
			CHECK_FALSE(m.Name.empty());
			CHECK((m.Machine == 0x8664 || m.Machine == 0));
		}
		else if (m.Kind == ArchiveMemberKind::Other)
			others++;
	}
	CHECK(objects > 500);
	CHECK(others == 0);
	CHECK(ar.Symbols().size() > objects);
	bool startupFound = false;
	for (auto& s : ar.Symbols()) {
		REQUIRE((s.Member >= 0 && s.Member < (int)ar.Members().size()));
		startupFound |= s.Name == "wmainCRTStartup" && ar.Members()[s.Member].Kind == ArchiveMemberKind::Object;
	}
	CHECK(startupFound);
}
