#pragma once

#include <vulkan/vulkan.h>

#include "core/AABB.hpp"
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

// Reduce a parsed deep payload to its world-space AABB. Pure CPU work,
// no Vulkan dependency — exposed alongside `uploadDeepImage` so tests
// can pin the reduction behaviour without spinning up a GPU device.
//
// Behaviour:
//   - NVS files (layout carries Float32 `world_pos.{x,y,z}`): reduce
//     over per-sample world position directly. Samples whose position
//     hits the background sentinel (|x|, |y|, or |z| >= 1e9, or any
//     non-finite component) are discarded.
//   - Non-NVS files (only Float32 `Z`): synthesise XYZ to match
//     `PointCloud.vert`'s height-field rendering — XY in centered
//     [-1, 1] from pixel index, Z = -frontDepth — and reduce over
//     that. Same background-Z gate.
//   - Layout missing the required channels (or Z is not Float32, or
//     there are no samples): returns an `AABB{}` with `valid = false`.
//     The caller treats this as "do not auto-frame" — never as a
//     degenerate zero-extent box at the origin.
[[nodiscard]] core::AABB reduceSceneBounds(const io::ParsedDeepImage& src,
                                           const core::DeepLayout& layout);

// Recommended Z-scale for the `PointCloud.vert` synthesis path. Given a
// scene AABB (produced by `reduceSceneBounds`) and the deep layout, returns
// the value to plug into `PointCloud.vert`'s `zScale` push-constant so the
// synthesised depth range matches the XY range `[-1, 1]`:
//   - NVS payloads (`world_pos.*` present): `1.0` (no scaling — real units).
//   - Z-only payloads: `2 / extent.z` if the bounds are valid and the
//     z-extent is positive; `1.0` otherwise.
// Surfaced as a free function so the formula is unit-testable without a
// GPU device (matches the testability pattern used for `reduceSceneBounds`).
[[nodiscard]] float computeRecommendedZScale(const core::AABB& sceneBounds,
                                             const core::DeepLayout& layout);

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
