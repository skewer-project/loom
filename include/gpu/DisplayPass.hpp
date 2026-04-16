#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace loom::gpu {

class DisplayPass {
  public:
    DisplayPass(VkDevice device, VkFormat swapchainFormat, VkDescriptorSetLayout bindlessLayout);
    ~DisplayPass();

    void record(VkCommandBuffer cmd, VkImage hdrImage, VkImage dstImage, VkImageView dstImageView,
                VkDescriptorSet bindlessSet, uint32_t bindlessSlot, uint32_t width, uint32_t height,
                uint32_t toneMapMode);

  private:
    void createPipeline(VkFormat swapchainFormat, VkDescriptorSetLayout bindlessLayout);
    VkShaderModule createShaderModule(const std::string& filename);

    VkDevice m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;

    struct PushConstants {
        uint32_t inputSlotIndex;
        uint32_t width;
        uint32_t height;
        uint32_t toneMapMode;
    };
};

}  // namespace loom::gpu
