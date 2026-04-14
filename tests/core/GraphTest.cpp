#include <gtest/gtest.h>

#include "core/Graph.hpp"

namespace core = loom::core;

class GraphTest : public ::testing::Test {
  protected:
    core::Graph graph;
};

TEST_F(GraphTest, NodeCreation) {
    core::NodeHandle h = graph.addNode(core::NodeType::Merge);
    core::Node* node = graph.getNode(h);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, core::NodeType::Merge);
    EXPECT_EQ(node->inputs.size(), 2);
    EXPECT_EQ(node->outputs.size(), 1);

    for (auto ph : node->inputs) EXPECT_NE(graph.getPin(ph), nullptr);
    for (auto ph : node->outputs) EXPECT_NE(graph.getPin(ph), nullptr);
}

TEST_F(GraphTest, LinkingNodes) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Viewer);

    core::Node* a = graph.getNode(nodeA);
    core::Node* b = graph.getNode(nodeB);

    ASSERT_FALSE(a->outputs.empty());
    ASSERT_FALSE(b->inputs.empty());

    core::PinHandle outA = a->outputs[0];
    core::PinHandle inB = b->inputs[0];

    EXPECT_TRUE(graph.tryAddLink(outA, inB));

    core::Pin* pOutA = graph.getPin(outA);
    core::Pin* pInB = graph.getPin(inB);

    EXPECT_TRUE(pInB->link.isValid());
    ASSERT_EQ(pOutA->links.size(), 1);
    EXPECT_EQ(pOutA->links[0], pInB->link);
}

TEST_F(GraphTest, TypeSafety) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Viewer);

    core::PinHandle outA = graph.getNode(nodeA)->outputs[0];
    core::PinHandle inB = graph.getNode(nodeB)->inputs[0];

    // Force type mismatch
    graph.getPin(inB)->type = core::PinType::DeepBuffer;

    EXPECT_FALSE(graph.tryAddLink(outA, inB));
}

TEST_F(GraphTest, CascadingDeletion) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Viewer);

    core::PinHandle outA = graph.getNode(nodeA)->outputs[0];
    core::PinHandle inB = graph.getNode(nodeB)->inputs[0];

    graph.tryAddLink(outA, inB);
    core::LinkHandle lh = graph.getPin(inB)->link;

    ASSERT_NE(graph.getLink(lh), nullptr);

    graph.removeNode(nodeA);

    EXPECT_EQ(graph.getNode(nodeA), nullptr);
    EXPECT_EQ(graph.getPin(outA), nullptr);
    EXPECT_EQ(graph.getLink(lh), nullptr);
    EXPECT_NE(graph.getPin(inB), nullptr);
    EXPECT_FALSE(graph.getPin(inB)->link.isValid());
}
