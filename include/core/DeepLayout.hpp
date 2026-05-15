#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace loom::core {

// Numeric type of a deep-EXR channel. Mirrors the three types OpenEXR's
// PixelType enum offers (`UINT`, `HALF`, `FLOAT`) — see docs/CONVENTIONS.md §19
// for the channel-naming convention layered on top.
enum class ChannelType : uint8_t {
    Float16 = 0,
    Float32 = 1,
    UInt32 = 2,
};

// Bytes occupied by one scalar element of the given type. Channels with
// `components > 1` multiply this by their component count to yield the
// channel-wide byte size (see DeepLayout::byteSize).
[[nodiscard]] inline constexpr uint32_t channelTypeBytes(ChannelType t) noexcept {
    switch (t) {
        case ChannelType::Float16:
            return 2;
        case ChannelType::Float32:
            return 4;
        case ChannelType::UInt32:
            return 4;
    }
    return 0;
}

// Declarative description of one deep-EXR channel.
//
// `name` is the channel name as it appears in the EXR header — see §19 for the
// v1 / NVS-extension reserved names (`Z`, `ZBack`, `R`, `G`, `B`, `A`,
// `world_pos.{x,y,z}`, ...).
//
// `components` is the number of scalar elements coalesced under a single
// logical channel. The EXR file format itself only knows about scalar
// channels, so a vec3 attribute (e.g. `world_pos`) is three scalar channels
// in the file (`world_pos.x|y|z`) but one logical channel (components=3) in
// the layout. Coalescing is the loader's responsibility (Phase B.1); the
// layout structure here is agnostic to whether channels were coalesced or
// stored individually.
struct DeepChannel {
    std::string name;
    ChannelType type = ChannelType::Float32;
    uint8_t components = 1;

    [[nodiscard]] bool operator==(const DeepChannel&) const = default;
};

// Interned, ordered, hashable description of the per-sample record layout
// used by a deep image.
//
// A DeepLayout is the canonical mapping from "channel name" to "byte offset
// within a packed sample record". The packed-sample buffer that lives on the
// GPU (see `gpu::ResourceRef::DeepRef::samples`) carries `stride()` bytes per
// sample; per-channel reads in shaders are `byteOffset(channel) +
// sampleIndex * stride()`.
//
// Packing is tight (no inter-channel padding). v1 makes no alignment
// guarantees beyond the natural alignment of the scalar element type —
// callers that need wider alignment should declare an explicit padding
// channel. Mixing `Float16` channels with `Float32` channels produces 2-byte
// alignment for the wider channels; the shader-side load is still correct
// (SSBO accesses tolerate 2-byte alignment for 4-byte loads on every desktop
// GPU we target), but a future revision may introduce automatic padding.
//
// Identity is by pointer: two DeepLayouts compare equal iff they live at the
// same address. The DeepLayoutRegistry is responsible for canonicalising
// content-identical layouts into the same pointer. Callers must always
// obtain a layout via `getDeepLayout(channels)` rather than constructing
// one directly.
class DeepLayout {
  public:
    // Construct from an ordered channel list. Channel order is preserved
    // verbatim — two layouts with the same channels in different orders are
    // distinct (and yield different pointers from the registry).
    explicit DeepLayout(std::vector<DeepChannel> channels);

    [[nodiscard]] std::span<const DeepChannel> channels() const noexcept { return m_channels; }

    [[nodiscard]] size_t channelCount() const noexcept { return m_channels.size(); }

    // Returns the index of the channel with the given name, or -1 if absent.
    // `findChannel` is the supported lookup API; iterating `channels()` and
    // matching on `name` works but is less robust against future renames.
    [[nodiscard]] int32_t findChannel(std::string_view name) const noexcept;

    // Byte offset of the named channel within a packed sample record.
    // Returns `kInvalidOffset` if the channel is absent.
    [[nodiscard]] uint32_t byteOffset(std::string_view name) const noexcept;
    [[nodiscard]] uint32_t byteOffset(int32_t channelIndex) const noexcept;

    // Bytes occupied by the named channel (`type bytes` × `components`).
    [[nodiscard]] uint32_t byteSize(int32_t channelIndex) const noexcept;

    // Bytes occupied by one packed sample record across all channels.
    [[nodiscard]] uint32_t stride() const noexcept { return m_stride; }

    // Content hash. Stable across runs for the same channel sequence — used
    // both by the registry and by `RenderCache` key derivations.
    [[nodiscard]] size_t hash() const noexcept { return m_hash; }

    static constexpr uint32_t kInvalidOffset = 0xFFFFFFFFu;

  private:
    std::vector<DeepChannel> m_channels;
    std::vector<uint32_t> m_offsets;  // parallel to m_channels
    uint32_t m_stride = 0;
    size_t m_hash = 0;
};

// Process-wide content-addressed registry. Hands out a stable pointer for
// each unique layout content; two requests with the same `channels` sequence
// always return the same `const DeepLayout*`.
//
// The registry owns the lifetime of every layout it returns. Returned
// pointers are valid for the lifetime of the process.
//
// Thread-safety: the registry is single-threaded as of this branch (matches
// CONVENTIONS §13). The Phase C.1 worker thread parses EXR data off the main
// thread but hands the channel list back to the main thread for interning —
// no concurrent registry access.
[[nodiscard]] const DeepLayout* getDeepLayout(std::vector<DeepChannel> channels);

// Diagnostic accessor. Returns the number of distinct interned layouts.
// Not part of the production contract; the `DEBUG_` prefix follows the same
// convention as `TransientImagePool::DEBUG_getFreeSlotCount`.
[[nodiscard]] size_t DEBUG_internedLayoutCount();

// Wipe the registry. Test-only; production code never re-initialises it.
void DEBUG_clearLayoutRegistry();

}  // namespace loom::core

namespace std {
template <>
struct hash<loom::core::DeepChannel> {
    size_t operator()(const loom::core::DeepChannel& c) const noexcept;
};
}  // namespace std
