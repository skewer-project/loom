#include "gpu/StagingArena.hpp"

#include <stdexcept>

namespace loom::gpu {

namespace {

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

StagingArena::StagingArena(VmaAllocator allocator, VkDeviceSize capacity)
    : m_allocator(allocator), m_capacity(capacity) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = capacity;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    // HOST_ACCESS_SEQUENTIAL_WRITE — we never read back through the mapped
    // pointer, only write. This lets VMA pick a write-combined memory type
    // on platforms that expose one.
    alloc.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo result{};
    if (vmaCreateBuffer(m_allocator, &info, &alloc, &m_buffer, &m_allocation, &result) !=
        VK_SUCCESS) {
        throw std::runtime_error("StagingArena: vmaCreateBuffer failed");
    }
    m_mapped = result.pMappedData;
    if (!m_mapped) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        throw std::runtime_error("StagingArena: persistently-mapped pointer is null");
    }
}

StagingArena::~StagingArena() {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
}

StagingArena::Allocation StagingArena::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    if (size == 0) return {};
    VkDeviceSize alignedHead = alignUp(m_head, alignment);
    if (alignedHead + size > m_capacity) return {};  // OOM — caller handles

    Allocation a{};
    a.buffer = m_buffer;
    a.offset = alignedHead;
    a.size = size;
    a.mapped = static_cast<uint8_t*>(m_mapped) + alignedHead;
    m_head = alignedHead + size;
    return a;
}

void StagingArena::reset() { m_head = 0; }

}  // namespace loom::gpu
