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

// Viewport input-mode dropdown rewires the Viewer's input pin via this
// helper. The contract:
//   - Re-pointing onto a different upstream output succeeds and breaks
//     the previous link.
//   - Re-pointing onto a non-Viewer handle fails, leaving any existing
//     wiring intact.
//   - Re-pointing onto a type-incompatible source (e.g. Deep pin into a
//     Float pin) fails (canAddLink rejects) — `tryAddLink` returns false
//     and the prior wire is preserved.
// See `Graph::replaceViewerInput` in include/core/Graph.hpp.
TEST_F(GraphTest, ReplaceViewerInputSwapsUpstream) {
    core::NodeHandle constA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle constB = graph.addNode(core::NodeType::Constant);
    core::NodeHandle viewer = graph.addNode(core::NodeType::Viewer);

    core::PinHandle outA = graph.getNode(constA)->outputs[0];
    core::PinHandle outB = graph.getNode(constB)->outputs[0];
    core::PinHandle inViewer = graph.getNode(viewer)->inputs[0];

    ASSERT_TRUE(graph.tryAddLink(outA, inViewer));
    core::LinkHandle firstLink = graph.getPin(inViewer)->link;
    ASSERT_TRUE(firstLink.isValid());

    EXPECT_TRUE(graph.replaceViewerInput(viewer, outB));
    core::LinkHandle secondLink = graph.getPin(inViewer)->link;
    EXPECT_TRUE(secondLink.isValid());
    EXPECT_NE(firstLink, secondLink);

    // The first link's slot should now be invalid (it was removed inside
    // `tryAddLink` when it saw the input already had a link).
    EXPECT_EQ(graph.getLink(firstLink), nullptr);
}

TEST_F(GraphTest, ReplaceViewerInputRejectsNonViewer) {
    core::NodeHandle constA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle constB = graph.addNode(core::NodeType::Constant);

    core::PinHandle outB = graph.getNode(constB)->outputs[0];
    EXPECT_FALSE(graph.replaceViewerInput(constA, outB));
}

TEST_F(GraphTest, ReplaceViewerInputPreservesOnTypeMismatch) {
    core::NodeHandle constA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle viewer = graph.addNode(core::NodeType::Viewer);
    core::NodeHandle reader = graph.addNode(core::NodeType::DeepEXRRead);

    core::PinHandle outA = graph.getNode(constA)->outputs[0];
    core::PinHandle outDeep = graph.getNode(reader)->outputs[0];
    core::PinHandle inViewer = graph.getNode(viewer)->inputs[0];

    ASSERT_TRUE(graph.tryAddLink(outA, inViewer));
    core::LinkHandle preserved = graph.getPin(inViewer)->link;
    ASSERT_TRUE(preserved.isValid());

    // Deep output → Image input is a type mismatch; canAddLink rejects.
    // The existing link must remain intact (no half-rewire state).
    EXPECT_FALSE(graph.replaceViewerInput(viewer, outDeep));
    EXPECT_EQ(graph.getPin(inViewer)->link, preserved);
    EXPECT_NE(graph.getLink(preserved), nullptr);
}

TEST_F(GraphTest, CascadingDeletion) {
    core::NodeHandle nodeA = graph.addNode(core::NodeType::Constant);
    core::NodeHandle nodeB = graph.addNode(core::NodeType::Viewer);

    core::PinHandle outA = graph.getNode(nodeA)->outputs[0];
    core::PinHandle inB = graph.getNode(nodeB)->inputs[0];

    ASSERT_TRUE(graph.tryAddLink(outA, inB));
    core::LinkHandle lh = graph.getPin(inB)->link;

    ASSERT_NE(graph.getLink(lh), nullptr);

    graph.removeNode(nodeA);

    EXPECT_EQ(graph.getNode(nodeA), nullptr);
    EXPECT_EQ(graph.getPin(outA), nullptr);
    EXPECT_EQ(graph.getLink(lh), nullptr);
    EXPECT_NE(graph.getPin(inB), nullptr);
    EXPECT_FALSE(graph.getPin(inB)->link.isValid());
}
