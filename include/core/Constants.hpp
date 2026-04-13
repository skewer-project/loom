#pragma once

namespace loom::core {

// The number of frames the CPU can prepare ahead of the GPU
// 2 is the standard balance between latency and throughput
static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

static constexpr int MAX_SWAPCHAIN_IMAGES = 8;

}  // namespace loom::core
