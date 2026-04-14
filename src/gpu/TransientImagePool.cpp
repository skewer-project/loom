#include "gpu/TransientImagePool.hpp"

#include <cassert>
#include <stdexcept>

namespace loom::gpu {

TransientImagePool::TransientImagePool(VkDevice device, VmaAllocator allocator,
                                       BindlessHeap& bindlessHeap)
    : m_device(device), m_allocator(allocator), m_bindlessHeap(bindlessHeap) {}

TransientImagePool::~TransientImagePool() {
    for (auto& entry : m_images) {
        if (entry.view != VK_NULL_HANDLE) vkDestroyImageView(m_device, entry.view, nullptr);
        if (entry.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_allocator, entry.image, entry.allocation);
        // Bindless slots are managed by the BindlessHeap, but unregistering here for completeness
        // if needed. Actually the prompt says "unregisterImage": push the slot back to the
        // respective free-list.
        if (entry.bindlessSlot != 0xFFFFFFFF) m_bindlessHeap.unregisterImage(entry.bindlessSlot);
    }
}

ImageHandle TransientImagePool::acquire(ImageSpec spec) {
    for (uint32_t i = 0; i < (uint32_t)m_images.size(); ++i) {
        auto& entry = m_images[i];
        if (entry.isFree && entry.spec == spec) {
            entry.isFree = false;
            return {i, entry.bindlessSlot, entry.generation};
        }
    }

    // Cache Miss
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = spec.extent.width;
    imageInfo.extent.height = spec.extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = spec.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = spec.usage | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(m_allocator, &imageInfo, &vmaAllocInfo, &image, &allocation, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = spec.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, image, allocation);
        throw std::runtime_error("failed to create image view!");
    }

    uint32_t slot = m_bindlessHeap.registerImage(view);
    if (slot == 0xFFFFFFFF) {
        vkDestroyImageView(m_device, view, nullptr);
        vmaDestroyImage(m_allocator, image, allocation);
        return {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};  // Invalid handle
    }

    uint32_t poolIndex = (uint32_t)m_images.size();
    m_images.push_back({image, view, allocation, spec, slot, VK_IMAGE_LAYOUT_UNDEFINED, 0, false});

    return {poolIndex, slot, 0};
}

void TransientImagePool::release(ImageHandle handle) {
    if (!handle.isValid()) return;
    assert(handle.poolIndex < m_images.size());
    auto& entry = m_images[handle.poolIndex];
    if (entry.generation != handle.generation) {
        throw std::runtime_error("stale handle release!");
    }
    m_pendingReleases.push_back(handle);
}

void TransientImagePool::flushPendingReleases() {
    for (auto handle : m_pendingReleases) {
        auto& entry = m_images[handle.poolIndex];
        entry.isFree = true;
        entry.generation++;
    }
    m_pendingReleases.clear();
}

VkImageLayout TransientImagePool::getLayout(ImageHandle handle) const {
    assert(handle.isValid() && handle.poolIndex < m_images.size());
    return m_images[handle.poolIndex].currentLayout;
}

void TransientImagePool::setLayout(ImageHandle handle, VkImageLayout layout) {
    assert(handle.isValid() && handle.poolIndex < m_images.size());
    m_images[handle.poolIndex].currentLayout = layout;
}

VkImageView TransientImagePool::getView(ImageHandle handle) const {
    assert(handle.isValid() && handle.poolIndex < m_images.size());
    return m_images[handle.poolIndex].view;
}

}  // namespace loom::gpu
