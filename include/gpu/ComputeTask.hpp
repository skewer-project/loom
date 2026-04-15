#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

#include "gpu/ResourceHandles.hpp"

namespace loom::gpu {

struct ComputeTask {
    VkPipeline pipeline;
    std::array<uint8_t, 128> pushConstants;  // 128 bytes: minimum guaranteed by spec
    uint32_t pushConstantSize;
    uint32_t groupCountX, groupCountY, groupCountZ;

    // ImageHandles this task reads from. Used by DispatchManager for RAW
    // hazard detection and layout validation.
    std::vector<ImageHandle> readDependencies;

    // ImageHandles this task writes to. Used by DispatchManager to track
    // which images have been written during this submit call.
    std::vector<ImageHandle> writeDependencies;
};

}  // namespace loom::gpu
