#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Nodes.hpp"
#include "core/RenderCache.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;

namespace {

core::NodeHandle addCamera(core::Graph& graph) { return graph.addNode(core::NodeType::Camera); }

}  // namespace

TEST(CameraNodeTest, PinSchemaIsSingleCameraOutput) {
    core::Graph graph;
    auto handle = addCamera(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->inputs.empty());
    ASSERT_EQ(node->outputs.size(), 1u);
    const core::Pin* outPin = graph.getPin(node->outputs[0]);
    ASSERT_NE(outPin, nullptr);
    EXPECT_EQ(outPin->type, core::PinType::Camera);
    EXPECT_EQ(outPin->direction, core::PinDirection::Output);
}

TEST(CameraNodeTest, BuildParamsHasFiveKnobs) {
    core::Graph graph;
    auto handle = addCamera(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->params.size(), 5u);

    EXPECT_EQ(node->params[0].name(), "position");
    EXPECT_TRUE(std::holds_alternative<glm::vec3>(node->params[0].value()));

    EXPECT_EQ(node->params[1].name(), "target");
    EXPECT_TRUE(std::holds_alternative<glm::vec3>(node->params[1].value()));

    EXPECT_EQ(node->params[2].name(), "fov_y_deg");
    EXPECT_TRUE(std::holds_alternative<float>(node->params[2].value()));
    EXPECT_FLOAT_EQ(std::get<float>(node->params[2].value()), 60.0f);
    EXPECT_TRUE(node->params[2].range().hasBounds);

    EXPECT_EQ(node->params[3].name(), "near");
    EXPECT_TRUE(std::holds_alternative<float>(node->params[3].value()));

    EXPECT_EQ(node->params[4].name(), "far");
    EXPECT_TRUE(std::holds_alternative<float>(node->params[4].value()));
}

TEST(CameraNodeTest, ExecuteStoresCameraRefAtDefaultValues) {
    core::Graph graph;
    auto handle = addCamera(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);

    core::RenderCache cache;
    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;
    ctx.requestedExtent = {1920, 1080};  // 16:9

    core::Region region;
    region.tiles.push_back({0, 0, 1920, 1080});

    node->execute(ctx, region);

    gpu::ResourceRef stored = cache.retrieve(node->outputs[0], region);
    ASSERT_EQ(stored.kind, gpu::ResourceRef::Kind::Camera);
    EXPECT_FLOAT_EQ(stored.camera.eyePos.x, 0.0f);
    EXPECT_FLOAT_EQ(stored.camera.eyePos.y, 0.0f);
    EXPECT_FLOAT_EQ(stored.camera.eyePos.z, 3.0f);
    EXPECT_FLOAT_EQ(stored.camera.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(stored.camera.farPlane, 100.0f);
    // FOV stored in radians; default knob is 60° → π/3.
    EXPECT_NEAR(stored.camera.fovY, glm::radians(60.0f), 1e-5f);
}

TEST(CameraNodeTest, EditingKnobMutatesStoredCamera) {
    core::Graph graph;
    auto handle = addCamera(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);

    node->setParam(0, glm::vec3(10.0f, -5.0f, -13.0f));  // position
    node->setParam(2, 90.0f);                            // fov_y_deg
    node->setParam(3, 0.5f);                             // near
    node->setParam(4, 500.0f);                           // far

    core::RenderCache cache;
    core::EvaluationContext ctx{};
    ctx.renderCache = &cache;
    ctx.requestedExtent = {800, 600};

    core::Region region;
    region.tiles.push_back({0, 0, 800, 600});

    node->execute(ctx, region);

    gpu::ResourceRef stored = cache.retrieve(node->outputs[0], region);
    ASSERT_EQ(stored.kind, gpu::ResourceRef::Kind::Camera);
    EXPECT_FLOAT_EQ(stored.camera.eyePos.x, 10.0f);
    EXPECT_FLOAT_EQ(stored.camera.eyePos.y, -5.0f);
    EXPECT_FLOAT_EQ(stored.camera.eyePos.z, -13.0f);
    EXPECT_FLOAT_EQ(stored.camera.nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(stored.camera.farPlane, 500.0f);
    EXPECT_NEAR(stored.camera.fovY, glm::radians(90.0f), 1e-5f);
}

TEST(CameraNodeTest, ParamsRoundTripThroughJson) {
    core::Graph graph;
    auto handle = addCamera(graph);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);

    node->setParam(0, glm::vec3(7.0f, 8.0f, 9.0f));
    node->setParam(2, 42.0f);

    crude_json::value json = node->paramsToJson();

    // Spawn a fresh node and restore from the JSON.
    auto handle2 = addCamera(graph);
    core::Node* node2 = graph.getNode(handle2);
    ASSERT_NE(node2, nullptr);
    node2->paramsFromJson(json);

    EXPECT_EQ(std::get<glm::vec3>(node2->params[0].value()), glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_FLOAT_EQ(std::get<float>(node2->params[2].value()), 42.0f);
}
