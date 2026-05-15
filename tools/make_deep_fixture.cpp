// Phase A.0 — throwaway fixture generator.
//
// Writes a tiny deterministic deep EXR to tests/data/deep_smoke.exr containing
// the v1 channel set documented in docs/CONVENTIONS.md §19:
//
//   Z, ZBack — front / back depth
//   R, G, B, A — premultiplied radiance / alpha
//
// The image is 16x16 with a per-pixel sample count between 0 and 2 driven by
// a stable function of (x, y) so the file is byte-identical across runs.
// Coordinate convention: right-handed, Y-up, meters (see §19).
//
// Run once to regenerate the committed fixture. Not part of the shipping
// build; the Phase B IDeepReader / IDeepWriter wrappers replace this.

#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineOutputFile.h>
#include <ImfHeader.h>
#include <ImfPartType.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 16;
constexpr int kHeight = 16;

// Stable, position-derived sample count in [0, 2]. The pattern is irrelevant
// — what matters is that the file has variable-per-pixel sample counts (the
// defining feature of a deep image) and that regeneration is deterministic.
uint32_t sampleCountAt(int x, int y) { return static_cast<uint32_t>((x + y) % 3); }

}  // namespace

int main(int argc, char** argv) {
    std::string outPath;
    if (argc >= 2) {
        outPath = argv[1];
    } else {
#ifdef LOOM_FIXTURE_DIR
        outPath = std::string(LOOM_FIXTURE_DIR) + "/deep_smoke.exr";
#else
        outPath = "deep_smoke.exr";
#endif
    }

    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());

    using namespace Imf;
    using namespace Imath;

    // Header advertises the six v1 channels. All half-float to keep the
    // committed fixture small; the channel-naming spec does not constrain
    // numeric type — DeepLayout (A.1) records the per-channel type from the
    // header.
    Header header(kWidth, kHeight);
    header.setType(DEEPSCANLINE);
    header.compression() = ZIPS_COMPRESSION;
    for (const char* name : {"Z", "ZBack"}) header.channels().insert(name, Channel(FLOAT));
    for (const char* name : {"R", "G", "B", "A"}) header.channels().insert(name, Channel(HALF));

    // Flatten per-pixel sample storage. OpenEXR's deep API hands the writer a
    // pointer-per-pixel grid; the pointers index into our flat per-channel
    // arrays.
    std::vector<uint32_t> counts(static_cast<size_t>(kWidth) * kHeight, 0);
    uint64_t totalSamples = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            uint32_t c = sampleCountAt(x, y);
            counts[static_cast<size_t>(y) * kWidth + x] = c;
            totalSamples += c;
        }
    }

    std::vector<float> zStore(totalSamples), zBackStore(totalSamples);
    std::vector<half> rStore(totalSamples), gStore(totalSamples), bStore(totalSamples),
        aStore(totalSamples);

    std::vector<float*> zPtrs(static_cast<size_t>(kWidth) * kHeight, nullptr);
    std::vector<float*> zBackPtrs(static_cast<size_t>(kWidth) * kHeight, nullptr);
    std::vector<half*> rPtrs(static_cast<size_t>(kWidth) * kHeight, nullptr);
    std::vector<half*> gPtrs(static_cast<size_t>(kWidth) * kHeight, nullptr);
    std::vector<half*> bPtrs(static_cast<size_t>(kWidth) * kHeight, nullptr);
    std::vector<half*> aPtrs(static_cast<size_t>(kWidth) * kHeight, nullptr);

    {
        uint64_t cursor = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                size_t pi = static_cast<size_t>(y) * kWidth + x;
                uint32_t c = counts[pi];
                if (c == 0) continue;
                zPtrs[pi] = &zStore[cursor];
                zBackPtrs[pi] = &zBackStore[cursor];
                rPtrs[pi] = &rStore[cursor];
                gPtrs[pi] = &gStore[cursor];
                bPtrs[pi] = &bStore[cursor];
                aPtrs[pi] = &aStore[cursor];
                for (uint32_t s = 0; s < c; ++s) {
                    float zFront = 1.0f + static_cast<float>(s) + 0.1f * static_cast<float>(x);
                    zStore[cursor + s] = zFront;
                    zBackStore[cursor + s] = zFront + 0.5f;
                    rStore[cursor + s] = half(0.25f * static_cast<float>(x % 4));
                    gStore[cursor + s] = half(0.25f * static_cast<float>(y % 4));
                    bStore[cursor + s] = half(0.5f);
                    aStore[cursor + s] = half(0.5f);
                }
                cursor += c;
            }
        }
    }

    DeepFrameBuffer fb;
    const int xStride = sizeof(uint32_t);
    const int yStride = sizeof(uint32_t) * kWidth;

    fb.insertSampleCountSlice(
        Slice(UINT, reinterpret_cast<char*>(counts.data()), xStride, yStride));

    auto addFloatSlice = [&](const char* name, std::vector<float*>& ptrs) {
        fb.insert(name, DeepSlice(FLOAT, reinterpret_cast<char*>(ptrs.data()), sizeof(float*),
                                  sizeof(float*) * kWidth, sizeof(float)));
    };
    auto addHalfSlice = [&](const char* name, std::vector<half*>& ptrs) {
        fb.insert(name, DeepSlice(HALF, reinterpret_cast<char*>(ptrs.data()), sizeof(half*),
                                  sizeof(half*) * kWidth, sizeof(half)));
    };

    addFloatSlice("Z", zPtrs);
    addFloatSlice("ZBack", zBackPtrs);
    addHalfSlice("R", rPtrs);
    addHalfSlice("G", gPtrs);
    addHalfSlice("B", bPtrs);
    addHalfSlice("A", aPtrs);

    DeepScanLineOutputFile file(outPath.c_str(), header);
    file.setFrameBuffer(fb);
    file.writePixels(kHeight);

    std::printf("wrote %s (%dx%d, %llu samples)\n", outPath.c_str(), kWidth, kHeight,
                static_cast<unsigned long long>(totalSamples));
    return 0;
}
