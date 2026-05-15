#include <gtest/gtest.h>

#include <glm/vec3.hpp>

#include "core/Graph.hpp"
#include "core/Param.hpp"

namespace core = loom::core;

namespace {

constexpr float kEps = 1e-5f;

}  // namespace

TEST(ParamTest, RoundTripFloat) {
    core::Param p("gain", 1.25f);
    auto json = p.toJson();
    auto back = core::Param::fromJson(json);

    EXPECT_EQ(back.name(), "gain");
    ASSERT_TRUE(std::holds_alternative<float>(back.value()));
    EXPECT_FLOAT_EQ(std::get<float>(back.value()), 1.25f);
}

TEST(ParamTest, RoundTripInt) {
    core::Param p("count", 42);
    auto back = core::Param::fromJson(p.toJson());

    ASSERT_TRUE(std::holds_alternative<int>(back.value()));
    EXPECT_EQ(std::get<int>(back.value()), 42);
}

TEST(ParamTest, RoundTripBool) {
    core::Param p("enabled", true);
    auto back = core::Param::fromJson(p.toJson());

    ASSERT_TRUE(std::holds_alternative<bool>(back.value()));
    EXPECT_TRUE(std::get<bool>(back.value()));
}

TEST(ParamTest, RoundTripVec3) {
    core::Param p("color", glm::vec3(0.5f, 0.25f, 0.125f));
    auto back = core::Param::fromJson(p.toJson());

    ASSERT_TRUE(std::holds_alternative<glm::vec3>(back.value()));
    glm::vec3 v = std::get<glm::vec3>(back.value());
    EXPECT_NEAR(v.x, 0.5f, kEps);
    EXPECT_NEAR(v.y, 0.25f, kEps);
    EXPECT_NEAR(v.z, 0.125f, kEps);
}

TEST(ParamTest, RoundTripString) {
    core::Param p("path", std::string("frame_####.exr"));
    auto back = core::Param::fromJson(p.toJson());

    ASSERT_TRUE(std::holds_alternative<std::string>(back.value()));
    EXPECT_EQ(std::get<std::string>(back.value()), "frame_####.exr");
}

TEST(ParamTest, RangeRoundTrip) {
    core::ParamRange r;
    r.min = 0.0f;
    r.max = 1.0f;
    r.step = 0.01f;
    r.hasBounds = true;

    core::Param p("amount", 0.5f, r);
    auto back = core::Param::fromJson(p.toJson());

    EXPECT_TRUE(back.range().hasBounds);
    EXPECT_FLOAT_EQ(back.range().min, 0.0f);
    EXPECT_FLOAT_EQ(back.range().max, 1.0f);
    EXPECT_FLOAT_EQ(back.range().step, 0.01f);
}

TEST(ParamTest, SetParamFlipsNodeDirty) {
    core::Graph graph;
    auto handle = graph.addNode(core::NodeType::Constant);
    core::Node* node = graph.getNode(handle);
    ASSERT_NE(node, nullptr);
    ASSERT_FALSE(node->params.empty());

    // Force-clear dirty so we can observe the flip on edit.
    node->isDirty = false;

    node->setParam(0, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(node->isDirty);

    auto& v = node->params[0].value();
    ASSERT_TRUE(std::holds_alternative<glm::vec3>(v));
    EXPECT_FLOAT_EQ(std::get<glm::vec3>(v).g, 1.0f);
}

TEST(ParamTest, NodePopulatesParamsViaBuildParams) {
    // The graph-side hook calls buildParams immediately after pin setup.
    // ConstantNode declares one `color` vec3; MergeNode declares one
    // `mergeColor` vec3; Viewer / Passthrough declare none.
    core::Graph graph;

    auto constant = graph.addNode(core::NodeType::Constant);
    auto merge = graph.addNode(core::NodeType::Merge);
    auto viewer = graph.addNode(core::NodeType::Viewer);
    auto pass = graph.addNode(core::NodeType::Passthrough);

    EXPECT_EQ(graph.getNode(constant)->params.size(), 1u);
    EXPECT_EQ(graph.getNode(constant)->params[0].name(), "color");
    EXPECT_EQ(graph.getNode(merge)->params.size(), 1u);
    EXPECT_EQ(graph.getNode(merge)->params[0].name(), "mergeColor");
    EXPECT_TRUE(graph.getNode(viewer)->params.empty());
    EXPECT_TRUE(graph.getNode(pass)->params.empty());
}

TEST(ParamTest, NodeParamsRoundTripThroughJson) {
    core::Graph graph;
    auto handle = graph.addNode(core::NodeType::Constant);
    core::Node* node = graph.getNode(handle);
    node->setParam(0, glm::vec3(0.7f, 0.8f, 0.9f));

    auto json = node->paramsToJson();

    // Mutate the param to something else, then restore from json.
    node->setParam(0, glm::vec3(0.0f, 0.0f, 0.0f));
    node->paramsFromJson(json);

    auto v = std::get<glm::vec3>(node->params[0].value());
    EXPECT_NEAR(v.r, 0.7f, kEps);
    EXPECT_NEAR(v.g, 0.8f, kEps);
    EXPECT_NEAR(v.b, 0.9f, kEps);
}

TEST(ParamTest, FromJsonOnMalformedReturnsDefault) {
    // A non-object input yields a default-constructed Param without
    // throwing.
    crude_json::value bad(crude_json::type_t::null);
    core::Param p = core::Param::fromJson(bad);
    EXPECT_TRUE(p.name().empty());
    ASSERT_TRUE(std::holds_alternative<float>(p.value()));
    EXPECT_FLOAT_EQ(std::get<float>(p.value()), 0.0f);
}
