#include <gtest/gtest.h>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Nodes.hpp"
#include "core/RenderCache.hpp"

namespace core = loom::core;

namespace {

core::NodeHandle addDeepReader(core::Graph& graph) {
    return graph.addNode(core::NodeType::DeepEXRRead);
}

}  // namespace

TEST(DeepEXRReadNodeTest, PinSchemaHasSingleDeepOutput) {
    core::Graph graph;
    auto handle = addDeepReader(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->inputs.empty());
    ASSERT_EQ(node->outputs.size(), 1u);
    const core::Pin* outPin = graph.getPin(node->outputs[0]);
    ASSERT_NE(outPin, nullptr);
    EXPECT_EQ(outPin->type, core::PinType::DeepBuffer);
    EXPECT_EQ(outPin->direction, core::PinDirection::Output);
}

TEST(DeepEXRReadNodeTest, BuildParamsHasFilePathAndFrameIndex) {
    core::Graph graph;
    auto handle = addDeepReader(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->params.size(), 2u);

    EXPECT_EQ(node->params[0].name(), "file_path");
    EXPECT_TRUE(std::holds_alternative<std::string>(node->params[0].value()));
    EXPECT_EQ(std::get<std::string>(node->params[0].value()), "");

    EXPECT_EQ(node->params[1].name(), "frame_index");
    EXPECT_TRUE(std::holds_alternative<int>(node->params[1].value()));
    EXPECT_EQ(std::get<int>(node->params[1].value()), 0);
    EXPECT_TRUE(node->params[1].range().hasBounds);
}

TEST(DeepEXRReadNodeTest, ExecuteWithEmptyPathStoresInvalidDeepRef) {
    // The execute path is normally GPU-bound, but the empty-path early-return
    // before any pool access is reachable headless. Pin that behaviour so a
    // freshly-spawned node before the user types a path produces a clean
    // invalid downstream payload (no crash, no allocation).
    core::Graph graph;
    auto handle = addDeepReader(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);

    core::RenderCache cache;
    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;
    // imagePool / bufferPool / stagingArena / deepReader intentionally null
    // — the empty-path branch never dereferences them.

    core::Region region;
    region.tiles.push_back({0, 0, 16, 16});

    node->execute(ctx, region);

    auto stored = cache.retrieve(node->outputs[0], region);
    EXPECT_EQ(stored.kind, loom::gpu::ResourceRef::Kind::Deep);
    EXPECT_FALSE(stored.deep.countImage.isValid());
    EXPECT_FALSE(stored.deep.offsetImage.isValid());
    EXPECT_FALSE(stored.deep.samples.isValid());
    EXPECT_EQ(stored.deep.layout, nullptr);
}

TEST(DeepEXRReadNodeTest, ExecuteWithPathButMissingContextStoresInvalidDeepRef) {
    // A path is set but the eval context lacks the required GPU pools / reader.
    // Should not crash; should not produce a valid deep ref.
    core::Graph graph;
    auto handle = addDeepReader(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    node->setParam(0, std::string("/some/path/does_not_exist.exr"));

    core::RenderCache cache;
    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;

    core::Region region;
    region.tiles.push_back({0, 0, 16, 16});

    node->execute(ctx, region);

    auto stored = cache.retrieve(node->outputs[0], region);
    EXPECT_EQ(stored.kind, loom::gpu::ResourceRef::Kind::Deep);
    EXPECT_FALSE(stored.deep.samples.isValid());
}
