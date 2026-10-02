#include "pch.h"
#include "AniFile.h"

namespace {
	constexpr uint32_t Tag(char a, char b, char c, char d) {
		return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
	}

	constexpr uint32_t RIFF = Tag('R', 'I', 'F', 'F'), ACON = Tag('A', 'C', 'O', 'N'), LIST = Tag('L', 'I', 'S', 'T'),
		anih = Tag('a', 'n', 'i', 'h'), rate = Tag('r', 'a', 't', 'e'), seq = Tag('s', 'e', 'q', ' '),
		fram = Tag('f', 'r', 'a', 'm'), icon = Tag('i', 'c', 'o', 'n'),
		INFO = Tag('I', 'N', 'F', 'O'), INAM = Tag('I', 'N', 'A', 'M'), IART = Tag('I', 'A', 'R', 'T');

	constexpr uint32_t AF_ICON = 1, AF_SEQUENCE = 2;

	uint32_t Read32(std::span<const std::byte> d, size_t pos) {
		uint32_t v = 0;
		for (size_t i = 0; i < 4; i++)
			v |= (uint32_t)std::to_integer<uint8_t>(d[pos + i]) << (8 * i);
		return v;
	}

	uint16_t Read16(std::span<const std::byte> d, size_t pos) {
		return (uint16_t)(std::to_integer<uint8_t>(d[pos]) | (std::to_integer<uint8_t>(d[pos + 1]) << 8));
	}

	std::wstring AnsiToWide(std::span<const std::byte> d) {
		size_t len = 0;
		while (len < d.size() && d[len] != std::byte{ 0 })
			len++;
		if (len == 0)
			return {};
		int n = ::MultiByteToWideChar(CP_ACP, 0, (PCSTR)d.data(), (int)len, nullptr, 0);
		std::wstring s(n, L'\0');
		::MultiByteToWideChar(CP_ACP, 0, (PCSTR)d.data(), (int)len, s.data(), n);
		return s;
	}

	// Walks the chunks in d[0, d.size()): calls f(id, data) for each one. A chunk's declared size is clamped to what exists.
	template<typename F>
	void ForEachChunk(std::span<const std::byte> d, F&& f) {
		size_t pos = 0;
		while (pos + 8 <= d.size()) {
			auto id = Read32(d, pos);
			uint64_t size = Read32(d, pos + 4);
			size_t start = pos + 8;
			size_t available = d.size() - start;
			auto length = (size_t)std::min<uint64_t>(size, available);
			f(id, d.subspan(start, length));
			pos = start + length + (length & 1);	// chunks are padded to an even size
		}
	}

	// The largest image of a .ico / .cur file.
	bool ParseIconFile(std::span<const std::byte> d, AniFrame& frame) {
		if (d.size() < 6 || Read16(d, 0) != 0)
			return false;
		auto type = Read16(d, 2);
		auto count = Read16(d, 4);
		if ((type != 1 && type != 2) || count == 0 || count > 256 || 6 + (size_t)count * 16 > d.size())
			return false;

		int best = -1;
		for (int i = 0; i < count; i++) {
			size_t e = 6 + (size_t)i * 16;
			uint32_t size = Read32(d, e + 8), offset = Read32(d, e + 12);
			if (size == 0 || (uint64_t)offset + size > d.size())
				continue;
			int w = std::to_integer<uint8_t>(d[e]) ? std::to_integer<uint8_t>(d[e]) : 256;
			int h = std::to_integer<uint8_t>(d[e + 1]) ? std::to_integer<uint8_t>(d[e + 1]) : 256;
			int bits = Read16(d, e + 6);
			if (best >= 0) {
				size_t b = 6 + (size_t)best * 16;
				int bw = std::to_integer<uint8_t>(d[b]) ? std::to_integer<uint8_t>(d[b]) : 256;
				int bh = std::to_integer<uint8_t>(d[b + 1]) ? std::to_integer<uint8_t>(d[b + 1]) : 256;
				if (w * h < bw * bh || (w * h == bw * bh && bits <= Read16(d, b + 6)))
					continue;
			}
			best = i;
			frame.Width = w;
			frame.Height = h;
			frame.BitCount = bits;
			frame.ImageOffset = offset;
			frame.ImageSize = size;
			frame.IsCursor = type == 2;
			// in a cursor the "planes" and "bit count" fields hold the hot spot
			frame.HotspotX = frame.IsCursor ? Read16(d, e + 4) : 0;
			frame.HotspotY = frame.IsCursor ? Read16(d, e + 6) : 0;
			if (frame.IsCursor)
				frame.BitCount = 0;
		}
		if (best < 0)
			return false;
		frame.Data.assign((const uint8_t*)d.data(), (const uint8_t*)d.data() + d.size());
		return true;
	}

	// the bit depth of a cursor image is in the image itself (BITMAPINFOHEADER.biBitCount)
	void FillBitCountFromImage(AniFrame& f) {
		if (f.BitCount == 0 && f.ImageSize >= 16 && f.ImageOffset + 16 <= f.Data.size()) {
			uint32_t headerSize = f.Data[f.ImageOffset] | (f.Data[f.ImageOffset + 1] << 8) | (f.Data[f.ImageOffset + 2] << 16);
			if (headerSize == 40)
				f.BitCount = f.Data[f.ImageOffset + 14] | (f.Data[f.ImageOffset + 15] << 8);
		}
	}
}

bool AniFile::Parse(std::span<const std::byte> data, AniFile& ani, std::wstring* error) {
	ani = {};
	auto fail = [&](PCWSTR message) {
		if (error)
			*error = message;
		return false;
	};

	if (data.size() < 12 || Read32(data, 0) != RIFF || Read32(data, 8) != ACON)
		return fail(L"Not an animated cursor (the RIFF/ACON header is missing)");
	// the declared RIFF size is only a hint: files in the wild get it wrong
	uint64_t declared = Read32(data, 4);
	auto body = data.subspan(12, (size_t)std::min<uint64_t>(declared >= 4 ? declared - 4 : 0, data.size() - 12));
	if (body.empty())
		body = data.subspan(12);

	bool haveHeader = false;
	uint32_t declaredFrames = 0, declaredSteps = 0;

	ForEachChunk(body, [&](uint32_t id, std::span<const std::byte> chunk) {
		if (id == anih && chunk.size() >= 36) {
			haveHeader = true;
			declaredFrames = Read32(chunk, 4);
			declaredSteps = Read32(chunk, 8);
			ani.Width = Read32(chunk, 12);
			ani.Height = Read32(chunk, 16);
			ani.BitCount = Read32(chunk, 20);
			ani.Planes = Read32(chunk, 24);
			ani.DisplayRate = std::max<uint32_t>(1, Read32(chunk, 28));
			ani.Flags = Read32(chunk, 32);
		}
		else if ((id == rate || id == seq) && !chunk.empty()) {
			auto& target = id == rate ? ani.Rates : ani.Sequence;
			auto n = std::min<size_t>(chunk.size() / 4, MaxSteps);
			target.clear();
			for (size_t i = 0; i < n; i++)
				target.push_back(Read32(chunk, i * 4));
		}
		else if (id == LIST && chunk.size() >= 4) {
			auto type = Read32(chunk, 0);
			auto list = chunk.subspan(4);
			if (type == fram) {
				ForEachChunk(list, [&](uint32_t subId, std::span<const std::byte> sub) {
					if (subId != icon || ani.Frames.size() >= MaxFrames)
						return;
					AniFrame frame;
					if (ParseIconFile(sub, frame)) {
						FillBitCountFromImage(frame);
						ani.Frames.push_back(std::move(frame));
					}
				});
			}
			else if (type == INFO) {
				ForEachChunk(list, [&](uint32_t subId, std::span<const std::byte> sub) {
					if (subId == INAM)
						ani.Title = AnsiToWide(sub);
					else if (subId == IART)
						ani.Author = AnsiToWide(sub);
				});
			}
		}
	});

	if (!haveHeader)
		return fail(L"The animation header (anih) is missing");
	if (!(ani.Flags & AF_ICON))
		return fail(L"The frames are raw bitmaps, which is not supported");
	if (ani.Frames.empty())
		return fail(L"The file contains no images");

	// without a sequence the steps are the frames in order
	if (!(ani.Flags & AF_SEQUENCE) || ani.Sequence.empty()) {
		ani.Sequence.clear();
		for (uint32_t i = 0; i < ani.Frames.size(); i++)
			ani.Sequence.push_back(i);
	}
	// the header says how many steps there are; the rate and sequence chunks may disagree
	if (declaredSteps && declaredSteps < ani.Sequence.size())
		ani.Sequence.resize(declaredSteps);
	for (auto& frame : ani.Sequence)
		if (frame >= ani.Frames.size())
			frame = 0;	// an invalid index shows the first image
	if (ani.Rates.size() != ani.Sequence.size())
		ani.Rates.clear();

	(void)declaredFrames;
	return true;
}

uint32_t AniFile::StepJiffies(size_t step) const {
	return std::max<uint32_t>(1, step < Rates.size() ? Rates[step] : DisplayRate);
}

uint32_t AniFile::StepMilliseconds(size_t step) const {
	return std::max<uint32_t>(1, (uint32_t)((uint64_t)StepJiffies(step) * 1000 / JiffiesPerSecond));
}

uint32_t AniFile::TotalMilliseconds() const {
	uint64_t total = 0;
	for (size_t i = 0; i < Sequence.size(); i++)
		total += StepMilliseconds(i);
	return (uint32_t)std::min<uint64_t>(total, 0xFFFFFFFF);
}
