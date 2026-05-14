#pragma once

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "vk_mem_alloc.h"

// Test-side utilities. Headers-only by design — most callers also want one
// or two test-only `#include`s and we keep linker dependencies tight.

namespace loom::testhelpers {

// Read back a Vulkan image of format VK_FORMAT_R32G32B32A32_SFLOAT into a
// host-visible buffer and return the contents as a vector<float>. One-shot
// command + vkQueueWaitIdle — acceptable in tests; never in the render loop.
//
// The image must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL or
// VK_IMAGE_LAYOUT_GENERAL on entry. We transition it to TRANSFER_SRC_OPTIMAL,
// copy it out, and leave it there — the caller is responsible for restoring
// the layout if they want to keep using the image.
inline std::vector<float> readbackRGBA32F(VkDevice device, VmaAllocator allocator,
                                          VkCommandPool pool, VkQueue queue, VkImage image,
                                          VkExtent2D extent) {
    const VkDeviceSize numPixels =
        static_cast<VkDeviceSize>(extent.width) * static_cast<VkDeviceSize>(extent.height);
    const VkDeviceSize byteSize = numPixels * 4 * sizeof(float);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &stagingBuffer, &stagingAlloc,
                        &allocResult) != VK_SUCCESS) {
        return {};
    }

    VkCommandBufferAllocateInfo cbAllocInfo{};
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool = pool;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cbAllocInfo, &cmd) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return {};
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Barrier: GENERAL → TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier2 toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    toTransfer.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.image = image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(cmd, &dep);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1,
                           &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    std::vector<float> out(numPixels * 4);
    std::memcpy(out.data(), allocResult.pMappedData, byteSize);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
    return out;
}

// Convenience: returns true if every pair (a, b) is within `epsilon`.
inline bool floatsClose(const std::vector<float>& a, const std::vector<float>& b, float epsilon) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const float diff = a[i] - b[i];
        const float abs = diff < 0 ? -diff : diff;
        if (abs > epsilon) return false;
    }
    return true;
}

}  // namespace loom::testhelpers
