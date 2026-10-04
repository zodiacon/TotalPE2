#include "pch.h"
#include "Md5.h"

namespace {
	constexpr uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
	};
	constexpr int S[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
	};

	uint32_t Rol(uint32_t x, int c) {
		return (x << c) | (x >> (32 - c));
	}

	void Block(uint32_t state[4], const uint8_t* p) {
		uint32_t m[16];
		for (int i = 0; i < 16; i++)
			m[i] = p[i * 4] | (p[i * 4 + 1] << 8) | (p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);

		uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
		for (int i = 0; i < 64; i++) {
			uint32_t f;
			int g;
			if (i < 16) { f = (b & c) | (~b & d); g = i; }
			else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
			else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
			else { f = c ^ (b | ~d); g = (7 * i) % 16; }
			uint32_t next = d;
			d = c;
			c = b;
			b = b + Rol(a + f + K[i] + m[g], S[i]);
			a = next;
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
	}
}

std::string Md5Hex(const void* data, size_t size) {
	uint32_t state[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
	auto bytes = static_cast<const uint8_t*>(data);

	size_t full = size / 64;
	for (size_t i = 0; i < full; i++)
		Block(state, bytes + i * 64);

	// the last block(s): the remaining bytes, a 0x80 marker, zero padding and the length in bits
	uint8_t tail[128]{};
	size_t rest = size - full * 64;
	if (rest)
		memcpy(tail, bytes + full * 64, rest);
	tail[rest] = 0x80;
	size_t tailSize = rest + 1 + 8 <= 64 ? 64 : 128;
	uint64_t bits = (uint64_t)size * 8;
	for (int i = 0; i < 8; i++)
		tail[tailSize - 8 + i] = (uint8_t)(bits >> (8 * i));
	Block(state, tail);
	if (tailSize == 128)
		Block(state, tail + 64);

	static const char hex[] = "0123456789abcdef";
	std::string result;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			auto byte = (uint8_t)(state[i] >> (8 * j));
			result += hex[byte >> 4];
			result += hex[byte & 15];
		}
	return result;
}

std::string Md5Hex(std::string const& text) {
	return Md5Hex(text.data(), text.size());
}
