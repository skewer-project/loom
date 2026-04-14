#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/Handle.hpp"
#include "gpu/ResourceHandles.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {
class TransientImagePool;
}

namespace loom::core {

struct EvaluationContext {
    VkExtent2D requestedExtent;
    gpu::TransientImagePool* imagePool;
    VmaAllocator allocator;
    VkCommandBuffer cmd;  // Shared command buffer for this frame

    // Persistent Cache keyed by upstream Output PinHandle: (generation << 32 | index).
    std::unordered_map<uint64_t, gpu::ImageHandle> outputCache;

    // Deferred garbage collection for the end of the frame.
    std::vector<gpu::ImageHandle> pendingImageReleases;
    std::vector<std::pair<VkBuffer, VmaAllocation>> pendingBufferFrees;
};

inline uint64_t pinKey(PinHandle h) { return ((uint64_t)h.generation << 32) | h.index; }

}  // namespace loom::core
