#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "gpu/BindlessHeap.hpp"
#include "gpu/ResourceHandles.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {

struct ImageSpec {
    VkFormat format;
    VkExtent2D extent;
    VkImageUsageFlags usage;

    bool operator==(const ImageSpec& other) const {
        return format == other.format && extent.width == other.extent.width &&
               extent.height == other.extent.height && usage == other.usage;
    }
};

class TransientImagePool {
  public:
    TransientImagePool(VkDevice device, VmaAllocator allocator, BindlessHeap& bindlessHeap);
    ~TransientImagePool();

    ImageHandle acquire(ImageSpec spec);
    void release(ImageHandle handle);
    void flushPendingReleases();

    VkImageLayout getLayout(ImageHandle handle) const;
    void setLayout(ImageHandle handle, VkImageLayout layout);

    VkImageView getView(ImageHandle handle) const;
    VkImage getImage(ImageHandle handle) const;
    uint32_t DEBUG_getBindlessSlot(ImageHandle handle) const { return handle.bindlessSlot; }

    // Diagnostic / test accessors. Not part of the production contract.
    // Returns the number of pool slots not currently in use. Leak tests assert
    // this returns to a baseline after a series of acquire/release cycles.
    uint32_t DEBUG_getFreeSlotCount() const;

  private:
    struct ImageEntry {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        ImageSpec spec{};
        uint32_t bindlessSlot = 0xFFFFFFFF;
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t generation = 0;
        bool isFree = true;
    };

    VkDevice m_device;
    VmaAllocator m_allocator;
    BindlessHeap& m_bindlessHeap;
    std::vector<ImageEntry> m_images;
    std::vector<ImageHandle> m_pendingReleases;
};

}  // namespace loom::gpu
