#pragma once

#include <cstdint>

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
    enum class Kind : uint8_t { None, Image, Buffer, Deep };
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
    struct DeepRef {
        ImageHandle countImage;
        ImageHandle offsetImage;
        BufferHandle samples;
        const core::DeepLayout* layout = nullptr;
    } deep;

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
};

}  // namespace loom::gpu
