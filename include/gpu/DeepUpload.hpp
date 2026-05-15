#pragma once

#include <vulkan/vulkan.h>

#include "gpu/ResourceHandles.hpp"

namespace loom::core {
class DeepLayout;
}

namespace loom::io {
struct ParsedDeepImage;
}

namespace loom::gpu {

class StagingArena;
class TransientBufferPool;
class TransientImagePool;

// Upload a CPU-side parsed deep image to GPU resources:
//
//   - `countImage`  — R32_UINT 2D image, width × height, per-pixel sample count.
//   - `offsetImage` — R32_UINT 2D image, prefix-sum offsets into `samples`.
//   - `samples`     — flat device-local SSBO, `layout.stride() * totalSamples`
//                     bytes. The CPU-side SoA channel data is interleaved
//                     into the packed AoS record layout described by
//                     `layout` before being staged.
//
// Resources are acquired from the supplied pools and registered into the
// bindless heap by those pools. The returned `ResourceRef` is `Kind::Deep`
// with `deep.layout = &layout`.
//
// The `cmd` parameter must be a recording command buffer; the function
// emits the `vkCmdCopyBuffer` / `vkCmdCopyBufferToImage` calls inline. The
// caller submits and waits (or chains into the frame loop) as appropriate.
//
// Staging memory is sourced from `staging`. The caller is responsible for
// `staging.reset()` after the GPU has consumed the copies. OOM in staging
// (the parsed image is larger than the arena) returns an invalid
// `ResourceRef` and does not partially fill `samples` / `count` / `offset`.
[[nodiscard]] ResourceRef uploadDeepImage(VkCommandBuffer cmd, StagingArena& staging,
                                          TransientImagePool& imagePool,
                                          TransientBufferPool& bufferPool,
                                          const io::ParsedDeepImage& src,
                                          const core::DeepLayout& layout);

}  // namespace loom::gpu
