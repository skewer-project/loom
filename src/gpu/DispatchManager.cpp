#include "gpu/DispatchManager.hpp"

#include <cstring>
#include <unordered_set>

#include "gpu/TransientImagePool.hpp"

namespace loom::gpu {

void DispatchManager::submit(VkCommandBuffer cmd, const std::vector<ComputeTask>& tasks,
                             ImageHandle finalViewerImage, VkDescriptorSet bindlessSet,
                             VkPipelineLayout pipelineLayout, TransientImagePool* imagePool) {
    if (tasks.empty() && !finalViewerImage.isValid()) return;

    // Pass 1 — Batched Pre-Dispatch Layout Transitions
    std::vector<VkImageMemoryBarrier2> barriers;
    std::unordered_set<uint32_t> processedImages;

    auto addBarrier = [&](ImageHandle handle) {
        if (!handle.isValid()) return;
        if (processedImages.contains(handle.poolIndex)) return;

        VkImageLayout currentLayout = imagePool->getLayout(handle);
        if (currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
            VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                          .pNext = nullptr};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.oldLayout = currentLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.image = imagePool->getImage(handle);
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            barriers.push_back(barrier);
            imagePool->setLayout(handle, VK_IMAGE_LAYOUT_GENERAL);
        }
        processedImages.insert(handle.poolIndex);
    };

    for (const auto& task : tasks) {
        for (auto h : task.readDependencies) addBarrier(h);
        for (auto h : task.writeDependencies) addBarrier(h);
    }
    if (finalViewerImage.isValid()) addBarrier(finalViewerImage);

    if (!barriers.empty()) {
        VkDependencyInfo dependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                        .pNext = nullptr};
        dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependencyInfo.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    // Pass 2 — Record Dispatches with RAW Hazard Sync
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &bindlessSet,
                            0, nullptr);

    std::unordered_set<uint32_t> writtenSlots;

    for (const auto& task : tasks) {
        bool needsHazardBarrier = false;
        for (auto h : task.readDependencies) {
            if (writtenSlots.contains(h.bindlessSlot)) {
                needsHazardBarrier = true;
                break;
            }
        }

        if (needsHazardBarrier) {
            VkMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .pNext = nullptr};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

            VkDependencyInfo dependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                            .pNext = nullptr};
            dependencyInfo.memoryBarrierCount = 1;
            dependencyInfo.pMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);

            // Clear written slots after barrier to avoid redundant barriers
            // Actually, we should probably keep them if they are written again?
            // The prompt says "Maintains a tracking set for write-hazard detection."
            // "For each task: 1. RAW Hazard Check ... if any match is found, insert ... before this
            // dispatch" So we don't necessarily clear it, but the barrier covers all previous
            // writes.
            writtenSlots.clear();
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, task.pipeline);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           task.pushConstantSize, task.pushConstants.data());

        vkCmdDispatch(cmd, task.groupCountX, task.groupCountY, task.groupCountZ);

        for (auto h : task.writeDependencies) {
            writtenSlots.insert(h.bindlessSlot);
        }
    }

    // Pass 3 — Viewer Transition
    if (finalViewerImage.isValid()) {
        VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                      .pNext = nullptr};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = imagePool->getImage(finalViewerImage);
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                        .pNext = nullptr};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);

        imagePool->setLayout(finalViewerImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

}  // namespace loom::gpu
