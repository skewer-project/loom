#include "gpu/BindlessHeap.hpp"

#include <cassert>
#include <stdexcept>

namespace loom::gpu {

BindlessHeap::BindlessHeap(VkDevice device) : m_device(device) {
    // 1. Create Descriptor Pool
    VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_RESOURCES},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_RESOURCES}};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bindless descriptor pool!");
    }

    // 2. Create Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = MAX_RESOURCES;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = MAX_RESOURCES;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorBindingFlags bindingFlags[] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};

    VkDescriptorSetLayoutBindingFlagsCreateInfo layoutFlags{};
    layoutFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    layoutFlags.bindingCount = 2;
    layoutFlags.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    layoutInfo.pNext = &layoutFlags;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bindless descriptor set layout!");
    }

    // 3. Allocate Global Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_set) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate bindless descriptor set!");
    }

    // 4. Initialize State (free queues)
    for (uint32_t i = 0; i < MAX_RESOURCES; ++i) {
        m_freeImageSlots.push(i);
        m_freeBufferSlots.push(i);
    }
}

BindlessHeap::~BindlessHeap() {
    if (m_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    if (m_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
}

uint32_t BindlessHeap::registerImage(VkImageView view) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_freeImageSlots.empty()) return 0xFFFFFFFF;

    uint32_t slot = m_freeImageSlots.front();
    m_freeImageSlots.pop();

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = slot;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    return slot;
}

uint32_t BindlessHeap::registerBuffer(VkBuffer buffer, VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_freeBufferSlots.empty()) return 0xFFFFFFFF;

    uint32_t slot = m_freeBufferSlots.front();
    m_freeBufferSlots.pop();

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set;
    descriptorWrite.dstBinding = 1;
    descriptorWrite.dstArrayElement = slot;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    return slot;
}

void BindlessHeap::unregisterImage(uint32_t slot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_freeImageSlots.push(slot);
}

void BindlessHeap::unregisterBuffer(uint32_t slot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_freeBufferSlots.push(slot);
}

}  // namespace loom::gpu
