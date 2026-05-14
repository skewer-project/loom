#pragma once

#include <cstdint>

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
    struct DeepRef {
        ImageHandle countImage;
        ImageHandle offsetImage;
        BufferHandle samples;
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
};

}  // namespace loom::gpu
