#include <gtest/gtest.h>

#include "core/DeepLayout.hpp"

namespace core = loom::core;

namespace {

// Common fixture channel set: the six-channel v1 deep-EXR spec from
// docs/CONVENTIONS.md §19. All scalar single-component channels (matches what
// OpenEXR's deep API hands us per-channel).
std::vector<core::DeepChannel> v1Channels() {
    return {
        {"Z", core::ChannelType::Float32, 1}, {"ZBack", core::ChannelType::Float32, 1},
        {"R", core::ChannelType::Float16, 1}, {"G", core::ChannelType::Float16, 1},
        {"B", core::ChannelType::Float16, 1}, {"A", core::ChannelType::Float16, 1},
    };
}

// NVS extension channel set: v1 + world_pos / normal / albedo. world_pos and
// normal are coalesced into vec3 entries; albedo is a vec3.
std::vector<core::DeepChannel> nvsChannels() {
    return {
        {"Z", core::ChannelType::Float32, 1},      {"R", core::ChannelType::Float16, 1},
        {"G", core::ChannelType::Float16, 1},      {"B", core::ChannelType::Float16, 1},
        {"A", core::ChannelType::Float16, 1},      {"world_pos", core::ChannelType::Float32, 3},
        {"normal", core::ChannelType::Float16, 3}, {"albedo", core::ChannelType::Float16, 3},
    };
}

class DeepLayoutFixture : public ::testing::Test {
  protected:
    void SetUp() override { core::DEBUG_clearLayoutRegistry(); }
    void TearDown() override { core::DEBUG_clearLayoutRegistry(); }
};

}  // namespace

TEST_F(DeepLayoutFixture, OffsetsAndStrideForV1Layout) {
    const core::DeepLayout* layout = core::getDeepLayout(v1Channels());
    ASSERT_NE(layout, nullptr);

    // Z (4) + ZBack (4) + R (2) + G (2) + B (2) + A (2) = 16 bytes / sample.
    EXPECT_EQ(layout->stride(), 16u);
    EXPECT_EQ(layout->channelCount(), 6u);

    EXPECT_EQ(layout->byteOffset("Z"), 0u);
    EXPECT_EQ(layout->byteOffset("ZBack"), 4u);
    EXPECT_EQ(layout->byteOffset("R"), 8u);
    EXPECT_EQ(layout->byteOffset("G"), 10u);
    EXPECT_EQ(layout->byteOffset("B"), 12u);
    EXPECT_EQ(layout->byteOffset("A"), 14u);

    // Index-based lookup matches name-based lookup.
    EXPECT_EQ(layout->findChannel("Z"), 0);
    EXPECT_EQ(layout->findChannel("A"), 5);
    EXPECT_EQ(layout->findChannel("missing"), -1);

    // Per-channel byte sizes reflect type × components.
    EXPECT_EQ(layout->byteSize(0), 4u);  // Z: Float32 × 1
    EXPECT_EQ(layout->byteSize(2), 2u);  // R: Float16 × 1
}

TEST_F(DeepLayoutFixture, OffsetsAndStrideForNVSLayout) {
    const core::DeepLayout* layout = core::getDeepLayout(nvsChannels());

    // 4 (Z) + 2*4 (RGBA) + 12 (world_pos: 3*4) + 6 (normal: 3*2) + 6 (albedo)
    EXPECT_EQ(layout->stride(), 4u + 8u + 12u + 6u + 6u);

    EXPECT_EQ(layout->byteOffset("Z"), 0u);
    EXPECT_EQ(layout->byteOffset("world_pos"), 12u);
    EXPECT_EQ(layout->byteOffset("normal"), 24u);
    EXPECT_EQ(layout->byteOffset("albedo"), 30u);
    EXPECT_EQ(layout->byteSize(layout->findChannel("world_pos")), 12u);
    EXPECT_EQ(layout->byteSize(layout->findChannel("normal")), 6u);
}

TEST_F(DeepLayoutFixture, IdenticalChannelsInternToSamePointer) {
    const core::DeepLayout* a = core::getDeepLayout(v1Channels());
    const core::DeepLayout* b = core::getDeepLayout(v1Channels());
    EXPECT_EQ(a, b);
    EXPECT_EQ(core::DEBUG_internedLayoutCount(), 1u);
}

TEST_F(DeepLayoutFixture, DifferentChannelOrderInternsDistinctly) {
    auto channels = v1Channels();
    auto swapped = channels;
    std::swap(swapped[2], swapped[3]);  // swap R and G

    const core::DeepLayout* a = core::getDeepLayout(channels);
    const core::DeepLayout* b = core::getDeepLayout(swapped);
    EXPECT_NE(a, b);
    EXPECT_NE(a->hash(), b->hash());
    EXPECT_EQ(core::DEBUG_internedLayoutCount(), 2u);
}

TEST_F(DeepLayoutFixture, HashIsStableAcrossInsertions) {
    // Build the same content twice (via two different copies of the channel
    // vector) and confirm the hash matches. This is the property the registry
    // relies on: identical channel content always hashes to the same bucket.
    auto c1 = v1Channels();
    auto c2 = v1Channels();

    const core::DeepLayout* a = core::getDeepLayout(std::move(c1));
    // a's hash now identifies the bucket; building a second layout with the
    // same content should return the same pointer (same hash, same content).
    const core::DeepLayout* b = core::getDeepLayout(std::move(c2));
    EXPECT_EQ(a, b);
    EXPECT_EQ(a->hash(), b->hash());
}

TEST_F(DeepLayoutFixture, UnknownChannelsIncludedWithoutPrejudice) {
    // The channel-naming spec (§19) says: producers may carry channels Loom
    // does not recognise; Loom logs and ignores them. At the DeepLayout layer
    // we don't have a notion of "known" vs "unknown" — every channel handed
    // in by the loader is recorded. The loader (Phase B.1) is what filters
    // for known names and logs unknowns. This test pins the contract that
    // the layout layer itself never rejects.
    auto channels = v1Channels();
    channels.push_back({"loom_future_metadata", core::ChannelType::UInt32, 1});

    const core::DeepLayout* layout = core::getDeepLayout(channels);
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->channelCount(), 7u);
    EXPECT_NE(layout->findChannel("loom_future_metadata"), -1);
    EXPECT_NE(layout->findChannel("Z"), -1);  // existing channels still resolve

    // Stride includes the unknown channel — round-trip writers in Phase D.4
    // need the unknown payload to survive read → write.
    EXPECT_EQ(layout->stride(), 16u + 4u);
}

TEST_F(DeepLayoutFixture, EmptyLayoutIsValid) {
    const core::DeepLayout* layout = core::getDeepLayout({});
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->channelCount(), 0u);
    EXPECT_EQ(layout->stride(), 0u);
    EXPECT_EQ(layout->findChannel("Z"), -1);
    EXPECT_EQ(layout->byteOffset("Z"), core::DeepLayout::kInvalidOffset);
}
