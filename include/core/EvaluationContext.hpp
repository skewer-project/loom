#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/Handle.hpp"
#include "gpu/ComputeTask.hpp"
#include "gpu/ResourceHandles.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {
class TransientImagePool;
class PipelineCache;
}  // namespace loom::gpu

namespace loom::core {

class RenderCache;

struct EvaluationContext {
    VkExtent2D requestedExtent;
    gpu::TransientImagePool* imagePool;
    gpu::PipelineCache* pipelineCache;
    RenderCache* renderCache;
    VmaAllocator allocator;
    VkCommandBuffer cmd;  // Shared command buffer for this frame

    // Generated compute tasks for this frame.
    std::vector<gpu::ComputeTask> tasks;

    // Deferred garbage collection for the end of the frame.
    std::vector<std::pair<VkBuffer, VmaAllocation>> pendingBufferFrees;
};

}  // namespace loom::core
