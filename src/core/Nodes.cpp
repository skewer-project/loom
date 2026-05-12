#include "core/Nodes.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

#include "core/DeepExrLoader.hpp"
#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/RenderCache.hpp"
#include "gpu/BindlessHeap.hpp"
#include "gpu/ComputeTask.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"

namespace loom::core {

gpu::ImageHandle Node::pullInput(EvaluationContext& ctx, uint32_t inputIndex) {
    ...

        // -----------------------------------------------------------------------------
        // DeepReadNode
        // -----------------------------------------------------------------------------

        DeepReadNode::~DeepReadNode() {
        // Note: Can't easily release GPU resources here without a context.
        // In a real engine, we'd have a more robust resource management system.
    }

    void DeepReadNode::setFilepath(EvaluationContext & ctx, const std::string& path) {
        if (filepath == path) return;
        filepath = path;
        needsUpload = true;
    }

    void DeepReadNode::markRequiredTiles(const Region& requestedRegion,
                                         std::unordered_set<NodeHandle>& activeNodes) {
        activeNodes.insert(id);
    }

    void DeepReadNode::execute(EvaluationContext & ctx, const Region& region) {
        if (needsUpload && !filepath.empty()) {
            releaseGpuResources(ctx);

            DeepSampleBuffer cpuBuffer = DeepExrLoader::load(filepath);
            deepBuffer.width = cpuBuffer.width;
            deepBuffer.height = cpuBuffer.height;

            uint32_t totalSamples = static_cast<uint32_t>(cpuBuffer.sampleData.size() / 5);
            VkDeviceSize sampleBufferSize = cpuBuffer.sampleData.size() * sizeof(float);
            VkDeviceSize lookupBufferSize =
                cpuBuffer.width * cpuBuffer.height * 2 * sizeof(uint32_t);

            // 1. Staging buffer
            VkBuffer stagingBuffer;
            VmaAllocation stagingAllocation;
            VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            stagingInfo.size = sampleBufferSize + lookupBufferSize;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo stagingAllocInfo{};
            stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

            vmaCreateBuffer(ctx.allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer,
                            &stagingAllocation, nullptr);

            void* data;
            vmaMapMemory(ctx.allocator, stagingAllocation, &data);
            memcpy(data, cpuBuffer.sampleData.data(), sampleBufferSize);

            uint32_t* lookupPtr = (uint32_t*)((char*)data + sampleBufferSize);
            for (size_t i = 0; i < cpuBuffer.width * cpuBuffer.height; ++i) {
                lookupPtr[i * 2 + 0] = cpuBuffer.offsets[i];
                lookupPtr[i * 2 + 1] = cpuBuffer.counts[i];
            }
            vmaUnmapMemory(ctx.allocator, stagingAllocation);

            // 2 & 3. Acquire SSBOs
            deepBuffer.sampleBuffer = ctx.bufferPool->acquire(sampleBufferSize);
            deepBuffer.lookupBuffer = ctx.bufferPool->acquire(lookupBufferSize);

            // 4. Copy
            VkCommandBuffer copyCmd = ctx.vkContext->beginSingleTimeCommands();

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size = sampleBufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffer,
                            ctx.bufferPool->getBuffer(deepBuffer.sampleBuffer), 1, &copyRegion);

            copyRegion.srcOffset = sampleBufferSize;
            copyRegion.dstOffset = 0;
            copyRegion.size = lookupBufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffer,
                            ctx.bufferPool->getBuffer(deepBuffer.lookupBuffer), 1, &copyRegion);

            // 6. Barrier
            VkBufferMemoryBarrier2 sampleBarrier{.sType =
                                                     VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            sampleBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            sampleBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            sampleBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            sampleBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            sampleBarrier.buffer = ctx.bufferPool->getBuffer(deepBuffer.sampleBuffer);
            sampleBarrier.offset = 0;
            sampleBarrier.size = sampleBufferSize;

            VkBufferMemoryBarrier2 lookupBarrier = sampleBarrier;
            lookupBarrier.buffer = ctx.bufferPool->getBuffer(deepBuffer.lookupBuffer);
            lookupBarrier.size = lookupBufferSize;

            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            VkBufferMemoryBarrier2 barriers[] = {sampleBarrier, lookupBarrier};
            depInfo.bufferMemoryBarrierCount = 2;
            depInfo.pBufferMemoryBarriers = barriers;

            vkCmdPipelineBarrier2(copyCmd, &depInfo);

            ctx.vkContext->endSingleTimeCommands(copyCmd);

            vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAllocation);
            needsUpload = false;
        }

        if (!deepBuffer.sampleBuffer.isValid()) return;

        // Flattening
        gpu::ImageSpec spec{};
        spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        spec.extent = {deepBuffer.width, deepBuffer.height};
        spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        gpu::ImageHandle outputHandle = ctx.imagePool->acquire(spec);

        gpu::ComputeTask task{};
        task.pipeline = ctx.pipelineCache->getOrCreate("DeepFlatten.comp.spv");

        struct {
            uint32_t lookupSSBOSlot;
            uint32_t sampleSSBOSlot;
            uint32_t outputImageSlot;
            uint32_t width;
            uint32_t height;
        } pc;
        pc.lookupSSBOSlot = deepBuffer.lookupBuffer.bindlessSlot;
        pc.sampleSSBOSlot = deepBuffer.sampleBuffer.bindlessSlot;
        pc.outputImageSlot = outputHandle.bindlessSlot;
        pc.width = deepBuffer.width;
        pc.height = deepBuffer.height;

        memcpy(task.pushConstants.data(), &pc, sizeof(pc));
        task.pushConstantSize = sizeof(pc);
        task.groupCountX = (deepBuffer.width + 15) / 16;
        task.groupCountY = (deepBuffer.height + 15) / 16;
        task.groupCountZ = 1;

        task.writeDependencies.push_back(outputHandle);
        // Note: sampleBuffer and lookupBuffer should be read dependencies if we had that tracking
        // for buffers

        ctx.tasks.push_back(task);
        ctx.renderCache->store(outputs[0], region, outputHandle);
    }

    void DeepReadNode::releaseGpuResources(EvaluationContext & ctx) {
        if (deepBuffer.sampleBuffer.isValid()) {
            ctx.bufferPool->release(deepBuffer.sampleBuffer);
            deepBuffer.sampleBuffer = {};
        }
        if (deepBuffer.lookupBuffer.isValid()) {
            ctx.bufferPool->release(deepBuffer.lookupBuffer);
            deepBuffer.lookupBuffer = {};
        }
    }

}  // namespace loom::core
