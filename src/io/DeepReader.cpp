#include "io/DeepReader.hpp"

#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfHeader.h>
#include <ImfPartType.h>

#include <cstring>
#include <vector>

#include "core/Log.hpp"

namespace loom::io {

namespace {

core::ChannelType pixelTypeToChannelType(Imf::PixelType t) {
    switch (t) {
        case Imf::HALF:
            return core::ChannelType::Float16;
        case Imf::FLOAT:
            return core::ChannelType::Float32;
        case Imf::UINT:
            return core::ChannelType::UInt32;
        default:
            // OpenEXR has no other pixel types as of 3.2; treat the unknown
            // case as Float32 and log. The reader never refuses to load.
            loom::log::warn("DeepReader: unknown PixelType, defaulting to Float32");
            return core::ChannelType::Float32;
    }
}

// Build the deep layout from an EXR header. v1 records every channel as a
// scalar entry (components = 1). Phase D.1 extends this to coalesce
// `world_pos.{x,y,z}` / `normal.{x,y,z}` / `albedo.{r,g,b}` into vec3
// channels per docs/CONVENTIONS.md §19.
//
// Channel order is preserved verbatim from the EXR header. OpenEXR sorts
// alphabetically by name on insertion, which is stable across writers and
// matches what `core::DeepLayout` interns on.
const core::DeepLayout* buildLayoutFromHeader(const Imf::Header& header) {
    std::vector<core::DeepChannel> channels;
    for (auto it = header.channels().begin(); it != header.channels().end(); ++it) {
        core::DeepChannel ch;
        ch.name = it.name();
        ch.type = pixelTypeToChannelType(it.channel().type);
        ch.components = 1;
        channels.push_back(std::move(ch));
    }
    return core::getDeepLayout(std::move(channels));
}

DeepFrame readSync(const std::string& path) {
    using namespace Imf;
    using namespace Imath;

    Imf::DeepScanLineInputFile file(path.c_str());
    const Imf::Header& header = file.header();
    if (!header.hasType() || header.type() != Imf::DEEPSCANLINE) {
        loom::log::warn("DeepReader: '", path, "' is not a deep scanline EXR");
        return {};
    }

    Box2i dw = header.dataWindow();
    const int width = dw.max.x - dw.min.x + 1;
    const int height = dw.max.y - dw.min.y + 1;
    if (width <= 0 || height <= 0) {
        loom::log::warn("DeepReader: '", path, "' has an empty data window");
        return {};
    }

    const core::DeepLayout* layout = buildLayoutFromHeader(header);

    // Sample-count pass. The base-pointer accounts for non-zero data-window
    // origin: OpenEXR's stride math is `slice[y*yStride + x*xStride]` with
    // `slice` expected to be addressable at `(dw.min.x, dw.min.y)`. We
    // allocate a flat zero-based buffer and offset the pointer.
    DeepFrame out;
    out.layout = layout;
    out.image.width = static_cast<uint32_t>(width);
    out.image.height = static_cast<uint32_t>(height);
    out.image.sampleCounts.assign(static_cast<size_t>(width) * height, 0);

    const int xStrideU = sizeof(uint32_t);
    const int yStrideU = sizeof(uint32_t) * width;
    char* countsBase =
        reinterpret_cast<char*>(out.image.sampleCounts.data()) -
        (static_cast<ptrdiff_t>(dw.min.x) * xStrideU + static_cast<ptrdiff_t>(dw.min.y) * yStrideU);

    Imf::DeepFrameBuffer fb;
    fb.insertSampleCountSlice(Imf::Slice(Imf::UINT, countsBase, xStrideU, yStrideU));
    file.setFrameBuffer(fb);
    file.readPixelSampleCounts(dw.min.y, dw.max.y);

    uint64_t totalSamples = 0;
    for (uint32_t c : out.image.sampleCounts) totalSamples += c;

    // Allocate per-channel flat storage and per-pixel pointer grids. Each
    // pixel's pointer indexes into the flat per-channel buffer using the
    // prefix-sum offset; OpenEXR's deep reader writes the per-pixel sample
    // count's worth of bytes starting at that pointer.
    const size_t nch = layout->channelCount();
    std::vector<uint64_t> pixelOffsets(static_cast<size_t>(width) * height, 0);
    {
        uint64_t cursor = 0;
        for (size_t i = 0; i < pixelOffsets.size(); ++i) {
            pixelOffsets[i] = cursor;
            cursor += out.image.sampleCounts[i];
        }
    }

    out.image.channelData.resize(nch);
    // Each channel's pointer-per-pixel grid is stored as a flat `void**`
    // array we can hand to the OpenEXR slice. The grid itself lives for
    // the duration of this function only.
    std::vector<std::vector<char*>> pointerGrids(nch);

    for (size_t ch = 0; ch < nch; ++ch) {
        const uint32_t byteSize = layout->byteSize(static_cast<int32_t>(ch));
        out.image.channelData[ch].assign(byteSize * totalSamples, 0);
        pointerGrids[ch].assign(static_cast<size_t>(width) * height, nullptr);
        for (size_t i = 0; i < pixelOffsets.size(); ++i) {
            if (out.image.sampleCounts[i] == 0) continue;
            pointerGrids[ch][i] = reinterpret_cast<char*>(out.image.channelData[ch].data() +
                                                          pixelOffsets[i] * byteSize);
        }
    }

    // Build a fresh framebuffer with every channel slice attached. We must
    // re-insert the sample-count slice; OpenEXR drops its cache on the
    // setFrameBuffer call and we re-read sample counts before readPixels.
    Imf::DeepFrameBuffer fb2;
    fb2.insertSampleCountSlice(Imf::Slice(Imf::UINT, countsBase, xStrideU, yStrideU));

    for (size_t ch = 0; ch < nch; ++ch) {
        const auto& spec = layout->channels()[ch];
        const uint32_t sampleStride = layout->byteSize(static_cast<int32_t>(ch));
        Imf::PixelType pt = Imf::FLOAT;
        switch (spec.type) {
            case core::ChannelType::Float16:
                pt = Imf::HALF;
                break;
            case core::ChannelType::Float32:
                pt = Imf::FLOAT;
                break;
            case core::ChannelType::UInt32:
                pt = Imf::UINT;
                break;
        }

        char* base =
            reinterpret_cast<char*>(pointerGrids[ch].data()) -
            (static_cast<ptrdiff_t>(dw.min.x) * static_cast<ptrdiff_t>(sizeof(char*)) +
             static_cast<ptrdiff_t>(dw.min.y) * static_cast<ptrdiff_t>(sizeof(char*) * width));
        fb2.insert(spec.name,
                   Imf::DeepSlice(pt, base, sizeof(char*), sizeof(char*) * width, sampleStride));
    }

    file.setFrameBuffer(fb2);
    file.readPixelSampleCounts(dw.min.y, dw.max.y);
    file.readPixels(dw.min.y, dw.max.y);

    return out;
}

}  // namespace

std::future<DeepFrame> SyncDeepReader::readFrame(const std::string& path, int /*frameIndex*/) {
    std::promise<DeepFrame> p;
    try {
        p.set_value(readSync(path));
    } catch (const std::exception& e) {
        loom::log::error("DeepReader: '", path, "' read failed: ", e.what());
        p.set_value(DeepFrame{});  // invalid frame, total-functional contract
    }
    return p.get_future();
}

}  // namespace loom::io
