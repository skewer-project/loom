#pragma once

#include <vulkan/vulkan.h>

#include <glm/mat4x4.hpp>

#include "gpu/ResourceHandles.hpp"
#include "vk_mem_alloc.h"

namespace loom::core {
class Camera;
}

namespace loom::gpu {

class PipelineCache;
class TransientBufferPool;

// Renders a deep payload as a per-sample point cloud, depth-tested, into an
// arbitrary RGBA color attachment + matching depth attachment.
//
// Sibling to `DisplayPass`. Owns:
//   - the per-pass `VkPipelineLayout` (combines the bindless set + a
//     dedicated `VkDescriptorSetLayout` for the per-frame camera UBO).
//   - the camera UBO (one host-visible buffer, mapped, updated each frame).
//   - the per-pass descriptor pool + descriptor set for the camera UBO.
//   - a depth image / view sized to the most recent `record(...)` call.
//     Reallocated lazily on resize.
//
// The pipeline itself is shared with `PipelineCache::getOrCreateGraphics`
// so the cached `VkPipelineCache` blob warms across passes.
//
// `record` does NOT transition the input deep payload — caller is
// responsible (the deep payload is already in `GENERAL` post-upload).
class PointCloudPass {
  public:
    PointCloudPass(VkDevice device, VmaAllocator allocator, VkDescriptorSetLayout bindlessLayout,
                   PipelineCache& pipelineCache, VkFormat colorFormat,
                   VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);
    ~PointCloudPass();

    PointCloudPass(const PointCloudPass&) = delete;
    PointCloudPass& operator=(const PointCloudPass&) = delete;

    // Record one frame's draw. The deep payload's `sampleToPixel` and
    // `samples` buffers must be in `SHADER_READ` state. `dstImage` must be
    // in `UNDEFINED` or `SHADER_READ_ONLY_OPTIMAL` on entry; this method
    // transitions to `COLOR_ATTACHMENT_OPTIMAL`, renders, then transitions
    // to `SHADER_READ_ONLY_OPTIMAL` (matching `DisplayPass::record`).
    //
    // `pointSize` is in pixels; v1 hardcodes 2.0 at the call site.
    // `zScale` shapes the synthetic world-Z derived from sample depth —
    // see `PointCloud.vert` for the v1 mapping. Defaults pick a sensible
    // range for the standard fixture; production callers will expose this
    // as a node param in a future PR.
    void record(VkCommandBuffer cmd, const ResourceRef::DeepRef& deep, VkImage dstImage,
                VkImageView dstImageView, VkDescriptorSet bindlessSet, uint32_t width,
                uint32_t height, const core::Camera& camera, float zScale = 1.0f,
                float pointSize = 2.0f);

  private:
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkDescriptorSetLayout m_bindlessLayout;
    PipelineCache& m_pipelineCache;
    VkFormat m_colorFormat;
    VkFormat m_depthFormat;

    VkDescriptorSetLayout m_cameraSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_cameraSet = VK_NULL_HANDLE;

    // Camera UBO: persistently host-visible mapped, one mat4.
    VkBuffer m_cameraBuffer = VK_NULL_HANDLE;
    VmaAllocation m_cameraAlloc = VK_NULL_HANDLE;
    void* m_cameraMapped = nullptr;

    // Depth attachment. Allocated lazily, recreated on size change.
    uint32_t m_depthW = 0;
    uint32_t m_depthH = 0;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthAlloc = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    void createPipelineLayout();
    void createDescriptorResources();
    void createCameraBuffer();
    void ensureDepth(uint32_t width, uint32_t height);
    void destroyDepth();
};

}  // namespace loom::gpu
