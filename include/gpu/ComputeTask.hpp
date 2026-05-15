#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "gpu/ResourceHandles.hpp"

namespace loom::gpu {

// Maximum guaranteed by the Vulkan spec is 128 bytes. Larger payloads must
// move to a uniform / storage buffer.
inline constexpr size_t MAX_PUSH_CONSTANT_BYTES = 128;

struct ComputeTask {
    VkPipeline pipeline;
    std::array<uint8_t, MAX_PUSH_CONSTANT_BYTES> pushConstants;
    uint32_t pushConstantSize;
    uint32_t groupCountX, groupCountY, groupCountZ;

    // ImageHandles this task reads from. Used by DispatchManager for RAW
    // hazard detection and layout validation.
    std::vector<ImageHandle> readDependencies;

    // ImageHandles this task writes to. Used by DispatchManager to track
    // which images have been written during this submit call.
    std::vector<ImageHandle> writeDependencies;

    // BufferHandles this task reads from / writes to. Empty for image-only
    // dispatches; populated by deep-EXR consumers and by future buffer-
    // backed nodes. Parallel to readDependencies / writeDependencies in
    // every respect (RAW + WAW hazard tracking through HazardTracker).
    std::vector<BufferHandle> readBuffers;
    std::vector<BufferHandle> writeBuffers;

    // Optional human-readable label for RenderDoc / NSight / GPU validation
    // layer output. Wrapped in vkCmdBeginDebugUtilsLabelEXT /
    // vkCmdEndDebugUtilsLabelEXT in debug builds when set; ignored in release.
    // Storage is borrowed (not owned) — pass a string literal or a buffer
    // whose lifetime exceeds the dispatch.
    const char* label = nullptr;

    // Typed push-constant helper. Asserts at compile time that `T` fits the
    // 128-byte budget and is trivially copyable so a raw memcpy is sound.
    // Replaces hand-rolled `memcpy(pushConstants.data(), &pc, sizeof(pc));
    // pushConstantSize = sizeof(pc);` patterns and removes the way to
    // accidentally overrun the buffer.
    template <typename T>
    void setPushConstants(const T& pc) {
        static_assert(sizeof(T) <= MAX_PUSH_CONSTANT_BYTES,
                      "push-constant payload exceeds the 128-byte spec minimum; "
                      "move it to a uniform / storage buffer");
        static_assert(std::is_trivially_copyable_v<T>,
                      "push-constant payload must be trivially copyable");
        std::memcpy(pushConstants.data(), &pc, sizeof(T));
        pushConstantSize = sizeof(T);
    }
};

}  // namespace loom::gpu
