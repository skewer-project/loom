#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/Graph.hpp"

namespace core = loom::core;

class TopoTest : public ::testing::Test {
  protected:
    core::Graph graph;
};

TEST_F(TopoTest, LinearSort) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Constant, "A");
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Passthrough, "B");
    core::NodeHandle nodeC = graph.addNode(core::NodeType::Viewer, "C");

    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeB)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeB)->outputs[0], graph.getNode(nodeC)->inputs[0]);

    const auto& order = graph.getTopologicalOrder();
    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], nodeA);
    EXPECT_EQ(order[1], nodeB);
    EXPECT_EQ(order[2], nodeC);
}

TEST_F(TopoTest, CycleRejection) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Passthrough, "A");
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Passthrough, "B");
    core::NodeHandle nodeC = graph.addNode(core::NodeType::Passthrough, "C");

    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeB)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeB)->outputs[0], graph.getNode(nodeC)->inputs[0]);

    // Attempt C -> A (Cycle)
    EXPECT_FALSE(
        graph.tryAddLink(graph.getNode(nodeC)->outputs[0], graph.getNode(nodeA)->inputs[0]));
    EXPECT_EQ(graph.getTopologicalOrder().size(), 3);
}

TEST_F(TopoTest, ComplexRejection) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Constant, "A");
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Passthrough, "B");
    core::NodeHandle nodeC = graph.addNode(core::NodeType::Passthrough, "C");
    core::NodeHandle nodeD = graph.addNode(core::NodeType::Merge, "D");

    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeB)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeC)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeB)->outputs[0], graph.getNode(nodeD)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeC)->outputs[0], graph.getNode(nodeD)->inputs[1]);

    // Attempt D -> B (Cycle)
    EXPECT_FALSE(
        graph.tryAddLink(graph.getNode(nodeD)->outputs[0], graph.getNode(nodeB)->inputs[0]));

    const auto& order = graph.getTopologicalOrder();
    ASSERT_EQ(order.size(), 4);
    EXPECT_EQ(order[0], nodeA);

    auto itB = std::find(order.begin(), order.end(), nodeB);
    auto itC = std::find(order.begin(), order.end(), nodeC);
    auto itD = std::find(order.begin(), order.end(), nodeD);

    EXPECT_LT(itB, itD);
    EXPECT_LT(itC, itD);
}

TEST_F(TopoTest, SelfLoopRejection) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Passthrough, "A");
    EXPECT_FALSE(
        graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeA)->inputs[0]));
}

TEST_F(TopoTest, MassiveGraph) {
    const int COUNT = 1000;
    std::vector<core::NodeHandle> nodeHandles;
    for (int i = 0; i < COUNT; ++i) {
        nodeHandles.push_back(graph.addNode(core::NodeType::Passthrough));
    }

    for (int i = 0; i < COUNT - 1; ++i) {
        auto* src = graph.getNode(nodeHandles[i]);
        auto* dst = graph.getNode(nodeHandles[i + 1]);
        ASSERT_TRUE(graph.tryAddLink(src->outputs[0], dst->inputs[0]));
    }

    const auto& order = graph.getTopologicalOrder();
    ASSERT_EQ(order.size(), COUNT);
    for (int i = 0; i < COUNT; ++i) {
        EXPECT_EQ(order[i], nodeHandles[i]);
    }

    auto* last = graph.getNode(nodeHandles[COUNT - 1]);
    auto* first = graph.getNode(nodeHandles[0]);
    EXPECT_FALSE(graph.tryAddLink(last->outputs[0], first->inputs[0]));
}
