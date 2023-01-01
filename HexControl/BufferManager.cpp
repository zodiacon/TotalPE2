#include "pch.h"
#include "BufferManager.h"
#include "MemoryBuffer.h"

std::unique_ptr<IBufferManager> IBufferManager::CreateFromData(const uint8_t* data, uint32_t size) {
    return std::make_unique<MemoryBuffer>(data, size);
}

