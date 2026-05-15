// Phase A.0 — throwaway load spike.
//
// Reads a deep EXR via OpenEXR's DeepScanLineInputFile and prints:
//   - width / height
//   - channel names + types (this is the channel-naming contract from
//     docs/CONVENTIONS.md §19; the spike is the smallest possible inspector)
//   - total sample count and average samples / pixel
//   - depth range (min/max Z across all samples)
//
// Usage:
//   ./exr_spike <path-to-deep.exr>
//   ./exr_spike                       # reads tests/data/deep_smoke.exr
//
// Not part of the shipping build. The DeepLayout abstraction (A.1) and the
// IDeepReader interface (B.1) replace this with first-class engine pieces.

#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfHeader.h>
#include <ImfPartType.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

const char* pixelTypeName(Imf::PixelType t) {
    switch (t) {
        case Imf::UINT:
            return "uint";
        case Imf::HALF:
            return "half";
        case Imf::FLOAT:
            return "float";
        default:
            return "?";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    if (argc >= 2) {
        path = argv[1];
    } else {
#ifdef LOOM_FIXTURE_DIR
        path = std::string(LOOM_FIXTURE_DIR) + "/deep_smoke.exr";
#else
        std::fprintf(stderr, "usage: exr_spike <path-to-deep.exr>\n");
        return 1;
#endif
    }

    try {
        using namespace Imf;
        using namespace Imath;

        DeepScanLineInputFile file(path.c_str());
        const Header& header = file.header();

        if (!header.hasType() || header.type() != DEEPSCANLINE) {
            std::fprintf(stderr, "%s is not a deep scanline EXR (type=%s)\n", path.c_str(),
                         header.hasType() ? header.type().c_str() : "<unset>");
            return 2;
        }

        Box2i dw = header.dataWindow();
        const int width = dw.max.x - dw.min.x + 1;
        const int height = dw.max.y - dw.min.y + 1;

        std::printf("file: %s\n", path.c_str());
        std::printf("  width  = %d\n", width);
        std::printf("  height = %d\n", height);
        std::printf("  channels:\n");
        for (auto it = header.channels().begin(); it != header.channels().end(); ++it) {
            std::printf("    %s : %s\n", it.name(), pixelTypeName(it.channel().type));
        }

        // Read sample-counts so we can size per-channel storage, then chain a
        // single Z pass to compute the depth range. The two passes share one
        // framebuffer — the input file remembers which slices have been read
        // and rebuilding fb mid-stream invalidates the sample-count snapshot.
        std::vector<uint32_t> counts(static_cast<size_t>(width) * height, 0);
        const int xStride = sizeof(uint32_t);
        const int yStride = sizeof(uint32_t) * width;
        char* countsBase =
            reinterpret_cast<char*>(counts.data()) - (static_cast<ptrdiff_t>(dw.min.x) * xStride +
                                                      static_cast<ptrdiff_t>(dw.min.y) * yStride);

        DeepFrameBuffer fb;
        fb.insertSampleCountSlice(Slice(UINT, countsBase, xStride, yStride));
        file.setFrameBuffer(fb);
        file.readPixelSampleCounts(dw.min.y, dw.max.y);

        uint64_t totalSamples = 0;
        for (uint32_t c : counts) totalSamples += c;
        std::printf("  total samples = %llu\n", static_cast<unsigned long long>(totalSamples));
        std::printf("  avg samples/px = %.3f\n",
                    static_cast<double>(totalSamples) /
                        static_cast<double>(static_cast<uint64_t>(width) * height));

        const bool hasZ = header.channels().findChannel("Z") != nullptr;
        if (hasZ && totalSamples > 0) {
            std::vector<float> zStore(totalSamples);
            std::vector<float*> zPtrs(static_cast<size_t>(width) * height, nullptr);
            uint64_t cursor = 0;
            for (size_t i = 0; i < counts.size(); ++i) {
                if (counts[i] == 0) continue;
                zPtrs[i] = &zStore[cursor];
                cursor += counts[i];
            }

            char* zBase =
                reinterpret_cast<char*>(zPtrs.data()) -
                (static_cast<ptrdiff_t>(dw.min.x) * static_cast<ptrdiff_t>(sizeof(float*)) +
                 static_cast<ptrdiff_t>(dw.min.y) * static_cast<ptrdiff_t>(sizeof(float*) * width));
            fb.insert("Z", DeepSlice(FLOAT, zBase, sizeof(float*), sizeof(float*) * width,
                                     sizeof(float)));
            file.setFrameBuffer(fb);
            // Re-setting the framebuffer drops the input file's internal
            // sample-count cache. The Vulkan-shaped pre-allocation we just
            // did is still valid (we own `counts`), but OpenEXR needs to
            // re-walk the file to re-establish what it's about to write.
            file.readPixelSampleCounts(dw.min.y, dw.max.y);
            file.readPixels(dw.min.y, dw.max.y);

            float zMin = std::numeric_limits<float>::infinity();
            float zMax = -std::numeric_limits<float>::infinity();
            for (float z : zStore) {
                zMin = std::min(zMin, z);
                zMax = std::max(zMax, z);
            }
            std::printf("  Z range = [%g, %g]\n", zMin, zMax);
        } else if (!hasZ) {
            std::printf("  (no Z channel — depth range omitted)\n");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exr_spike: %s\n", e.what());
        return 1;
    }

    return 0;
}
