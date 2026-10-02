#include "TestCommon.h"
#include <Md5.h>

TEST_CASE("MD5 matches the RFC 1321 test suite", "[md5]") {
	CHECK(Md5Hex(std::string("")) == "d41d8cd98f00b204e9800998ecf8427e");
	CHECK(Md5Hex(std::string("a")) == "0cc175b9c0f1b6a831c399e269772661");
	CHECK(Md5Hex(std::string("abc")) == "900150983cd24fb0d6963f7d28e17f72");
	CHECK(Md5Hex(std::string("message digest")) == "f96b697d7cb7938d525a2f31aaf161d0");
	CHECK(Md5Hex(std::string("abcdefghijklmnopqrstuvwxyz")) == "c3fcd3d76192e4007dfb496cca67e13b");
	CHECK(Md5Hex(std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")) == "d174ab98d277d9f5a5611c2c9f419d9f");
	CHECK(Md5Hex(std::string("12345678901234567890123456789012345678901234567890123456789012345678901234567890")) == "57edf4a22be3c955ac49da2e2107b67a");
}

TEST_CASE("MD5 handles the block boundaries", "[md5]") {
	// lengths around the padding thresholds (55/56 bytes: the length no longer fits; 64: a whole block)
	struct { size_t Length; char const* Hash; } const cases[] = {
		{ 55, "ef1772b6dff9a122358552954ad0df65" }, { 56, "3b0c8ac703f828b04c6c197006d17218" },
		{ 57, "652b906d60af96844ebd21b674f35e93" }, { 63, "b06521f39153d618550606be297466d5" },
		{ 64, "014842d480b571495a4a0363793f7367" }, { 65, "c743a45e0d2e6a95cb859adae0248435" },
		{ 119, "8a7bd0732ed6a28ce75f6dabc90e1613" }, { 120, "5f61c0ccad4cac44c75ff505e1f1e537" },
		{ 121, "f6acfca2d47c87f2b14ca038234d3614" }, { 128, "e510683b3f5ffe4093d021808bc6ff70" },
	};
	for (auto& c : cases) {
		CAPTURE(c.Length);
		CHECK(Md5Hex(std::string(c.Length, 'a')) == c.Hash);
	}
}

TEST_CASE("MD5 of a large input", "[md5]") {
	CHECK(Md5Hex(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21");
}

TEST_CASE("MD5 of binary data", "[md5]") {
	std::vector<uint8_t> zeros(16, 0);
	CHECK(Md5Hex(zeros.data(), zeros.size()) == "4ae71336e44bf9bf79d2752e234818a5");
}
