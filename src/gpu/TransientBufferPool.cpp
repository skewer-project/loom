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
        if (entry.bindlessSlot != 0xFFFFFFFF) m_bindlessHeap.unregisterBuffer(entry.bindlessSlot);
    }
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

void TransientBufferPool::release(BufferHandle handle) {
    if (!handle.isValid()) return;
    assert(handle.poolIndex < m_buffers.size());
    auto& entry = m_buffers[handle.poolIndex];
    if (entry.generation != handle.generation) {
        throw std::runtime_error("stale handle release!");
    }
    m_pendingReleases.push_back(handle);
}

void TransientBufferPool::flushPendingReleases() {
    for (auto handle : m_pendingReleases) {
        auto& entry = m_buffers[handle.poolIndex];
        entry.isFree = true;
        entry.generation++;
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
