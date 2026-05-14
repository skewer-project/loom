#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "gpu/BindlessHeap.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {

class Instance;
class Device;

// Owns the per-engine resource allocators: command pool (for per-frame and
// one-shot command buffers), descriptor pool (consumed by ImGui), VMA
// allocator, and the BindlessHeap. Also exposes the one-time-command helpers
// used by transfer and layout-transition code outside the frame loop.
class ResourceFactory {
  public:
    ResourceFactory(Instance& instance, Device& device);
    ~ResourceFactory();

    ResourceFactory(const ResourceFactory&) = delete;
    ResourceFactory& operator=(const ResourceFactory&) = delete;

    [[nodiscard]] VkCommandPool getCommandPool() const { return m_commandPool; }
    [[nodiscard]] VkDescriptorPool getDescriptorPool() const { return m_descriptorPool; }
    [[nodiscard]] VmaAllocator getVmaAllocator() const { return m_vmaAllocator; }
    [[nodiscard]] BindlessHeap& getBindlessHeap() { return *m_bindlessHeap; }

    // One-shot command-buffer helpers. Allocated from the engine command pool
    // and freed on endSingleTimeCommands. Submits on the graphics queue and
    // waits with vkQueueWaitIdle — acceptable for one-off setup work; never
    // use in the frame loop.
    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer cmd) const;

  private:
    Device& m_device;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    std::unique_ptr<BindlessHeap> m_bindlessHeap;

    void createCommandPool();
    void createDescriptorPool();
    void createVmaAllocator(Instance& instance);
};

}  // namespace loom::gpu
