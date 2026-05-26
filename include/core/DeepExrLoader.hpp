#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace loom::core {

struct DeepSampleBuffer {
    uint32_t width, height;
    std::vector<uint32_t> offsets;  // exclusive prefix sum, 1 per pixel
    std::vector<uint32_t> counts;   // sample count per pixel
    // Packed as [R, G, B, A, Z] per sample, std430-compatible (5 floats = 20 bytes)
    std::vector<float> sampleData;
};

class DeepExrLoader {
  public:
    static DeepSampleBuffer load(const std::string& filepath);
};

}  // namespace loom::core
