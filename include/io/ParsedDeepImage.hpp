#pragma once

#include <cstdint>
#include <vector>

namespace loom::io {

// CPU-side representation of a parsed deep image. Produced by the Phase B.1
// `IDeepReader::readFrame` and consumed by `gpu::uploadDeepImage`. The
// fields are SoA — one byte-blob per channel — because that's the shape
// OpenEXR's deep API gives us. The upload helper interleaves into the
// packed AoS form on its way to the GPU.
//
// The `DeepLayout*` schema lives outside this struct: `uploadDeepImage`
// takes the layout as a separate argument so a producer can hand off a
// `ParsedDeepImage` plus the interned layout pointer from
// `core::getDeepLayout`. Decoupling them lets one layout serve many
// frames in an animation sequence.
struct ParsedDeepImage {
    uint32_t width = 0;
    uint32_t height = 0;

    // Per-pixel sample count, row-major, length = width * height.
    std::vector<uint32_t> sampleCounts;

    // Per-channel raw bytes, parallel to the consuming layout's
    // `channels()`. `channelData[ch].size()` is
    // `byteSize(ch) * totalSamples` — the samples for channel `ch`
    // concatenated contiguously.
    std::vector<std::vector<uint8_t>> channelData;

    [[nodiscard]] uint64_t totalSamples() const {
        uint64_t total = 0;
        for (uint32_t c : sampleCounts) total += c;
        return total;
    }
};

}  // namespace loom::io
