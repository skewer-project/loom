#include <gtest/gtest.h>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/RenderCache.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;

namespace {

// Forge an ImageHandle without going through the pool. These tests verify
// RenderCache bookkeeping in isolation — they do not allocate Vulkan images.
gpu::ImageHandle makeHandle(uint32_t poolIndex, uint32_t bindlessSlot, uint32_t generation) {
    gpu::ImageHandle h;
    h.poolIndex = poolIndex;
    h.bindlessSlot = bindlessSlot;
    h.generation = generation;
    return h;
}

bool handleEqual(const gpu::ImageHandle& a, const gpu::ImageHandle& b) {
    return a.poolIndex == b.poolIndex && a.bindlessSlot == b.bindlessSlot &&
           a.generation == b.generation;
}

core::Region oneTileRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    core::Region r;
    r.tiles.push_back({x, y, w, h});
    return r;
}

// Test-only Node subclass that promotes the protected pullInput accessor to
// public so we can exercise the region-threading contract without standing up
// a full Vulkan evaluation context.
struct PullInputTestNode : core::Node {
    PullInputTestNode() : Node(core::NodeHandle{}, core::NodeType::Passthrough, "PullInputTest") {}
    void markRequiredTiles(const core::Region&, std::unordered_set<core::NodeHandle>&) override {}
    void execute(core::EvaluationContext&, const core::Region&) override {}
    using core::Node::pullInput;
};

}  // namespace

TEST(RenderCacheTest, OverwriteEnqueuesPreviousHandle) {
    core::RenderCache cache;
    core::Graph graph;

    // Create a real pin so we have a valid PinHandle. We use a Passthrough
    // node (has both an input and an output pin) to obtain an output pin
    // handle.
    auto nodeH = graph.addNode(core::NodeType::Passthrough);
    auto outPin = graph.getNode(nodeH)->outputs[0];

    const core::Region emptyRegion;
    const auto first = makeHandle(/*pool=*/0, /*slot=*/100, /*gen=*/1);
    const auto second = makeHandle(/*pool=*/1, /*slot=*/101, /*gen=*/1);

    cache.store(outPin, emptyRegion, first);
    EXPECT_EQ(cache.takePendingReleases().size(), 0u);

    cache.store(outPin, emptyRegion, second);

    auto pending = cache.takePendingReleases();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_TRUE(handleEqual(pending[0], first));
}

TEST(RenderCacheTest, StoringIdenticalHandleIsIdempotent) {
    // The optimisation only enqueues a previous handle when it actually
    // differs. Storing the same handle twice should be a no-op for the
    // release queue.
    core::RenderCache cache;
    core::Graph graph;

    auto nodeH = graph.addNode(core::NodeType::Passthrough);
    auto outPin = graph.getNode(nodeH)->outputs[0];

    const core::Region emptyRegion;
    const auto h = makeHandle(/*pool=*/0, /*slot=*/100, /*gen=*/1);

    cache.store(outPin, emptyRegion, h);
    cache.store(outPin, emptyRegion, h);

    EXPECT_EQ(cache.takePendingReleases().size(), 0u);
}

TEST(RenderCacheTest, GarbageCollectDropsHandlesForDeletedPins) {
    core::RenderCache cache;
    core::Graph graph;

    auto nodeH = graph.addNode(core::NodeType::Passthrough);
    auto outPin = graph.getNode(nodeH)->outputs[0];

    const core::Region emptyRegion;
    const auto h = makeHandle(/*pool=*/0, /*slot=*/100, /*gen=*/1);
    cache.store(outPin, emptyRegion, h);
    EXPECT_EQ(cache.DEBUG_size(), 1u);

    // Remove the node — its pins become invalid.
    graph.removeNode(nodeH);

    // GC pass should drop the orphan entry and enqueue the handle for release.
    cache.garbageCollect(&graph);
    EXPECT_EQ(cache.DEBUG_size(), 0u);

    auto pending = cache.takePendingReleases();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_TRUE(handleEqual(pending[0], h));
}

TEST(RenderCacheTest, RegionMissCausesReeval) {
    // Storing under region A and probing under region B is a miss — the cache
    // returns an invalid handle, so the evaluator will produce a fresh image
    // rather than handing back a stale one.
    core::RenderCache cache;
    core::Graph graph;
    auto nodeH = graph.addNode(core::NodeType::Passthrough);
    auto outPin = graph.getNode(nodeH)->outputs[0];

    const auto regionA = oneTileRegion(0, 0, 800, 600);
    const auto regionB = oneTileRegion(0, 0, 1280, 720);
    const auto h = makeHandle(0, 100, 1);

    cache.store(outPin, regionA, h);

    auto miss = cache.retrieve(outPin, regionB);
    EXPECT_FALSE(miss.isValid());
}

TEST(RenderCacheTest, RegionHitSkipsReeval) {
    // The exact-same region produces a hit, returning the stored handle.
    core::RenderCache cache;
    core::Graph graph;
    auto nodeH = graph.addNode(core::NodeType::Passthrough);
    auto outPin = graph.getNode(nodeH)->outputs[0];

    const auto regionA = oneTileRegion(0, 0, 800, 600);
    const auto h = makeHandle(7, 200, 3);

    cache.store(outPin, regionA, h);

    auto hit = cache.retrieve(outPin, regionA);
    EXPECT_TRUE(handleEqual(hit, h));
}

TEST(RenderCacheTest, RegionCanonicalisation) {
    // Two regions with the same tile set in different insertion orders hash
    // and compare equal — the cache canonicalises on the way in, so storing
    // under one ordering retrieves under the other.
    core::RenderCache cache;
    core::Graph graph;
    auto nodeH = graph.addNode(core::NodeType::Passthrough);
    auto outPin = graph.getNode(nodeH)->outputs[0];

    core::Region storeOrder;
    storeOrder.tiles.push_back({0, 0, 64, 64});
    storeOrder.tiles.push_back({64, 0, 64, 64});
    storeOrder.tiles.push_back({0, 64, 64, 64});

    core::Region retrieveOrder;
    retrieveOrder.tiles.push_back({0, 64, 64, 64});
    retrieveOrder.tiles.push_back({0, 0, 64, 64});
    retrieveOrder.tiles.push_back({64, 0, 64, 64});

    const auto h = makeHandle(1, 50, 2);
    cache.store(outPin, storeOrder, h);

    auto hit = cache.retrieve(outPin, retrieveOrder);
    EXPECT_TRUE(handleEqual(hit, h));
}

TEST(RenderCacheTest, RegionPropagatesInPullInput) {
    // Node::pullInput must thread the requested region through to
    // RenderCache::retrieve. Same upstream pin, two regions, one stored — the
    // pullInput call at the stored region hits, the call at the other region
    // misses.
    core::RenderCache cache;
    core::Graph graph;

    auto srcH = graph.addNode(core::NodeType::Constant);
    auto sinkH = graph.addNode(core::NodeType::Passthrough);
    auto srcOut = graph.getNode(srcH)->outputs[0];
    auto sinkIn = graph.getNode(sinkH)->inputs[0];
    ASSERT_TRUE(graph.tryAddLink(srcOut, sinkIn));

    const auto regionA = oneTileRegion(0, 0, 800, 600);
    const auto regionB = oneTileRegion(0, 0, 1280, 720);
    const auto stored = makeHandle(2, 75, 4);
    cache.store(srcOut, regionA, stored);

    PullInputTestNode tester;
    tester.graph = &graph;
    tester.inputs.push_back(sinkIn);

    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;

    auto hit = tester.pullInput(ctx, regionA, 0);
    EXPECT_TRUE(handleEqual(hit, stored));

    auto miss = tester.pullInput(ctx, regionB, 0);
    EXPECT_FALSE(miss.isValid());
}

TEST(RenderCacheTest, ClearEnqueuesAllHandles) {
    core::RenderCache cache;
    core::Graph graph;

    auto nA = graph.addNode(core::NodeType::Passthrough);
    auto nB = graph.addNode(core::NodeType::Passthrough);

    const core::Region emptyRegion;
    cache.store(graph.getNode(nA)->outputs[0], emptyRegion, makeHandle(0, 100, 1));
    cache.store(graph.getNode(nB)->outputs[0], emptyRegion, makeHandle(1, 101, 1));

    cache.clear();

    EXPECT_EQ(cache.DEBUG_size(), 0u);
    EXPECT_EQ(cache.takePendingReleases().size(), 2u);
}
