#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>

namespace loom::gpu {

class PipelineCache {
  public:
    PipelineCache(VkDevice device, VkPipelineLayout pipelineLayout);
    ~PipelineCache();

    // Loads SPIR-V from disk, creates a VkShaderModule, creates a
    // VkComputePipelineInfo against the global pipeline layout, and caches
    // the resulting VkPipeline keyed by filename.
    VkPipeline getOrCreate(const std::string& spvPath);

    // The global pipeline layout must be passed in — it is created once
    // by the application using the BindlessHeap's VkDescriptorSetLayout.
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }

  private:
    VkDevice m_device;
    VkPipelineLayout m_pipelineLayout;
    std::unordered_map<std::string, VkPipeline> m_pipelines;
};

}  // namespace loom::gpu
