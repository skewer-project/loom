#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

namespace loom::gpu {

class BindlessHeap {
  public:
    explicit BindlessHeap(VkDevice device);
    ~BindlessHeap();

    BindlessHeap(const BindlessHeap&) = delete;
    BindlessHeap& operator=(const BindlessHeap&) = delete;

    [[nodiscard]] uint32_t registerImage(VkImageView view);
    [[nodiscard]] uint32_t registerBuffer(VkBuffer buffer, VkDeviceSize size);

    // Queue `slot` for return to the free list. The slot becomes free once
    // the timeline-semaphore counter reaches `releaseAtFrame` — callers pass
    // `frameLoop.currentFrameValue() + MAX_FRAMES_IN_FLIGHT` so the slot is
    // not reissued while shaders still reference its descriptor.
    void unregisterImage(uint32_t slot, uint64_t releaseAtFrame);
    void unregisterBuffer(uint32_t slot, uint64_t releaseAtFrame);

    // Returns slots whose `releaseAtFrame <= retiredValue` to the free list.
    // Called once per frame by the engine after the FrameLoop queries the
    // timeline counter.
    void onFrameRetired(uint64_t retiredValue);

    [[nodiscard]] VkDescriptorSetLayout getLayout() const { return m_layout; }
    [[nodiscard]] VkDescriptorSet getDescriptorSet() const { return m_set; }

    static constexpr uint32_t MAX_RESOURCES = 2048;

  private:
    struct PendingSlot {
        uint32_t slot;
        uint64_t releaseAtFrame;
    };

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;

    std::queue<uint32_t> m_freeImageSlots;
    std::queue<uint32_t> m_freeBufferSlots;
    std::vector<PendingSlot> m_pendingImageSlots;
    std::vector<PendingSlot> m_pendingBufferSlots;
    std::mutex m_mutex;
};

}  // namespace loom::gpu
