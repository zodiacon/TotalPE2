#include "TestCommon.h"
#include <AniFile.h>
#include "ByteWriter.h"

namespace {
	// a .ico / .cur file with one 32 bit image
	std::vector<uint8_t> IconFile(int size, bool cursor = false, int hotX = 0, int hotY = 0, uint8_t shade = 0x80) {
		ByteWriter dib;
		dib.U32(40).U32(size).U32(size * 2).U16(1).U16(32).U32(0).U32(0).U32(0).U32(0).U32(0).U32(0);
		for (int i = 0; i < size * size; i++)
			dib.U8(shade).U8(0).U8(0xFF).U8(0xFF);			// BGRA
		for (int i = 0; i < size * (((size + 31) / 32) * 4); i++)
			dib.U8(0);										// the AND mask

		ByteWriter w;
		w.U16(0).U16(cursor ? 2 : 1).U16(1);
		w.U8(size >= 256 ? 0 : (uint8_t)size).U8(size >= 256 ? 0 : (uint8_t)size).U8(0).U8(0);
		w.U16(cursor ? (uint16_t)hotX : 1).U16(cursor ? (uint16_t)hotY : 32);
		w.U32((uint32_t)dib.Bytes.size()).U32(22);
		w.Bytes.insert(w.Bytes.end(), dib.Bytes.begin(), dib.Bytes.end());
		return w.Bytes;
	}

	struct AniSpec {
		std::vector<std::vector<uint8_t>> Frames;
		std::vector<uint32_t> Sequence, Rates;
		uint32_t Jiffies{ 6 };
		uint32_t Flags{ 1 };		// AF_ICON
		std::string Title, Author;
		bool WriteSequence{ true };
		bool WriteHeader{ true };
		uint32_t DeclaredFrames{ 0xFFFFFFFF };
		uint32_t DeclaredSteps{ 0xFFFFFFFF };

		static void Chunk(ByteWriter& w, char const* id, std::vector<uint8_t> const& data) {
			for (int i = 0; i < 4; i++)
				w.U8((uint8_t)id[i]);
			w.U32((uint32_t)data.size());
			w.Bytes.insert(w.Bytes.end(), data.begin(), data.end());
			if (data.size() & 1)
				w.U8(0);	// chunks are padded to an even size
		}
		static std::vector<uint8_t> ZString(std::string const& s) {
			std::vector<uint8_t> v(s.begin(), s.end());
			v.push_back(0);
			return v;
		}

		ByteWriter Build() const {
			auto steps = Sequence.empty() ? (uint32_t)Frames.size() : (uint32_t)Sequence.size();
			ByteWriter body;
			for (char c : std::string("ACON"))
				body.U8(c);

			if (!Title.empty() || !Author.empty()) {
				ByteWriter info;
				for (char c : std::string("INFO"))
					info.U8(c);
				if (!Title.empty())
					Chunk(info, "INAM", ZString(Title));
				if (!Author.empty())
					Chunk(info, "IART", ZString(Author));
				Chunk(body, "LIST", info.Bytes);
			}
			if (WriteHeader) {
				ByteWriter h;
				h.U32(36).U32(DeclaredFrames == 0xFFFFFFFF ? (uint32_t)Frames.size() : DeclaredFrames);
				h.U32(DeclaredSteps == 0xFFFFFFFF ? steps : DeclaredSteps).U32(32).U32(32).U32(32).U32(1).U32(Jiffies).U32(Flags);
				Chunk(body, "anih", h.Bytes);
			}
			if (!Rates.empty()) {
				ByteWriter r;
				for (auto v : Rates)
					r.U32(v);
				Chunk(body, "rate", r.Bytes);
			}
			if (!Sequence.empty() && WriteSequence) {
				ByteWriter s;
				for (auto v : Sequence)
					s.U32(v);
				Chunk(body, "seq ", s.Bytes);
			}
			ByteWriter frames;
			for (char c : std::string("fram"))
				frames.U8(c);
			for (auto& f : Frames)
				Chunk(frames, "icon", f);
			Chunk(body, "LIST", frames.Bytes);

			ByteWriter file;
			for (char c : std::string("RIFF"))
				file.U8(c);
			file.U32((uint32_t)body.Bytes.size());
			file.Bytes.insert(file.Bytes.end(), body.Bytes.begin(), body.Bytes.end());
			return file;
		}
	};

	AniSpec ThreeFrames() {
		AniSpec s;
		s.Frames = { IconFile(32, true, 3, 4, 0x10), IconFile(32, true, 3, 4, 0x80), IconFile(32, true, 3, 4, 0xF0) };
		return s;
	}
}

TEST_CASE("An animated cursor is parsed", "[ani]") {
	auto spec = ThreeFrames();
	spec.Sequence = { 0, 1, 2, 1 };
	spec.Rates = { 3, 6, 3, 12 };
	spec.Flags = 3;	// AF_ICON | AF_SEQUENCE
	spec.Title = "Spinner";
	spec.Author = "Test Author";
	auto file = spec.Build();

	AniFile ani;
	std::wstring error;
	REQUIRE(AniFile::Parse(file.Span(), ani, &error));

	SECTION("header") {
		CHECK(ani.Flags == 3);
		CHECK(ani.Width == 32);
		CHECK(ani.Height == 32);
		CHECK(ani.BitCount == 32);
		CHECK(ani.DisplayRate == 6);
		CHECK(ani.Title == L"Spinner");
		CHECK(ani.Author == L"Test Author");
	}
	SECTION("frames") {
		REQUIRE(ani.Frames.size() == 3);
		for (auto& f : ani.Frames) {
			CHECK(f.Width == 32);
			CHECK(f.Height == 32);
			CHECK(f.IsCursor);
			CHECK(f.HotspotX == 3);
			CHECK(f.HotspotY == 4);
			CHECK(f.BitCount == 32);	// read from the image: a cursor has no bit depth in its directory
			CHECK(f.ImageOffset == 22);
			CHECK(f.ImageOffset + f.ImageSize == f.Data.size());
		}
		CHECK(ani.Frames[0].Data == IconFile(32, true, 3, 4, 0x10));
	}
	SECTION("steps") {
		REQUIRE(ani.StepCount() == 4);
		CHECK(ani.Sequence == std::vector<uint32_t>{ 0, 1, 2, 1 });
		CHECK(&ani.StepFrame(3) == &ani.Frames[1]);
		CHECK(ani.StepJiffies(0) == 3);
		CHECK(ani.StepJiffies(3) == 12);
		CHECK(ani.StepMilliseconds(0) == 50);	// 3/60 s
		CHECK(ani.StepMilliseconds(1) == 100);
		CHECK(ani.StepMilliseconds(3) == 200);
		CHECK(ani.TotalMilliseconds() == 400);
	}
}

TEST_CASE("Without a sequence and rates the frames play in order at the display rate", "[ani]") {
	auto spec = ThreeFrames();
	spec.Jiffies = 6;
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.Sequence == std::vector<uint32_t>{ 0, 1, 2 });
	CHECK(ani.Rates.empty());
	for (size_t i = 0; i < 3; i++)
		CHECK(ani.StepMilliseconds(i) == 100);
	CHECK(ani.Title.empty());
}

TEST_CASE("A sequence is ignored unless the flag says it is there", "[ani]") {
	auto spec = ThreeFrames();
	spec.Sequence = { 2, 2, 2 };
	spec.Flags = 1;		// AF_ICON only
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.Sequence == std::vector<uint32_t>{ 0, 1, 2 });
}

TEST_CASE("Durations are never zero", "[ani]") {
	auto spec = ThreeFrames();
	spec.Jiffies = 0;
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.StepJiffies(0) == 1);
	CHECK(ani.StepMilliseconds(0) == 16);

	spec = ThreeFrames();
	spec.Rates = { 0, 0, 0 };
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.StepMilliseconds(1) == 16);
}

TEST_CASE("Icons are animated too", "[ani]") {
	AniSpec spec;
	spec.Frames = { IconFile(16, false), IconFile(16, false), IconFile(16, false), IconFile(16, false) };
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	REQUIRE(ani.Frames.size() == 4);
	CHECK_FALSE(ani.Frames[0].IsCursor);
	CHECK(ani.Frames[0].BitCount == 32);
	CHECK(ani.Frames[0].HotspotX == 0);
}

TEST_CASE("The images can be turned into icons", "[ani]") {
	auto spec = ThreeFrames();
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	for (auto& f : ani.Frames) {
		auto icon = ::CreateIconFromResourceEx(f.Data.data() + f.ImageOffset, f.ImageSize, TRUE, 0x00030000, f.Width, f.Height, LR_DEFAULTCOLOR);
		CHECK(icon != nullptr);
		if (icon)
			::DestroyIcon(icon);
	}
}

TEST_CASE("The largest image of a frame is used", "[ani]") {
	// a frame with two images, 16 and 48 pixels
	auto small = IconFile(16), large = IconFile(48);
	ByteWriter w;
	w.U16(0).U16(1).U16(2);
	auto entry = [&](std::vector<uint8_t> const& f, uint32_t offset, int size) {
		w.U8((uint8_t)size).U8((uint8_t)size).U8(0).U8(0).U16(1).U16(32).U32((uint32_t)f.size() - 22).U32(offset);
	};
	uint32_t o1 = 6 + 32, o2 = o1 + (uint32_t)small.size() - 22;
	entry(small, o1, 16);
	entry(large, o2, 48);
	w.Bytes.insert(w.Bytes.end(), small.begin() + 22, small.end());
	w.Bytes.insert(w.Bytes.end(), large.begin() + 22, large.end());

	AniSpec spec;
	spec.Frames = { w.Bytes };
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	REQUIRE(ani.Frames.size() == 1);
	CHECK(ani.Frames[0].Width == 48);
	CHECK(ani.Frames[0].Height == 48);
	CHECK(ani.Frames[0].ImageOffset == o2);
}

TEST_CASE("Chunks of odd size are padded", "[ani]") {
	auto spec = ThreeFrames();
	spec.Title = "AB";		// "AB\0" is 3 bytes: the chunk has a pad byte
	spec.Author = "Z";		// 2 bytes: no pad
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.Title == L"AB");
	CHECK(ani.Author == L"Z");
	CHECK(ani.Frames.size() == 3);
}

TEST_CASE("A wrong RIFF size is tolerated", "[ani]") {
	auto file = ThreeFrames().Build();

	SECTION("too large") {
		file.Bytes[4] = 0xFF;
		file.Bytes[5] = 0xFF;
		file.Bytes[6] = 0xFF;
		file.Bytes[7] = 0x7F;
	}
	SECTION("zero") {
		file.Bytes[4] = file.Bytes[5] = file.Bytes[6] = file.Bytes[7] = 0;
	}
	AniFile ani;
	REQUIRE(AniFile::Parse(file.Span(), ani));
	CHECK(ani.Frames.size() == 3);
}

TEST_CASE("Invalid sequence indexes show the first frame", "[ani]") {
	auto spec = ThreeFrames();
	spec.Flags = 3;
	spec.Sequence = { 0, 99, 2 };
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.Sequence == std::vector<uint32_t>{ 0, 0, 2 });
}

TEST_CASE("The header may declare fewer steps than the sequence holds", "[ani]") {
	auto spec = ThreeFrames();
	spec.Flags = 3;
	spec.Sequence = { 0, 1, 2, 1, 0 };
	spec.DeclaredSteps = 3;
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.StepCount() == 3);
}

TEST_CASE("Rates that do not match the steps are dropped", "[ani]") {
	auto spec = ThreeFrames();
	spec.Rates = { 1, 2 };	// two rates, three steps
	AniFile ani;
	REQUIRE(AniFile::Parse(spec.Build().Span(), ani));
	CHECK(ani.Rates.empty());
	CHECK(ani.StepMilliseconds(0) == 100);
}

TEST_CASE("Files that are not usable are rejected with a reason", "[ani]") {
	AniFile ani;
	std::wstring error;

	SECTION("empty and tiny") {
		CHECK_FALSE(AniFile::Parse({}, ani, &error));
		CHECK_FALSE(error.empty());
		ByteWriter w;
		w.U32(0x46464952);
		CHECK_FALSE(AniFile::Parse(w.Span(), ani, &error));
	}
	SECTION("not RIFF") {
		auto file = ThreeFrames().Build();
		file.Bytes[0] = 'X';
		CHECK_FALSE(AniFile::Parse(file.Span(), ani, &error));
		CHECK_FALSE(error.empty());
	}
	SECTION("RIFF but not ACON") {
		auto file = ThreeFrames().Build();
		file.Bytes[8] = 'W';
		CHECK_FALSE(AniFile::Parse(file.Span(), ani, &error));
	}
	SECTION("no animation header") {
		auto spec = ThreeFrames();
		spec.WriteHeader = false;
		CHECK_FALSE(AniFile::Parse(spec.Build().Span(), ani, &error));
		CHECK_THAT(Narrow(error), Catch::Matchers::ContainsSubstring("anih"));
	}
	SECTION("no images") {
		AniSpec spec;
		CHECK_FALSE(AniFile::Parse(spec.Build().Span(), ani, &error));
		CHECK_THAT(Narrow(error), Catch::Matchers::ContainsSubstring("no images"));
	}
	SECTION("frames that are raw bitmaps") {
		auto spec = ThreeFrames();
		spec.Flags = 0;
		CHECK_FALSE(AniFile::Parse(spec.Build().Span(), ani, &error));
		CHECK_THAT(Narrow(error), Catch::Matchers::ContainsSubstring("raw bitmaps"));
	}
	SECTION("frames that are not icon files") {
		AniSpec spec;
		spec.Frames = { { 1, 2, 3, 4, 5, 6, 7, 8 } };
		CHECK_FALSE(AniFile::Parse(spec.Build().Span(), ani, &error));
	}
	SECTION("an icon whose image lies outside the data") {
		auto frame = IconFile(16);
		frame[18] = 0xFF;	// the offset of the image: 0xFFxx
		frame[19] = 0x7F;
		AniSpec spec;
		spec.Frames = { frame };
		CHECK_FALSE(AniFile::Parse(spec.Build().Span(), ani, &error));
	}
}

TEST_CASE("Truncated and damaged files do not crash the parser", "[ani]") {
	auto spec = ThreeFrames();
	spec.Flags = 3;
	spec.Sequence = { 0, 1, 2, 1 };
	spec.Rates = { 3, 6, 3, 12 };
	spec.Title = "Spinner";
	auto file = spec.Build();

	SECTION("every truncation") {
		for (size_t length = 0; length < file.Bytes.size(); length++) {
			AniFile ani;
			if (AniFile::Parse(file.Span().first(length), ani)) {
				// whatever was parsed is consistent
				for (auto i = 0u; i < ani.StepCount(); i++) {
					auto& f = ani.StepFrame(i);
					CHECK(f.ImageOffset + (uint64_t)f.ImageSize <= f.Data.size());
					CHECK(ani.StepMilliseconds(i) >= 1);
				}
			}
		}
	}
	SECTION("every byte damaged") {
		for (size_t i = 0; i < file.Bytes.size(); i++)
			for (uint8_t value : { (uint8_t)0x00, (uint8_t)0xFF, (uint8_t)0x80 }) {
				auto copy = file;
				copy.Bytes[i] = value;
				AniFile ani;
				if (AniFile::Parse(copy.Span(), ani)) {
					for (auto j = 0u; j < ani.StepCount(); j++) {
						auto& f = ani.StepFrame(j);
						REQUIRE(f.ImageOffset + (uint64_t)f.ImageSize <= f.Data.size());
					}
				}
			}
	}
	SECTION("huge counts are bounded") {
		auto huge = spec;
		huge.DeclaredFrames = 0xFFFFFFFF - 1;
		huge.DeclaredSteps = 0xFFFFFFFF - 1;
		AniFile ani;
		if (AniFile::Parse(huge.Build().Span(), ani))
			CHECK(ani.StepCount() <= AniFile::MaxSteps);
	}
}

TEST_CASE("The animated cursors in user32.dll are parsed", "[ani][system]") {
	WCHAR dir[MAX_PATH];
	::GetSystemDirectoryW(dir, MAX_PATH);
	PEFile pe;
	if (!pe.Open(std::wstring(dir) + L"\\user32.dll"))
		SKIP("user32.dll could not be opened");

	int found = 0;
	for (auto& r : pe.GetFlatResources()) {
		if (r.TypeID != (WORD)(ULONG_PTR)RT_ANICURSOR && r.TypeID != (WORD)(ULONG_PTR)RT_ANIICON)
			continue;
		found++;
		AniFile ani;
		std::wstring error;
		INFO("resource " << Narrow(r.NameStr) << " (" << r.Data.size() << " bytes): " << Narrow(error));
		REQUIRE(AniFile::Parse(r.Data, ani, &error));
		CHECK(ani.Frames.size() >= 1);
		CHECK(ani.StepCount() >= 1);
		CHECK(ani.TotalMilliseconds() >= 1);
		for (auto& f : ani.Frames) {
			CHECK(f.Width > 0);
			CHECK(f.Height > 0);
			auto icon = ::CreateIconFromResourceEx(f.Data.data() + f.ImageOffset, f.ImageSize, TRUE, 0x00030000, f.Width, f.Height, LR_DEFAULTCOLOR);
			CHECK(icon != nullptr);
			if (icon)
				::DestroyIcon(icon);
		}
	}
	if (found == 0)
		SKIP("user32.dll has no animated cursors on this version of Windows");
}
