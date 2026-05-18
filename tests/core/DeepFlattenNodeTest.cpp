#include <gtest/gtest.h>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Nodes.hpp"
#include "core/RenderCache.hpp"

namespace core = loom::core;

TEST(DeepFlattenNodeTest, PinSchemaHasDeepInputAndImageOutput) {
    core::Graph graph;
    auto handle = graph.addNode(core::NodeType::DeepFlatten);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->inputs.size(), 1u);
    ASSERT_EQ(node->outputs.size(), 1u);

    const core::Pin* in = graph.getPin(node->inputs[0]);
    const core::Pin* out = graph.getPin(node->outputs[0]);
    ASSERT_NE(in, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(in->type, core::PinType::DeepBuffer);
    EXPECT_EQ(in->direction, core::PinDirection::Input);
    EXPECT_EQ(out->type, core::PinType::Float);
    EXPECT_EQ(out->direction, core::PinDirection::Output);
}

TEST(DeepFlattenNodeTest, NoUpstreamProducesInvalidOutputRef) {
    // With no input wired, pullDeepInput returns an empty DeepRef; execute
    // should store an invalid output without crashing on null pools.
    core::Graph graph;
    auto handle = graph.addNode(core::NodeType::DeepFlatten);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);

    core::RenderCache cache;
    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;

    core::Region region;
    region.tiles.push_back({0, 0, 16, 16});
    node->execute(ctx, region);

    auto stored = cache.retrieve(node->outputs[0], region);
    EXPECT_EQ(stored.kind, loom::gpu::ResourceRef::Kind::None);
}

TEST(DeepFlattenNodeTest, CanWireFromDeepReadNode) {
    // canAddLink approves a DeepEXRRead → DeepFlatten wiring (matching
    // PinType::DeepBuffer on both ends).
    core::Graph graph;
    auto reader = graph.addNode(core::NodeType::DeepEXRRead);
    auto flatten = graph.addNode(core::NodeType::DeepFlatten);
    core::Node* readerNode = graph.getNode(reader);
    core::Node* flattenNode = graph.getNode(flatten);
    ASSERT_NE(readerNode, nullptr);
    ASSERT_NE(flattenNode, nullptr);
    ASSERT_FALSE(readerNode->outputs.empty());
    ASSERT_FALSE(flattenNode->inputs.empty());

    EXPECT_TRUE(graph.canAddLink(readerNode->outputs[0], flattenNode->inputs[0]));
    EXPECT_TRUE(graph.tryAddLink(readerNode->outputs[0], flattenNode->inputs[0]));
}

TEST(DeepFlattenNodeTest, RejectsWiringFromImagePin) {
    // canAddLink rejects an image-typed output → deep-typed input wiring.
    core::Graph graph;
    auto constant = graph.addNode(core::NodeType::Constant);
    auto flatten = graph.addNode(core::NodeType::DeepFlatten);
    core::Node* c = graph.getNode(constant);
    core::Node* f = graph.getNode(flatten);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(f, nullptr);

    EXPECT_FALSE(graph.canAddLink(c->outputs[0], f->inputs[0]));
}
