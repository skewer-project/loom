#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "gpu/BindlessHeap.hpp"
#include "gpu/ResourceHandles.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {

class TransientBufferPool {
  public:
    TransientBufferPool(VkDevice device, VmaAllocator allocator, BindlessHeap& bindlessHeap);
    ~TransientBufferPool();

    BufferHandle acquire(VkDeviceSize minSize);
    void release(BufferHandle handle);
    void flushPendingReleases();

    VkBuffer getBuffer(BufferHandle handle) const;
    VkDeviceSize getSize(BufferHandle handle) const;
    uint32_t DEBUG_getBindlessSlot(BufferHandle handle) const { return handle.bindlessSlot; }

  private:
    struct BufferEntry {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        uint32_t bindlessSlot = 0xFFFFFFFF;
        uint32_t generation = 0;
        bool isFree = true;
    };

    VkDevice m_device;  // Note: unused as of phase 4
    VmaAllocator m_allocator;
    BindlessHeap& m_bindlessHeap;
    std::vector<BufferEntry> m_buffers;
    std::vector<BufferHandle> m_pendingReleases;
};

}  // namespace loom::gpu
