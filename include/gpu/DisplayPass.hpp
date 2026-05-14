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
                uint32_t toneMapMode, uint32_t displayTransform, float exposure);

  private:
    void createPipeline(VkFormat swapchainFormat, VkDescriptorSetLayout bindlessLayout);
    VkShaderModule createShaderModule(const std::string& filename);

    VkDevice m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;

    // Layout must match shaders/DisplayPass.frag's push_constant block and
    // loom::color::DisplayParams exactly. Extending this requires updating all
    // three in lockstep.
    struct PushConstants {
        uint32_t inputSlotIndex;
        uint32_t width;
        uint32_t height;
        uint32_t toneMapMode;
        uint32_t displayTransform;
        float exposure;
        float _pad0;
        float _pad1;
    };
};

}  // namespace loom::gpu
