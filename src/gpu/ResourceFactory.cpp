#include "gpu/ResourceFactory.hpp"

#include <stdexcept>

#include "core/Assert.hpp"
#include "gpu/Device.hpp"
#include "gpu/Instance.hpp"

namespace loom::gpu {

ResourceFactory::ResourceFactory(Instance& instance, Device& device) : m_device(device) {
    createCommandPool();
    createDescriptorPool();
    createVmaAllocator(instance);
    m_bindlessHeap = std::make_unique<BindlessHeap>(m_device.get());
}

ResourceFactory::~ResourceFactory() {
    VkDevice device = m_device.get();
    if (device == VK_NULL_HANDLE) return;

    // BindlessHeap owns descriptor sets allocated from its own internal pool
    // and must be torn down before the device.
    m_bindlessHeap.reset();

    if (m_vmaAllocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_vmaAllocator);
        m_vmaAllocator = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}

void ResourceFactory::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // Allows individual command buffers to be re-recorded each frame without
    // resetting the entire pool.
    poolInfo.queueFamilyIndex = m_device.getGraphicsQueueFamily();
    // Command buffers from this pool can only be submitted to queues from
    // this family.

    LOOM_VK_CHECK(vkCreateCommandPool(m_device.get(), &poolInfo, nullptr, &m_commandPool));
}

void ResourceFactory::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        // 1000 allows one descriptor per node-preview image in the
        // compositor. Expand if needed.
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // FREE_DESCRIPTOR_SET_BIT is mandatory to allow ImGui (and any future UI
    // system) to free individual descriptor sets without resetting the entire
    // pool.
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;

    LOOM_VK_CHECK(vkCreateDescriptorPool(m_device.get(), &poolInfo, nullptr, &m_descriptorPool));
}

void ResourceFactory::createVmaAllocator(Instance& instance) {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = m_device.getPhysical();
    allocatorInfo.device = m_device.get();
    allocatorInfo.instance = instance.get();

    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VMA allocator!");
    }
}

VkCommandBuffer ResourceFactory::beginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    LOOM_VK_CHECK(vkAllocateCommandBuffers(m_device.get(), &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    LOOM_VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    return commandBuffer;
}

void ResourceFactory::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
    LOOM_VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    LOOM_VK_CHECK(vkQueueSubmit(m_device.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
    LOOM_VK_CHECK(vkQueueWaitIdle(m_device.getGraphicsQueue()));
    // Hard sync — CPU blocks until GPU finishes. Acceptable for one-time setup;
    // never use in the frame loop.

    vkFreeCommandBuffers(m_device.get(), m_commandPool, 1, &commandBuffer);
}

}  // namespace loom::gpu
