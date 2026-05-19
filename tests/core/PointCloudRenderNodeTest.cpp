#include <gtest/gtest.h>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Nodes.hpp"
#include "core/RenderCache.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;

namespace {

core::NodeHandle addPCR(core::Graph& graph) {
    return graph.addNode(core::NodeType::PointCloudRender);
}

}  // namespace

TEST(PointCloudRenderNodeTest, PinSchemaHasDeepAndCameraInputsImageOutput) {
    core::Graph graph;
    auto handle = addPCR(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->inputs.size(), 2u);
    ASSERT_EQ(node->outputs.size(), 1u);

    const core::Pin* deepPin = graph.getPin(node->inputs[0]);
    const core::Pin* camPin = graph.getPin(node->inputs[1]);
    const core::Pin* outPin = graph.getPin(node->outputs[0]);
    ASSERT_NE(deepPin, nullptr);
    ASSERT_NE(camPin, nullptr);
    ASSERT_NE(outPin, nullptr);

    EXPECT_EQ(deepPin->type, core::PinType::DeepBuffer);
    EXPECT_EQ(deepPin->direction, core::PinDirection::Input);
    EXPECT_EQ(camPin->type, core::PinType::Camera);
    EXPECT_EQ(camPin->direction, core::PinDirection::Input);
    EXPECT_EQ(outPin->type, core::PinType::Float);
    EXPECT_EQ(outPin->direction, core::PinDirection::Output);
}

TEST(PointCloudRenderNodeTest, BuildParamsHasZScaleAndPointSize) {
    core::Graph graph;
    auto handle = addPCR(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->params.size(), 2u);

    EXPECT_EQ(node->params[0].name(), "z_scale");
    EXPECT_FLOAT_EQ(std::get<float>(node->params[0].value()), 1.0f);
    EXPECT_TRUE(node->params[0].range().hasBounds);
    EXPECT_FLOAT_EQ(node->params[0].range().min, 0.01f);
    EXPECT_FLOAT_EQ(node->params[0].range().max, 10.0f);

    EXPECT_EQ(node->params[1].name(), "point_size");
    EXPECT_FLOAT_EQ(std::get<float>(node->params[1].value()), 2.0f);
    EXPECT_TRUE(node->params[1].range().hasBounds);
}

TEST(PointCloudRenderNodeTest, ExecuteWithMissingInputsStoresInvalidImage) {
    // No upstream links, no engine pass — node should store an invalid
    // image without crashing or attempting GPU work. Pin this behaviour:
    // it's what downstream nodes (`ViewerNode`) rely on when the graph
    // is half-wired during construction.
    core::Graph graph;
    auto handle = addPCR(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);

    core::RenderCache cache;
    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;
    ctx.requestedExtent = {640, 480};

    core::Region region;
    region.tiles.push_back({0, 0, 640, 480});

    node->execute(ctx, region);

    gpu::ResourceRef stored = cache.retrieve(node->outputs[0], region);
    EXPECT_EQ(stored.kind, gpu::ResourceRef::Kind::Image);
    EXPECT_FALSE(stored.image.isValid());
}
