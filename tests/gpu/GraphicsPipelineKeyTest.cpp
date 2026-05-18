#include <gtest/gtest.h>

#include "gpu/PipelineCache.hpp"

namespace gpu = loom::gpu;

namespace {

gpu::GraphicsPipelineKey baseKey() {
    gpu::GraphicsPipelineKey k;
    k.vertSpv = "PointCloud.vert.spv";
    k.fragSpv = "PointCloud.frag.spv";
    k.layout = reinterpret_cast<VkPipelineLayout>(uintptr_t{0xDEAD});
    k.vertexInput = gpu::VertexInputDesc::None;
    k.topology = gpu::Topology::PointList;
    k.blend = gpu::BlendMode::Opaque;
    k.depth = gpu::DepthMode::TestWrite;
    k.colorFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    k.depthFormat = VK_FORMAT_D32_SFLOAT;
    k.samples = VK_SAMPLE_COUNT_1_BIT;
    return k;
}

}  // namespace

TEST(GraphicsPipelineKeyTest, IdenticalKeysCompareEqual) { EXPECT_TRUE(baseKey() == baseKey()); }

TEST(GraphicsPipelineKeyTest, DifferingShadersAreDistinct) {
    auto a = baseKey();
    auto b = baseKey();
    b.fragSpv = "PointCloud.frag.alt.spv";
    EXPECT_FALSE(a == b);
}

TEST(GraphicsPipelineKeyTest, DifferingBlendModeAreDistinct) {
    auto a = baseKey();
    auto b = baseKey();
    b.blend = gpu::BlendMode::AlphaBlend;
    EXPECT_FALSE(a == b);
}

TEST(GraphicsPipelineKeyTest, DifferingDepthModeAreDistinct) {
    auto a = baseKey();
    auto b = baseKey();
    b.depth = gpu::DepthMode::None;
    EXPECT_FALSE(a == b);
}

TEST(GraphicsPipelineKeyTest, DifferingTopologyAreDistinct) {
    auto a = baseKey();
    auto b = baseKey();
    b.topology = gpu::Topology::TriangleList;
    EXPECT_FALSE(a == b);
}

TEST(GraphicsPipelineKeyTest, DifferingColorFormatAreDistinct) {
    auto a = baseKey();
    auto b = baseKey();
    b.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    EXPECT_FALSE(a == b);
}

TEST(GraphicsPipelineKeyTest, DifferingLayoutHandleAreDistinct) {
    auto a = baseKey();
    auto b = baseKey();
    b.layout = reinterpret_cast<VkPipelineLayout>(uintptr_t{0xBEEF});
    EXPECT_FALSE(a == b);
}

TEST(GraphicsPipelineKeyTest, HashIsStableForIdenticalKeys) {
    gpu::GraphicsPipelineKeyHash hasher;
    EXPECT_EQ(hasher(baseKey()), hasher(baseKey()));
}

TEST(GraphicsPipelineKeyTest, HashChangesOnFieldChange) {
    // Not a contract (collisions are allowed) but a useful smoke test: at
    // least one field permutation must produce a distinct hash.
    gpu::GraphicsPipelineKeyHash hasher;
    auto a = baseKey();
    auto b = baseKey();
    b.blend = gpu::BlendMode::Additive;
    EXPECT_NE(hasher(a), hasher(b));
}
