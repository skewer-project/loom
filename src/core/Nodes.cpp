#include "core/Nodes.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::core {

static void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                            VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

gpu::ImageHandle Node::pullInput(EvaluationContext& ctx, uint32_t inputIndex) {
    if (!graph || inputIndex >= inputs.size()) return {};

    PinHandle inPinHandle = inputs[inputIndex];
    Pin* inPin = graph->getPin(inPinHandle);
    if (!inPin || !inPin->link.isValid()) return {};

    Link* link = graph->getLink(inPin->link);
    if (!link) return {};

    PinHandle srcPinHandle = link->startPin;
    Pin* srcPin = graph->getPin(srcPinHandle);
    if (!srcPin) return {};

    NodeHandle srcNodeHandle = srcPin->node;
    Node* srcNode = graph->getNode(srcNodeHandle);
    if (!srcNode) return {};

    uint64_t key = pinKey(srcPinHandle);

    // Cache Hit
    if (!srcNode->isDirty) {
        auto it = ctx.outputCache.find(key);
        if (it != ctx.outputCache.end()) return it->second;
    }

    // Cache Eviction
    for (PinHandle outPinHandle : srcNode->outputs) {
        uint64_t outKey = pinKey(outPinHandle);
        auto it = ctx.outputCache.find(outKey);
        if (it != ctx.outputCache.end()) {
            ctx.pendingImageReleases.push_back(it->second);
            ctx.outputCache.erase(it);
        }
    }

    // Re-entrancy Guard
    if (srcNode->isEvaluating) {
        std::cerr << "Cycle violation detected!" << std::endl;
        return {};
    }

    struct EvalGuard {
        Node* n;
        EvalGuard(Node* n) : n(n) { n->isEvaluating = true; }
        ~EvalGuard() { n->isEvaluating = false; }
    } guard(srcNode);

    srcNode->evaluate(ctx);
    srcNode->isDirty = false;

    return ctx.outputCache[key];
}

void ConstantNode::evaluate(EvaluationContext& ctx) {
    if (outputs.empty()) return;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    size_t pixelCount = (size_t)ctx.requestedExtent.width * ctx.requestedExtent.height;
    bufferInfo.size = pixelCount * 4 * sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkResult res = vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo, &stagingBuffer,
                                   &stagingAllocation, nullptr);
    assert(res == VK_SUCCESS);
    if (res != VK_SUCCESS) return;

    float color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    void* data;
    res = vmaMapMemory(ctx.allocator, stagingAllocation, &data);
    assert(res == VK_SUCCESS);
    for (size_t i = 0; i < pixelCount; ++i) {
        memcpy((float*)data + i * 4, color, sizeof(color));
    }
    vmaUnmapMemory(ctx.allocator, stagingAllocation);

    ctx.pendingBufferFrees.push_back({stagingBuffer, stagingAllocation});

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

    gpu::ImageHandle handle = ctx.imagePool->acquire(spec);
    VkImage image = ctx.imagePool->getImage(handle);

    transitionImage(ctx.cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = ctx.requestedExtent.width;
    region.imageExtent.height = ctx.requestedExtent.height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(ctx.cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);

    transitionImage(ctx.cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    ctx.outputCache[pinKey(outputs[0])] = handle;
}

void MergeNode::evaluate(EvaluationContext& ctx) {
    if (outputs.empty()) return;

    pullInput(ctx, 0);
    pullInput(ctx, 1);

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle handle = ctx.imagePool->acquire(spec);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    size_t pixelCount = (size_t)ctx.requestedExtent.width * ctx.requestedExtent.height;
    bufferInfo.size = pixelCount * 4 * sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    VkResult res = vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo, &stagingBuffer,
                                   &stagingAllocation, nullptr);
    assert(res == VK_SUCCESS);
    if (res != VK_SUCCESS) return;

    float purple[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    void* data;
    res = vmaMapMemory(ctx.allocator, stagingAllocation, &data);
    assert(res == VK_SUCCESS);
    for (size_t i = 0; i < pixelCount; ++i) {
        memcpy((float*)data + i * 4, purple, sizeof(purple));
    }
    vmaUnmapMemory(ctx.allocator, stagingAllocation);

    ctx.pendingBufferFrees.push_back({stagingBuffer, stagingAllocation});

    VkImage image = ctx.imagePool->getImage(handle);
    transitionImage(ctx.cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = ctx.requestedExtent.width;
    region.imageExtent.height = ctx.requestedExtent.height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(ctx.cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);

    transitionImage(ctx.cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    ctx.outputCache[pinKey(outputs[0])] = handle;
}

void ViewerNode::evaluate(EvaluationContext& ctx) { lastOutput = pullInput(ctx, 0); }

void PassthroughNode::evaluate(EvaluationContext& ctx) {
    gpu::ImageHandle in = pullInput(ctx, 0);
    if (!outputs.empty()) {
        ctx.outputCache[pinKey(outputs[0])] = in;
    }
}

}  // namespace loom::core
