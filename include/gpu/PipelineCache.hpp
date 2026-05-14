#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>

namespace loom::gpu {

// SPIR-V → VkPipeline cache backed by a real VkPipelineCache, persisted to
// disk between runs. On construction, attempts to load
// `<userDataDir>/pipeline_cache.bin`; on destruction, writes back whatever the
// driver produced. A missing or unreadable file is non-fatal: the engine
// boots with an empty cache and re-warms from shader sources.
class PipelineCache {
  public:
    PipelineCache(VkDevice device, VkPipelineLayout pipelineLayout);
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    // Loads SPIR-V from disk, creates a VkShaderModule, creates the compute
    // pipeline against the global pipeline layout (and the persistent
    // VkPipelineCache), and caches the resulting VkPipeline by filename.
    [[nodiscard]] VkPipeline getOrCreate(const std::string& spvPath);

    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }

  private:
    VkDevice m_device;
    VkPipelineLayout m_pipelineLayout;
    VkPipelineCache m_vkCache = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> m_pipelines;

    void createVkPipelineCache();
    void saveVkPipelineCache() const;
};

}  // namespace loom::gpu
