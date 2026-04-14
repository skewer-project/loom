#include <gtest/gtest.h>

#include "core/Graph.hpp"

namespace core = loom::core;

class NodeBehaviorTest : public ::testing::Test {
  protected:
    core::Graph graph;
};

TEST_F(NodeBehaviorTest, LinkOverwriting) {
    // Test that connecting a new output to an occupied input replaces the old link
    core::NodeHandle constA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle constB = graph.addNode(core::NodeType::Constant);
    core::NodeHandle viewer = graph.addNode(core::NodeType::Viewer);

    core::PinHandle outA = graph.getNode(constA)->outputs[0];
    core::PinHandle outB = graph.getNode(constB)->outputs[0];
    core::PinHandle inV = graph.getNode(viewer)->inputs[0];

    // First link: A -> V
    EXPECT_TRUE(graph.tryAddLink(outA, inV));
    core::LinkHandle link1 = graph.getPin(inV)->link;
    EXPECT_EQ(graph.getPin(outA)->links.size(), 1);

    // Second link: B -> V (should replace A -> V)
    EXPECT_TRUE(graph.tryAddLink(outB, inV));
    core::LinkHandle link2 = graph.getPin(inV)->link;

    EXPECT_NE(link1, link2);
    EXPECT_EQ(graph.getLink(link1), nullptr);        // Link 1 should be destroyed
    EXPECT_EQ(graph.getPin(outA)->links.size(), 0);  // OutA should be empty
    EXPECT_EQ(graph.getPin(outB)->links.size(), 1);  // OutB should have the new link
}

TEST_F(NodeBehaviorTest, MultipleOutboundLinks) {
    // One output driving multiple inputs
    core::NodeHandle source = graph.addNode(core::NodeType::Constant);
    core::NodeHandle v1 = graph.addNode(core::NodeType::Viewer);
    core::NodeHandle v2 = graph.addNode(core::NodeType::Viewer);

    core::PinHandle out = graph.getNode(source)->outputs[0];
    core::PinHandle in1 = graph.getNode(v1)->inputs[0];
    core::PinHandle in2 = graph.getNode(v2)->inputs[0];

    EXPECT_TRUE(graph.tryAddLink(out, in1));
    EXPECT_TRUE(graph.tryAddLink(out, in2));

    EXPECT_EQ(graph.getPin(out)->links.size(), 2);
    EXPECT_TRUE(graph.getPin(in1)->link.isValid());
    EXPECT_TRUE(graph.getPin(in2)->link.isValid());
    EXPECT_NE(graph.getPin(in1)->link, graph.getPin(in2)->link);
}

TEST_F(NodeBehaviorTest, CyclePrevention) {
    // Passthrough nodes: A -> B -> C -> A (should fail)
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Passthrough);
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Passthrough);
    core::NodeHandle nodeC = graph.addNode(core::NodeType::Passthrough);

    auto getOut = [&](core::NodeHandle h) { return graph.getNode(h)->outputs[0]; };
    auto getIn = [&](core::NodeHandle h) { return graph.getNode(h)->inputs[0]; };

    EXPECT_TRUE(graph.tryAddLink(getOut(nodeA), getIn(nodeB)));
    EXPECT_TRUE(graph.tryAddLink(getOut(nodeB), getIn(nodeC)));

    // Try to close the loop: C -> A
    EXPECT_FALSE(graph.canAddLink(getOut(nodeC), getIn(nodeA)));
    EXPECT_FALSE(graph.tryAddLink(getOut(nodeC), getIn(nodeA)));

    // Self-loop: A -> A
    EXPECT_FALSE(graph.canAddLink(getOut(nodeA), getIn(nodeA)));
}

TEST_F(NodeBehaviorTest, HandleReconstitution) {
    // Verify that we can get valid handles back from raw indices
    core::NodeHandle h1 = graph.addNode(core::NodeType::Constant);
    uint32_t index = h1.index;

    core::NodeHandle h2 = graph.getNodeHandleByIndex(index);
    EXPECT_EQ(h1, h2);
    EXPECT_TRUE(h2.isValid());

    graph.removeNode(h1);
    core::NodeHandle h3 = graph.getNodeHandleByIndex(index);
    EXPECT_FALSE(h3.isValid());  // Generation should have advanced
}

TEST_F(NodeBehaviorTest, NodeSwappingImpact) {
    // Test deleting a node in the middle of a chain
    core::NodeHandle n1 = graph.addNode(core::NodeType::Constant);
    core::NodeHandle n2 = graph.addNode(core::NodeType::Passthrough);
    core::NodeHandle n3 = graph.addNode(core::NodeType::Viewer);

    graph.tryAddLink(graph.getNode(n1)->outputs[0], graph.getNode(n2)->inputs[0]);
    graph.tryAddLink(graph.getNode(n2)->outputs[0], graph.getNode(n3)->inputs[0]);

    // Chain: 1 -> 2 -> 3
    EXPECT_TRUE(graph.getPin(graph.getNode(n3)->inputs[0])->link.isValid());

    // Remove middle node
    graph.removeNode(n2);

    // Node 3's input should now be disconnected
    EXPECT_FALSE(graph.getPin(graph.getNode(n3)->inputs[0])->link.isValid());
    // Node 1's output should have 0 links
    EXPECT_EQ(graph.getPin(graph.getNode(n1)->outputs[0])->links.size(), 0);
}
