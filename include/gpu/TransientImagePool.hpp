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

    [[nodiscard]] ImageHandle acquire(ImageSpec spec);

    // Queue `handle` for release. The pool entry becomes available for
    // re-acquire only after the timeline-semaphore counter reaches
    // `releaseAtFrame`. Production callers pass
    // `frameLoop.currentFrameValue() + MAX_FRAMES_IN_FLIGHT`; tests can pass
    // `0` and pair with `flushPendingReleases()` for synchronous behaviour.
    void release(ImageHandle handle, uint64_t releaseAtFrame = 0);

    // Marks free every pending release whose `releaseAtFrame <= retiredValue`
    // and bumps its generation. Called once per frame by the engine after the
    // FrameLoop queries the timeline counter.
    void onFrameRetired(uint64_t retiredValue);

    // Synchronous drain: releases every pending entry regardless of tag. ONLY
    // safe after vkDeviceWaitIdle. Used by shutdown paths and unit tests;
    // production code should drive `onFrameRetired` instead.
    void flushPendingReleases();

    [[nodiscard]] VkImageLayout getLayout(ImageHandle handle) const;
    void setLayout(ImageHandle handle, VkImageLayout layout);

    [[nodiscard]] VkImageView getView(ImageHandle handle) const;
    [[nodiscard]] VkImage getImage(ImageHandle handle) const;

    // Source-extent accessor — needed by `DisplayPass` to aspect-fit the
    // viewer's HDR image onto the viewport regardless of native EXR
    // resolution. Returns the `ImageSpec::extent` the slot was acquired
    // with. Asserts on an invalid / freed handle.
    [[nodiscard]] VkExtent2D getExtent(ImageHandle handle) const;
    [[nodiscard]] uint32_t DEBUG_getBindlessSlot(ImageHandle handle) const {
        return handle.bindlessSlot;
    }

    // Diagnostic / test accessors. Not part of the production contract.
    // Returns the number of pool slots not currently in use. Leak tests assert
    // this returns to a baseline after a series of acquire/release cycles.
    [[nodiscard]] uint32_t DEBUG_getFreeSlotCount() const;

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

    struct PendingRelease {
        ImageHandle handle;
        uint64_t releaseAtFrame;
    };

    void retireEntry(ImageHandle handle);

    VkDevice m_device;
    VmaAllocator m_allocator;
    BindlessHeap& m_bindlessHeap;
    std::vector<ImageEntry> m_images;
    std::vector<PendingRelease> m_pendingReleases;
};

}  // namespace loom::gpu
