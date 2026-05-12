#include "core/DeepExrLoader.hpp"

#include <ImathBox.h>
#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfHeader.h>
#include <ImfPartType.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace loom::core {

struct Sample {
    float r, g, b, a, z;
};

DeepSampleBuffer DeepExrLoader::load(const std::string& filepath) {
    Imf::DeepScanLineInputFile file(filepath.c_str());
    const Imf::Header& header = file.header();
    const Imf::ChannelList& channels = header.channels();

    Imath::Box2i dw = header.dataWindow();
    int width = dw.max.x - dw.min.x + 1;
    int height = dw.max.y - dw.min.y + 1;

    DeepSampleBuffer result;
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.offsets.resize(width * height);
    result.counts.resize(width * height);

    // 1. Read sampleCount for every pixel
    file.readPixelSampleCounts(dw.min.y, dw.max.y);
    for (int y = dw.min.y; y <= dw.max.y; ++y) {
        int idx = (y - dw.min.y) * width;
        const unsigned int* rowCounts = file.getPixelSampleCounts(y);
        for (int x = 0; x < width; ++x) {
            result.counts[idx + x] = rowCounts[dw.min.x + x];
        }
    }

    // 2. Compute offsets via exclusive prefix sum
    result.offsets[0] = 0;
    for (size_t i = 1; i < result.counts.size(); ++i) {
        result.offsets[i] = result.offsets[i - 1] + result.counts[i - 1];
    }
    uint32_t totalSamples = result.offsets.back() + result.counts.back();

    // 3. Allocate sampleData
    result.sampleData.resize(totalSamples * 5);

    // 4. DeepFrameBuffer setup
    Imf::DeepFrameBuffer frameBuffer;
    std::vector<float*> ptrs(width * height);

    auto addChannel = [&](const char* name, int offset) {
        if (channels.findChannel(name)) {
            for (int i = 0; i < width * height; ++i) {
                ptrs[i] = result.sampleData.data() + result.offsets[i] * 5 + offset;
            }
            frameBuffer.insert(name, Imf::DeepSlice(Imf::FLOAT, (char*)ptrs.data(), sizeof(float*),
                                                    width * sizeof(float*), 5 * sizeof(float)));
        }
    };

    addChannel("R", 0);
    addChannel("G", 1);
    addChannel("B", 2);
    addChannel("A", 3);
    addChannel("Z", 4);

    file.setFrameBuffer(frameBuffer);
    file.readPixels(dw.min.y, dw.max.y);

    // Handle missing channels (A default to 1.0, Z default to 0.0)
    bool hasA = channels.findChannel("A") != nullptr;
    bool hasZ = channels.findChannel("Z") != nullptr;
    bool hasR = channels.findChannel("R") != nullptr;
    bool hasG = channels.findChannel("G") != nullptr;
    bool hasB = channels.findChannel("B") != nullptr;

    for (uint32_t i = 0; i < totalSamples; ++i) {
        if (!hasR) result.sampleData[i * 5 + 0] = 0.0f;
        if (!hasG) result.sampleData[i * 5 + 1] = 0.0f;
        if (!hasB) result.sampleData[i * 5 + 2] = 0.0f;
        if (!hasA) result.sampleData[i * 5 + 3] = 1.0f;
        if (!hasZ) result.sampleData[i * 5 + 4] = 0.0f;
    }

    // 5. Z-sort per pixel
    for (uint32_t i = 0; i < result.width * result.height; ++i) {
        Sample* begin = reinterpret_cast<Sample*>(result.sampleData.data() + result.offsets[i] * 5);
        Sample* end = begin + result.counts[i];
        std::sort(begin, end, [](const Sample& a, const Sample& b) { return a.z < b.z; });
    }

    return result;
}

}  // namespace loom::core
