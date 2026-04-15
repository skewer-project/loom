#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "gpu/ComputeTask.hpp"
#include "gpu/ResourceHandles.hpp"

namespace loom::gpu {

class TransientImagePool;

class DispatchManager {
  public:
    void submit(VkCommandBuffer cmd, const std::vector<ComputeTask>& tasks,
                ImageHandle finalViewerImage, VkDescriptorSet bindlessSet,
                VkPipelineLayout pipelineLayout, TransientImagePool* imagePool);
};

}  // namespace loom::gpu
