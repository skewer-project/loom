#include <gtest/gtest.h>

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
