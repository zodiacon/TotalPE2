#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <GhidraDecompiler.h>

// Memory for the decompiler to read: blocks of bytes at virtual addresses (code, import slots, data). A read that starts in
// a block is answered, with zeros after its end (as a section is padded: instructions are decoded from a buffer that
// can go past the last one); one that starts elsewhere is not.
class TestImage {
public:
	// The bytes at an address. Code is put together with the helpers below, so that the operands that are relative (to
	// the next instruction) are worked out from the addresses.
	class Block {
	public:
		explicit Block(uint64_t address) : m_Address(address) {}

		Block& Bytes(std::initializer_list<uint8_t> bytes) {
			m_Bytes.insert(m_Bytes.end(), bytes);
			return *this;
		}
		Block& U32(uint32_t value) {
			for (int i = 0; i < 4; i++)
				m_Bytes.push_back(uint8_t(value >> (8 * i)));
			return *this;
		}
		Block& U64(uint64_t value) {
			for (int i = 0; i < 8; i++)
				m_Bytes.push_back(uint8_t(value >> (8 * i)));
			return *this;
		}
		Block& Text(std::string const& text) {
			m_Bytes.insert(m_Bytes.end(), text.begin(), text.end());
			m_Bytes.push_back(0);
			return *this;
		}
		// a 32 bit displacement from the end of the instruction to 'target' (x64: call, jmp, and operands relative to rip);
		// 'after' is how many bytes of the instruction come after the displacement (an immediate operand)
		Block& Rel32(uint64_t target, int after = 0) {
			auto next = Here() + 4 + after;
			return U32(uint32_t(int64_t(target - next)));
		}
		// x64: call/jmp qword ptr [rip+slot]
		Block& CallThrough(uint64_t slot) { return Bytes({ 0xFF, 0x15 }).Rel32(slot); }
		Block& JumpThrough(uint64_t slot) { return Bytes({ 0xFF, 0x25 }).Rel32(slot); }
		// x64: call target
		Block& Call(uint64_t target) { return Bytes({ 0xE8 }).Rel32(target); }

		uint64_t Address() const { return m_Address; }
		uint64_t Here() const { return m_Address + m_Bytes.size(); }
		std::vector<uint8_t> const& Data() const { return m_Bytes; }

	private:
		uint64_t m_Address;
		std::vector<uint8_t> m_Bytes;
	};

	Block& At(uint64_t address) {
		return m_Blocks.try_emplace(address, address).first->second;
	}

	bool Read(uint64_t address, uint8_t* buffer, size_t size) const {
		auto it = m_Blocks.upper_bound(address);
		if (it == m_Blocks.begin())
			return false;
		--it;
		auto const& data = it->second.Data();
		auto offset = address - it->first;
		if (offset >= data.size())
			return false;
		auto count = std::min(size, size_t(data.size() - offset));
		memcpy(buffer, data.data() + offset, count);
		memset(buffer + count, 0, size - count);
		return true;
	}

	GhidraDecompiler::ReadMemory Reader() const {
		return [this](uint64_t address, uint8_t* buffer, size_t size) { return Read(address, buffer, size); };
	}

private:
	std::map<uint64_t, Block> m_Blocks;
};
