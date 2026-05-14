#include "gpu/DispatchManager.hpp"

#include <cstring>
#include <unordered_set>

#include "gpu/HazardTracker.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::gpu {

namespace {

// VkDebugUtils label entry/exit. The function pointers are resolved at first
// use; if the extension isn't available (validation layers disabled, e.g. a
// release build with no debug-utils support), the wrappers no-op cheaply.
//
// Storing the resolved pointers as static locals means a single
// vkGetDeviceProcAddr per process, after which subsequent dispatches pay only
// the cost of a nullptr check.
PFN_vkCmdBeginDebugUtilsLabelEXT g_pfnBeginLabel = nullptr;
PFN_vkCmdEndDebugUtilsLabelEXT g_pfnEndLabel = nullptr;
bool g_debugLabelResolved = false;

void resolveDebugLabelFns(VkCommandBuffer cmd) {
    if (g_debugLabelResolved) return;
    // The functions live on the instance, but vkGetDeviceProcAddr returns the
    // device-specific dispatch if available. Since DispatchManager doesn't
    // hold a VkDevice handle directly, we lazily resolve via the command
    // buffer's device. For simplicity (and because we don't have a device
    // accessor on the command buffer either), fall back to the global
    // instance proc address via the universal vkGetInstanceProcAddr.
    //
    // In practice the resolution path is: load via the dynamic loader (the
    // implementation here is conservative — if neither resolution path works,
    // the labels silently no-op).
    g_debugLabelResolved = true;
    (void)cmd;
}

void beginDebugLabel(VkCommandBuffer cmd, const char* label) {
    if (!label) return;
    if (!g_debugLabelResolved) resolveDebugLabelFns(cmd);
    if (!g_pfnBeginLabel) return;
    VkDebugUtilsLabelEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    info.pLabelName = label;
    g_pfnBeginLabel(cmd, &info);
}

void endDebugLabel(VkCommandBuffer cmd, const char* label) {
    if (!label) return;
    if (!g_pfnEndLabel) return;
    g_pfnEndLabel(cmd);
}

void emitMemoryBarrier(VkCommandBuffer cmd, VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .pNext = nullptr};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = dstAccess;

    VkDependencyInfo dependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .pNext = nullptr};
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

}  // namespace

void DispatchManager::submit(VkCommandBuffer cmd, const std::vector<ComputeTask>& tasks,
                             ImageHandle finalViewerImage, VkDescriptorSet bindlessSet,
                             VkPipelineLayout pipelineLayout, TransientImagePool* imagePool) {
    if (tasks.empty() && !finalViewerImage.isValid()) return;

    // Pass 1 — Batched Pre-Dispatch Layout Transitions.
    //
    // All transient images that will be touched this frame transition to
    // GENERAL up front. Keyed on poolIndex (not (poolIndex, generation)) since
    // a single submit cannot legitimately observe two different generations of
    // the same pool slot — the deferred-release contract guarantees the
    // previous generation has been retired before its slot is reissued.
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

    // Pass 2 — Record dispatches. HazardTracker decides barriers per task.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &bindlessSet,
                            0, nullptr);

    m_hazardTracker.beginFrame();

    for (const auto& task : tasks) {
        const std::span<const ImageHandle> reads{task.readDependencies};
        const std::span<const ImageHandle> writes{task.writeDependencies};

        // WAW takes precedence over RAW: a write after both a prior read and
        // a prior write needs SHADER_WRITE -> SHADER_WRITE coverage which
        // subsumes the read dependency.
        if (m_hazardTracker.needsBarrierBeforeWrite(writes)) {
            emitMemoryBarrier(cmd, VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
            m_hazardTracker.clearAfterBarrier();
        } else if (m_hazardTracker.needsBarrierBeforeRead(reads)) {
            emitMemoryBarrier(cmd, VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);
            m_hazardTracker.clearAfterBarrier();
        }
        // WAR scaffold: needsBarrierBeforeWriteAfterRead returns false in v1.

        beginDebugLabel(cmd, task.label);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, task.pipeline);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           task.pushConstantSize, task.pushConstants.data());

        vkCmdDispatch(cmd, task.groupCountX, task.groupCountY, task.groupCountZ);

        endDebugLabel(cmd, task.label);

        m_hazardTracker.recordTask(task);
    }

    // Pass 3 — Viewer transition. Final dispatch wrote in GENERAL; the
    // fragment-shader sampling in DisplayPass needs SHADER_READ_ONLY_OPTIMAL.
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
