#include <cassert>
#include <iostream>

#include "core/Graph.hpp"

using namespace loom::core;

void testCreation() {
    Graph graph;
    NodeHandle h = graph.addNode(NodeType::Merge);

    Node* node = graph.getNode(h);
    assert(node != nullptr);
    assert(node->type == NodeType::Merge);
    assert(node->inputs.size() == 2);
    assert(node->outputs.size() == 1);

    // Verify pins exist in slotmap
    for (auto ph : node->inputs) assert(graph.getPin(ph) != nullptr);
    for (auto ph : node->outputs) assert(graph.getPin(ph) != nullptr);

    std::cout << "[PASS] Node Creation Test" << std::endl;
}

void testLinking() {
    Graph graph;
    NodeHandle nodeA = graph.addNode(NodeType::Constant);
    NodeHandle nodeB = graph.addNode(NodeType::Viewer);

    Node* a = graph.getNode(nodeA);
    Node* b = graph.getNode(nodeB);

    PinHandle outA = a->outputs[0];
    PinHandle inB = b->inputs[0];

    bool linked = graph.tryAddLink(outA, inB);
    assert(linked == true);

    Pin* pOutA = graph.getPin(outA);
    Pin* pInB = graph.getPin(inB);

    assert(pInB->link.isValid());
    assert(pOutA->links.size() == 1);
    assert(pOutA->links[0] == pInB->link);

    Link* link = graph.getLink(pInB->link);
    assert(link != nullptr);
    assert(link->startPin == outA);
    assert(link->endPin == inB);

    std::cout << "[PASS] Linking Test" << std::endl;
}

void testTypeSafety() {
    Graph graph;
    // For this test, let's manually create a DeepBuffer pin to test type mismatch
    // In current setupNodePins, all are Float. Let's add a way to test this.
    // We can just manually modify a pin's type for the test since we have access.

    NodeHandle nodeA = graph.addNode(NodeType::Constant);
    NodeHandle nodeB = graph.addNode(NodeType::Viewer);

    PinHandle outA = graph.getNode(nodeA)->outputs[0];
    PinHandle inB = graph.getNode(nodeB)->inputs[0];

    graph.getPin(inB)->type = PinType::DeepBuffer;

    bool linked = graph.tryAddLink(outA, inB);
    assert(linked == false);
    assert(!graph.getPin(inB)->link.isValid());

    std::cout << "[PASS] Type Safety Test" << std::endl;
}

void testCascadingDeletion() {
    Graph graph;
    NodeHandle nodeA = graph.addNode(NodeType::Constant);
    NodeHandle nodeB = graph.addNode(NodeType::Viewer);

    PinHandle outA = graph.getNode(nodeA)->outputs[0];
    PinHandle inB = graph.getNode(nodeB)->inputs[0];

    graph.tryAddLink(outA, inB);
    LinkHandle lh = graph.getPin(inB)->link;

    assert(graph.getLink(lh) != nullptr);

    graph.removeNode(nodeA);

    assert(graph.getNode(nodeA) == nullptr);
    assert(graph.getPin(outA) == nullptr);
    assert(graph.getLink(lh) == nullptr);
    assert(graph.getPin(inB) != nullptr);
    assert(!graph.getPin(inB)->link.isValid());

    std::cout << "[PASS] Cascading Deletion Test" << std::endl;
}

int main() {
    testCreation();
    testLinking();
    testTypeSafety();
    testCascadingDeletion();

    std::cout << "\nAll Graph tests passed successfully!" << std::endl;
    return 0;
}
