#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "core/AABB.hpp"

namespace loom::core {
class DeepLayout;
}

namespace loom::gpu {

struct ImageHandle {
    uint32_t poolIndex = 0xFFFFFFFF;
    uint32_t bindlessSlot = 0xFFFFFFFF;
    uint32_t generation = 0xFFFFFFFF;

    bool operator==(const ImageHandle& other) const {
        return poolIndex == other.poolIndex && bindlessSlot == other.bindlessSlot &&
               generation == other.generation;
    }
    bool operator!=(const ImageHandle& other) const { return !(*this == other); }
    [[nodiscard]] bool isValid() const { return poolIndex != 0xFFFFFFFF; }
};

struct BufferHandle {
    uint32_t poolIndex = 0xFFFFFFFF;
    uint32_t bindlessSlot = 0xFFFFFFFF;
    uint32_t generation = 0xFFFFFFFF;

    bool operator==(const BufferHandle& other) const {
        return poolIndex == other.poolIndex && bindlessSlot == other.bindlessSlot &&
               generation == other.generation;
    }
    bool operator!=(const BufferHandle& other) const { return !(*this == other); }
    [[nodiscard]] bool isValid() const { return poolIndex != 0xFFFFFFFF; }
};

// Tagged-union payload carried on every pin edge and stored in the
// RenderCache. v1 production code only ever populates `Kind::Image`;
// `Kind::Buffer` and `Kind::Deep` are reserved in the union so that future
// node types (geometry buffers, motion vectors, OpenEXR deep compositing) can
// land without touching call sites.
struct ResourceRef {
    enum class Kind : uint8_t { None, Image, Buffer, Deep, Camera };
    Kind kind = Kind::None;

    ImageHandle image;
    BufferHandle buffer;
    // DeepRef carries everything a deep-EXR consumer needs to walk per-pixel
    // sample data on the GPU:
    //   - `countImage`  — R32_UINT image, samples-per-pixel.
    //   - `offsetImage` — R32_UINT image, prefix-sum offsets into `samples`.
    //   - `samples`     — flat device-local SSBO; per-sample stride and per-
    //                     channel offsets come from `layout`.
    //   - `layout`      — process-interned `core::DeepLayout` pointer
    //                     describing channel names / types / byte layout.
    //                     nullable when the ref carries an empty / placeholder
    //                     deep handle (e.g. uninitialised pin payload), but
    //                     production-path consumers must check before use.
    //                     Pointer equality with another DeepRef::layout means
    //                     the two refs share the same channel schema.
    //   - `width`/`height` — source image dimensions in pixels. Used by
    //                     consumers (`DeepFlattenNode`, point-cloud passes)
    //                     to size their dispatch / draw extent.
    //   - `sampleToPixel` — flat device-local SSBO of `uint32` length
    //                     `totalSamples`; entry `i` is the source pixel
    //                     index (`py * width + px`) for sample `i`. Lets
    //                     a vertex shader (Phase B.5 `PointCloudPass`) map
    //                     `gl_VertexIndex` → world position without a
    //                     binary search over `offsetImage`. Optional —
    //                     consumers that don't need per-sample pixel
    //                     ancestry can ignore it.
    //   - `sceneBounds`  — world-space AABB of the deep payload's sample
    //                     positions. Computed by `gpu::uploadDeepImage`
    //                     during its CPU-side interleave pass. Drives the
    //                     viewport's auto-frame (Phase B.8.2) so the
    //                     orbit camera reads a sensible starting pose
    //                     regardless of how far the producer placed the
    //                     scene from the origin. `valid == false` when
    //                     the payload had no usable position samples
    //                     (background-sentinel-only file, Float16 Z that
    //                     v1 can't reduce, etc.). Consumers must check
    //                     before trusting `center` / `radius`.
    struct DeepRef {
        ImageHandle countImage;
        ImageHandle offsetImage;
        BufferHandle samples;
        BufferHandle sampleToPixel;
        const core::DeepLayout* layout = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t totalSamples = 0;
        core::AABB sceneBounds{};

        // Per-payload Z-scaling hint for point-cloud rendering when the
        // payload has only front-depth (no `world_pos.*` channels). The
        // shader synthesises XY in centered [-1, 1]; multiplying the raw Z
        // by `recommendedZScale` normalises the depth range to a
        // comparable extent, so the synthesised slab is roughly
        // proportioned and the orbit camera feels natural. Computed by
        // `gpu::reduceSceneBounds` as `2 / (zMax - zMin)`. NVS payloads
        // (with `world_pos.*`) ignore this — their positions are in real
        // world units.
        //
        // Default `1.0f` keeps the v1 PointCloud shader's behaviour for
        // payloads without bounds information.
        float recommendedZScale = 1.0f;
    } deep;

    // CameraRef is a value-type snapshot of a `core::Camera`'s state. Carried
    // through the graph by `Kind::Camera` pins (typically produced by
    // `core::CameraNode` and consumed by 3D renderers such as
    // `PointCloudRenderNode`). The snapshot model avoids pointer-lifetime
    // questions through `RenderCache` — same shape as `DeepRef`. All fields
    // are scene-referred (RH / Y-up / meters per CONVENTIONS §19).
    struct CameraRef {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::vec3 eyePos{0.0f};
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        float fovY = 1.047197551f;  // 60° in radians
    } camera;

    [[nodiscard]] bool isValid() const { return kind != Kind::None; }

    [[nodiscard]] static ResourceRef fromImage(ImageHandle h) {
        ResourceRef r;
        r.kind = Kind::Image;
        r.image = h;
        return r;
    }
    [[nodiscard]] static ResourceRef fromBuffer(BufferHandle h) {
        ResourceRef r;
        r.kind = Kind::Buffer;
        r.buffer = h;
        return r;
    }
    [[nodiscard]] static ResourceRef fromDeep(DeepRef d) {
        ResourceRef r;
        r.kind = Kind::Deep;
        r.deep = d;
        return r;
    }
    [[nodiscard]] static ResourceRef fromCamera(CameraRef c) {
        ResourceRef r;
        r.kind = Kind::Camera;
        r.camera = c;
        return r;
    }
};

}  // namespace loom::gpu
