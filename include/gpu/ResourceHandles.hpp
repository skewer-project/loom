#pragma once

#include <cstdint>

namespace loom::gpu {

struct ImageHandle {
    uint32_t poolIndex = 0xFFFFFFFF;
    uint32_t bindlessSlot = 0xFFFFFFFF;
    uint32_t generation = 0xFFFFFFFF;

    bool operator==(const ImageHandle& other) const {
        return poolIndex == other.poolIndex && bindlessSlot == other.bindlessSlot &&
               generation == other.generation;
    }
    bool operator!=(const ImageHandle& other) const { return !(*this == other); }
    bool isValid() const { return poolIndex != 0xFFFFFFFF; }
};

struct BufferHandle {
    uint32_t poolIndex = 0xFFFFFFFF;
    uint32_t bindlessSlot = 0xFFFFFFFF;
    uint32_t generation = 0xFFFFFFFF;

    bool operator==(const BufferHandle& other) const {
        return poolIndex == other.poolIndex && bindlessSlot == other.bindlessSlot &&
               generation == other.generation;
    }
    bool operator!=(const BufferHandle& other) const { return !(*this == other); }
    bool isValid() const { return poolIndex != 0xFFFFFFFF; }
};

}  // namespace loom::gpu
