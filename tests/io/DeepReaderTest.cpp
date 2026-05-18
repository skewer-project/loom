#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "core/DeepLayout.hpp"
#include "io/DeepReader.hpp"

namespace core = loom::core;
namespace io = loom::io;

namespace {

// The fixture is committed at `tests/data/deep_smoke.exr`. The build-time
// `LOOM_TESTDATA_DIR` macro carries the source-tree path so the test runs
// regardless of cwd.
std::string fixturePath(const char* filename) {
#ifdef LOOM_TESTDATA_DIR
    return std::string(LOOM_TESTDATA_DIR) + "/" + filename;
#else
    return std::string("tests/data/") + filename;
#endif
}

}  // namespace

class DeepReaderTest : public ::testing::Test {
  protected:
    void SetUp() override { core::DEBUG_clearLayoutRegistry(); }
    void TearDown() override { core::DEBUG_clearLayoutRegistry(); }
};

TEST_F(DeepReaderTest, ReadsSmokeFixtureChannelSet) {
    io::SyncDeepReader reader;
    auto frame = reader.readFrame(fixturePath("deep_smoke.exr")).get();

    ASSERT_TRUE(frame.isValid());
    EXPECT_EQ(frame.image.width, 16u);
    EXPECT_EQ(frame.image.height, 16u);

    // OpenEXR enumerates channels alphabetically; the smoke fixture writes
    // Z, ZBack, R, G, B, A → the header iterator yields A, B, G, R, Z, ZBack.
    ASSERT_EQ(frame.layout->channelCount(), 6u);
    EXPECT_NE(frame.layout->findChannel("Z"), -1);
    EXPECT_NE(frame.layout->findChannel("ZBack"), -1);
    EXPECT_NE(frame.layout->findChannel("R"), -1);
    EXPECT_NE(frame.layout->findChannel("G"), -1);
    EXPECT_NE(frame.layout->findChannel("B"), -1);
    EXPECT_NE(frame.layout->findChannel("A"), -1);

    // ZBack is FLOAT (4 bytes); B is HALF (2 bytes) — confirms the
    // pixel-type mapping survives the round trip.
    EXPECT_EQ(frame.layout->byteSize(frame.layout->findChannel("Z")), 4u);
    EXPECT_EQ(frame.layout->byteSize(frame.layout->findChannel("ZBack")), 4u);
    EXPECT_EQ(frame.layout->byteSize(frame.layout->findChannel("R")), 2u);
}

TEST_F(DeepReaderTest, SampleCountsMatchFixture) {
    io::SyncDeepReader reader;
    auto frame = reader.readFrame(fixturePath("deep_smoke.exr")).get();
    ASSERT_TRUE(frame.isValid());

    // Fixture writes `sampleCountAt(x, y) = (x + y) % 3` deterministically.
    // Reconstruct it here and verify the reader gets the same numbers back.
    uint64_t total = 0;
    for (uint32_t y = 0; y < frame.image.height; ++y) {
        for (uint32_t x = 0; x < frame.image.width; ++x) {
            uint32_t expected = (x + y) % 3;
            uint32_t actual = frame.image.sampleCounts[y * frame.image.width + x];
            EXPECT_EQ(actual, expected) << "at (" << x << ", " << y << ")";
            total += expected;
        }
    }
    EXPECT_EQ(frame.image.totalSamples(), total);
    EXPECT_EQ(total, 255u);  // matches `make_deep_fixture` output
}

TEST_F(DeepReaderTest, ZChannelDataRoundTrips) {
    io::SyncDeepReader reader;
    auto frame = reader.readFrame(fixturePath("deep_smoke.exr")).get();
    ASSERT_TRUE(frame.isValid());

    const int32_t zIdx = frame.layout->findChannel("Z");
    ASSERT_GE(zIdx, 0);
    ASSERT_EQ(frame.layout->byteSize(zIdx), sizeof(float));

    // The fixture writer stores Z = 1.0 + sampleIndex + 0.1 * x. Verify a
    // handful of samples to pin the channel-data layout end-to-end.
    const float* zSamples = reinterpret_cast<const float*>(frame.image.channelData[zIdx].data());

    uint64_t cursor = 0;
    for (uint32_t y = 0; y < frame.image.height; ++y) {
        for (uint32_t x = 0; x < frame.image.width; ++x) {
            uint32_t c = frame.image.sampleCounts[y * frame.image.width + x];
            for (uint32_t s = 0; s < c; ++s) {
                float expected = 1.0f + static_cast<float>(s) + 0.1f * static_cast<float>(x);
                EXPECT_NEAR(zSamples[cursor], expected, 1e-5f)
                    << "at (" << x << ", " << y << ") sample " << s;
                ++cursor;
            }
        }
    }
}

TEST_F(DeepReaderTest, MissingFileReturnsInvalidFrame) {
    io::SyncDeepReader reader;
    // The reader catches the throwing OpenEXR error path and returns an
    // invalid `DeepFrame`. No exception escapes.
    auto frame = reader.readFrame("/nonexistent/path/does_not_exist.exr").get();
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(frame.layout, nullptr);
}

TEST_F(DeepReaderTest, InternedLayoutMatchesAcrossReads) {
    // Reading the same file twice produces the same `DeepLayout*` — the
    // registry interns identical channel content.
    io::SyncDeepReader reader;
    auto a = reader.readFrame(fixturePath("deep_smoke.exr")).get();
    auto b = reader.readFrame(fixturePath("deep_smoke.exr")).get();
    ASSERT_TRUE(a.isValid());
    ASSERT_TRUE(b.isValid());
    EXPECT_EQ(a.layout, b.layout);
}
