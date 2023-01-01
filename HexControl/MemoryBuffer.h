#pragma once

#include "BufferManager.h"

class MemoryBuffer : public IBufferManager {
public:
	explicit MemoryBuffer(uint32_t initialSize = 0);
	MemoryBuffer(const uint8_t* data, uint32_t size, bool copy = true);

	void Clear();

	// Inherited via IBufferManager
	uint32_t GetData(int64_t offset, uint8_t* buffer, uint32_t count) override;
	bool Insert(int64_t offset, const uint8_t* data, uint32_t count) override;
	bool Delete(int64_t offset, size_t count) override;
	bool SetData(int64_t offset, const uint8_t* data, uint32_t count) override;
	bool SetData(int64_t offset, uint8_t value, uint32_t count) override;
	int64_t GetSize() const override;
	uint8_t* GetRawData(int64_t offset) override;
	bool IsReadOnly() const override;

private:
	std::vector<uint8_t> m_buffer;
	uint8_t* m_ptr;
	int64_t m_size;
};

