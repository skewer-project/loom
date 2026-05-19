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

    // `width` / `height` are the destination viewport dimensions; `srcWidth`
    // / `srcHeight` are the source HDR image's native dimensions. The
    // fragment shader aspect-fits the source into the destination viewport
    // with a black letterbox so EXRs whose aspect doesn't match the
    // viewport render cleanly (no undefined-load garbage in the unused
    // strip — see docs/archive/feature-open-exr-2026.md "Phase B.8
    // follow-up #2").
    void record(VkCommandBuffer cmd, VkImage hdrImage, VkImage dstImage, VkImageView dstImageView,
                VkDescriptorSet bindlessSet, uint32_t bindlessSlot, uint32_t width, uint32_t height,
                uint32_t srcWidth, uint32_t srcHeight, uint32_t toneMapMode,
                uint32_t displayTransform, float exposure);

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
        uint32_t width;      // destination viewport width
        uint32_t height;     // destination viewport height
        uint32_t srcWidth;   // source HDR image width
        uint32_t srcHeight;  // source HDR image height
        uint32_t toneMapMode;
        uint32_t displayTransform;
        float exposure;
    };
};

}  // namespace loom::gpu
