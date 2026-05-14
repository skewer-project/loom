#include "gpu/TransientBufferPool.hpp"

#include <cassert>
#include <stdexcept>

namespace loom::gpu {

TransientBufferPool::TransientBufferPool(VkDevice device, VmaAllocator allocator,
                                         BindlessHeap& bindlessHeap)
    : m_device(device), m_allocator(allocator), m_bindlessHeap(bindlessHeap) {}

TransientBufferPool::~TransientBufferPool() {
    for (auto& entry : m_buffers) {
        if (entry.buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_allocator, entry.buffer, entry.allocation);
    }
    // No bindless unregister: see TransientImagePool::~TransientImagePool.
}

BufferHandle TransientBufferPool::acquire(VkDeviceSize minSize) {
    for (uint32_t i = 0; i < (uint32_t)m_buffers.size(); ++i) {
        auto& entry = m_buffers[i];
        if (entry.isFree && entry.size >= minSize && entry.size <= minSize * 2) {
            entry.isFree = false;
            return {i, entry.bindlessSlot, entry.generation};
        }
    }

    // Cache Miss
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = minSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkBuffer buffer;
    VmaAllocation allocation;
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &vmaAllocInfo, &buffer, &allocation, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    uint32_t slot = m_bindlessHeap.registerBuffer(buffer, minSize);
    if (slot == 0xFFFFFFFF) {
        vmaDestroyBuffer(m_allocator, buffer, allocation);
        return {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};  // Invalid handle
    }

    uint32_t poolIndex = (uint32_t)m_buffers.size();
    m_buffers.push_back({buffer, allocation, minSize, slot, 0, false});

    return {poolIndex, slot, 0};
}

void TransientBufferPool::release(BufferHandle handle, uint64_t releaseAtFrame) {
    if (!handle.isValid()) return;
    assert(handle.poolIndex < m_buffers.size());
    auto& entry = m_buffers[handle.poolIndex];
    if (entry.generation != handle.generation) {
        throw std::runtime_error("stale handle release!");
    }
    m_pendingReleases.push_back({handle, releaseAtFrame});
}

void TransientBufferPool::retireEntry(BufferHandle handle) {
    auto& entry = m_buffers[handle.poolIndex];
    entry.isFree = true;
    entry.generation++;
}

void TransientBufferPool::onFrameRetired(uint64_t retiredValue) {
    auto keep = m_pendingReleases.begin();
    for (auto it = m_pendingReleases.begin(); it != m_pendingReleases.end(); ++it) {
        if (it->releaseAtFrame <= retiredValue) {
            retireEntry(it->handle);
        } else {
            if (keep != it) *keep = *it;
            ++keep;
        }
    }
    m_pendingReleases.erase(keep, m_pendingReleases.end());
}

void TransientBufferPool::flushPendingReleases() {
    for (const auto& pending : m_pendingReleases) {
        retireEntry(pending.handle);
    }
    m_pendingReleases.clear();
}

VkBuffer TransientBufferPool::getBuffer(BufferHandle handle) const {
    assert(handle.isValid() && handle.poolIndex < m_buffers.size());
    return m_buffers[handle.poolIndex].buffer;
}

VkDeviceSize TransientBufferPool::getSize(BufferHandle handle) const {
    assert(handle.isValid() && handle.poolIndex < m_buffers.size());
    return m_buffers[handle.poolIndex].size;
}

}  // namespace loom::gpu
