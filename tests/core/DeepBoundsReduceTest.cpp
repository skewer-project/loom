#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <vector>

#include "core/DeepLayout.hpp"
#include "gpu/DeepUpload.hpp"
#include "io/ParsedDeepImage.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;
namespace io = loom::io;

namespace {

// Build a non-NVS deep payload: one channel `Z`, Float32, fixed sample
// count per pixel. Z values pulled from `zValues` in row-major order.
io::ParsedDeepImage makeZOnlyPayload(uint32_t w, uint32_t h, std::vector<float> zValues) {
    io::ParsedDeepImage img;
    img.width = w;
    img.height = h;
    // One sample per pixel keeps the row-major iteration explicit and
    // means `zValues[i]` is the depth at pixel `i`.
    img.sampleCounts.assign(static_cast<size_t>(w) * h, 1);
    std::vector<uint8_t> zBytes(zValues.size() * sizeof(float));
    std::memcpy(zBytes.data(), zValues.data(), zBytes.size());
    img.channelData.push_back(std::move(zBytes));
    return img;
}

// Build an NVS deep payload: channels alphabetically sorted
// `world_pos.x` / `world_pos.y` / `world_pos.z` / `Z`.
// `worldPos` is row-major, one (x,y,z) per sample; `zValues` is the
// matching front-depth.
io::ParsedDeepImage makeNVSPayload(uint32_t w, uint32_t h, std::vector<float> wpx,
                                   std::vector<float> wpy, std::vector<float> wpz,
                                   std::vector<float> zValues) {
    io::ParsedDeepImage img;
    img.width = w;
    img.height = h;
    img.sampleCounts.assign(static_cast<size_t>(w) * h, 1);
    auto pack = [](const std::vector<float>& src) {
        std::vector<uint8_t> bytes(src.size() * sizeof(float));
        std::memcpy(bytes.data(), src.data(), bytes.size());
        return bytes;
    };
    // Channel order matches the alphabetical layout below.
    img.channelData.push_back(pack(zValues));  // "Z"
    img.channelData.push_back(pack(wpx));      // "world_pos.x"
    img.channelData.push_back(pack(wpy));      // "world_pos.y"
    img.channelData.push_back(pack(wpz));      // "world_pos.z"
    return img;
}

const core::DeepLayout* zOnlyLayout() {
    return core::getDeepLayout({{"Z", core::ChannelType::Float32, 1}});
}

const core::DeepLayout* nvsLayout() {
    // Alphabetical order matches what `DeepReader` builds from an EXR
    // header (OpenEXR sorts channels by name on insertion).
    return core::getDeepLayout({
        {"Z", core::ChannelType::Float32, 1},
        {"world_pos.x", core::ChannelType::Float32, 1},
        {"world_pos.y", core::ChannelType::Float32, 1},
        {"world_pos.z", core::ChannelType::Float32, 1},
    });
}

}  // namespace

class DeepBoundsReduceTest : public ::testing::Test {
  protected:
    void SetUp() override { core::DEBUG_clearLayoutRegistry(); }
    void TearDown() override { core::DEBUG_clearLayoutRegistry(); }
};

TEST_F(DeepBoundsReduceTest, EmptyPayloadIsInvalid) {
    io::ParsedDeepImage img;
    img.width = 0;
    img.height = 0;
    const core::DeepLayout* layout = zOnlyLayout();
    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    EXPECT_FALSE(box.valid);
}

TEST_F(DeepBoundsReduceTest, ZOnlyPayloadProducesSynthesisedBounds) {
    // 2x2 image, one sample per pixel, all at the same front depth.
    // Synthesised XY ∈ [-0.5, 0.5] (centered per-pixel), Z = -depth.
    auto img = makeZOnlyPayload(2, 2, {1.0f, 1.0f, 1.0f, 1.0f});
    const core::DeepLayout* layout = zOnlyLayout();

    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    ASSERT_TRUE(box.valid);

    EXPECT_NEAR(box.min.x, -0.5f, 1e-5f);
    EXPECT_NEAR(box.max.x, 0.5f, 1e-5f);
    EXPECT_NEAR(box.min.y, -0.5f, 1e-5f);
    EXPECT_NEAR(box.max.y, 0.5f, 1e-5f);
    EXPECT_NEAR(box.min.z, -1.0f, 1e-5f);
    EXPECT_NEAR(box.max.z, -1.0f, 1e-5f);
}

TEST_F(DeepBoundsReduceTest, BackgroundSentinelIsFiltered) {
    // Three pixels at depth 5, one at the 1e10 background sentinel. The
    // sentinel must not influence the Z range — bounds.max.z should be
    // pinned to the foreground value (which becomes -5 after the RH
    // negation), not the sentinel.
    auto img = makeZOnlyPayload(2, 2, {5.0f, 5.0f, 5.0f, 1.0e10f});
    const core::DeepLayout* layout = zOnlyLayout();

    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    ASSERT_TRUE(box.valid);
    EXPECT_NEAR(box.min.z, -5.0f, 1e-3f);
    EXPECT_NEAR(box.max.z, -5.0f, 1e-3f);
}

TEST_F(DeepBoundsReduceTest, NonFiniteSamplesAreFiltered) {
    auto img = makeZOnlyPayload(2, 2,
                                {3.0f, std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::quiet_NaN(), 7.0f});
    const core::DeepLayout* layout = zOnlyLayout();

    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    ASSERT_TRUE(box.valid);
    EXPECT_NEAR(box.min.z, -7.0f, 1e-5f);
    EXPECT_NEAR(box.max.z, -3.0f, 1e-5f);
}

TEST_F(DeepBoundsReduceTest, AllSentinelsLeavesBoundsInvalid) {
    // A file containing nothing but background samples → no usable data.
    auto img = makeZOnlyPayload(2, 2, {1.0e10f, 1.0e10f, 1.0e10f, 1.0e10f});
    const core::DeepLayout* layout = zOnlyLayout();

    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    EXPECT_FALSE(box.valid);
}

TEST_F(DeepBoundsReduceTest, NVSPathReadsWorldPosDirectly) {
    // 2x1 image with two samples placed at known world positions far from
    // the origin — proves the NVS path bypasses the height-field synthesis
    // and surfaces the file's real geometry to the auto-frame.
    auto img = makeNVSPayload(/*w=*/2, /*h=*/1,
                              /*wpx=*/{10.0f, 14.0f},
                              /*wpy=*/{-5.0f, -3.0f},
                              /*wpz=*/{-13.0f, -11.0f},
                              /*Z=*/{13.0f, 11.0f});
    const core::DeepLayout* layout = nvsLayout();

    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    ASSERT_TRUE(box.valid);
    EXPECT_NEAR(box.min.x, 10.0f, 1e-5f);
    EXPECT_NEAR(box.max.x, 14.0f, 1e-5f);
    EXPECT_NEAR(box.min.y, -5.0f, 1e-5f);
    EXPECT_NEAR(box.max.y, -3.0f, 1e-5f);
    EXPECT_NEAR(box.min.z, -13.0f, 1e-5f);
    EXPECT_NEAR(box.max.z, -11.0f, 1e-5f);
}

TEST_F(DeepBoundsReduceTest, NVSPathFiltersSentinelOnAnyAxis) {
    // Producer emits the sentinel on a single axis (not all three) for a
    // "no hit" sample. The filter still discards.
    auto img = makeNVSPayload(/*w=*/2, /*h=*/1,
                              /*wpx=*/{1.0f, 1.0e10f},
                              /*wpy=*/{2.0f, 2.0f},
                              /*wpz=*/{3.0f, 3.0f},
                              /*Z=*/{1.0f, 1.0f});
    const core::DeepLayout* layout = nvsLayout();

    core::AABB box = gpu::reduceSceneBounds(img, *layout);
    ASSERT_TRUE(box.valid);
    EXPECT_NEAR(box.min.x, 1.0f, 1e-5f);
    EXPECT_NEAR(box.max.x, 1.0f, 1e-5f);
}
