#pragma once

#include <vulkan/vulkan.h>

namespace loom::gpu {

// Records a single image-layout transition on `cmd` via vkCmdPipelineBarrier2.
// Stage and access masks are derived from the source and destination layouts
// via an internal switch table — unknown layouts LOOM_ASSERT rather than
// silently emit zero masks (which is the textbook unsynchronised-hazard bug
// the helper was originally written to avoid).
//
// Supported layouts in v1: UNDEFINED, GENERAL, COLOR_ATTACHMENT_OPTIMAL,
// SHADER_READ_ONLY_OPTIMAL, TRANSFER_SRC_OPTIMAL, TRANSFER_DST_OPTIMAL,
// PRESENT_SRC_KHR. Adding a new layout means extending the table in
// LayoutTransitions.cpp.
void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                           VkImageLayout newLayout);

}  // namespace loom::gpu
