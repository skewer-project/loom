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

class Camera;
class RenderCache;

struct EvaluationContext {
    VkExtent2D requestedExtent;
    gpu::TransientImagePool* imagePool;
    gpu::PipelineCache* pipelineCache;
    RenderCache* renderCache;
    VmaAllocator allocator;
    VkCommandBuffer cmd;  // Shared command buffer for this frame

    // View parameters threaded from the UI into graph evaluation. Nullable
    // when graph eval doesn't require a camera (flat 2D viewports, headless
    // tests). Phase B onwards populates this whenever a `PointCloudPass`
    // or `PointCloudSplatPass` is in the active dispatch chain.
    const Camera* camera = nullptr;

    // Logical frame index. Drives time-varying parameters (deep-EXR sequence
    // playback in Phase C, animation curves in Phase E). Distinct from the
    // GPU frame value tracked by FrameLoop — that one is a Vulkan-side
    // counter; this is a scene-time index.
    uint64_t frame = 0;

    // Generated compute tasks for this frame.
    std::vector<gpu::ComputeTask> tasks;

    // Deferred garbage collection for the end of the frame.
    std::vector<std::pair<VkBuffer, VmaAllocation>> pendingBufferFrees;
};

}  // namespace loom::core
