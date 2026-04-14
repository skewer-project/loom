#pragma once

#include <vulkan/vulkan.h>

#include <mutex>
#include <queue>

namespace loom::gpu {

class BindlessHeap {
  public:
    explicit BindlessHeap(VkDevice device);
    ~BindlessHeap();

    // Disable copying
    BindlessHeap(const BindlessHeap&) = delete;
    BindlessHeap& operator=(const BindlessHeap&) = delete;

    uint32_t registerImage(VkImageView view);
    uint32_t registerBuffer(VkBuffer buffer, VkDeviceSize size);
    void unregisterImage(uint32_t slot);
    void unregisterBuffer(uint32_t slot);

    VkDescriptorSetLayout getLayout() const { return m_layout; }
    VkDescriptorSet getDescriptorSet() const { return m_set; }

    static constexpr uint32_t MAX_RESOURCES = 2048;

  private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;

    std::queue<uint32_t> m_freeImageSlots;
    std::queue<uint32_t> m_freeBufferSlots;
    std::mutex m_mutex;
};

}  // namespace loom::gpu
