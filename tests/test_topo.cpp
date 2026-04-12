#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include "core/Graph.hpp"

using namespace loom::core;

#define ASSERT_WITH_LOG(cond, msg)                             \
    if (!(cond)) {                                             \
        std::cerr << "ASSERTION FAILED: " << msg << std::endl; \
        assert(cond);                                          \
    }

void testLinearSort() {
    std::cout << "Starting testLinearSort..." << std::endl;
    Graph graph;
    // Constant (Out) -> Passthrough (In, Out) -> Viewer (In)
    NodeHandle nodeA = graph.addNode(NodeType::Constant, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Passthrough, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Viewer, "C");

    auto* pA = graph.getNode(nodeA);
    auto* pB = graph.getNode(nodeB);
    auto* pC = graph.getNode(nodeC);

    graph.tryAddLink(pA->outputs[0], pB->inputs[0]);
    graph.tryAddLink(pB->outputs[0], pC->inputs[0]);

    const auto& order = graph.getTopologicalOrder();
    ASSERT_WITH_LOG(order.size() == 3, "Linear sort size should be 3");
    ASSERT_WITH_LOG(order[0] == nodeA, "First node should be A");
    ASSERT_WITH_LOG(order[1] == nodeB, "Second node should be B");
    ASSERT_WITH_LOG(order[2] == nodeC, "Third node should be C");

    std::cout << "[PASS] Linear Sort Test" << std::endl;
}

void testCycleRejection() {
    std::cout << "Starting testCycleRejection..." << std::endl;
    Graph graph;
    // All Passthrough (In, Out) to allow chaining
    NodeHandle nodeA = graph.addNode(NodeType::Passthrough, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Passthrough, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Passthrough, "C");

    auto* pA = graph.getNode(nodeA);
    auto* pB = graph.getNode(nodeB);
    auto* pC = graph.getNode(nodeC);

    graph.tryAddLink(pA->outputs[0], pB->inputs[0]);
    graph.tryAddLink(pB->outputs[0], pC->inputs[0]);

    // Attempt C -> A (Cycle)
    bool success = graph.tryAddLink(pC->outputs[0], pA->inputs[0]);
    ASSERT_WITH_LOG(success == false, "Cycle link C->A should fail");

    ASSERT_WITH_LOG(graph.getTopologicalOrder().size() == 3,
                    "Topo order size should be 3 after failed link");

    std::cout << "[PASS] Cycle Rejection Test" << std::endl;
}

void testComplexRejection() {
    std::cout << "Starting testComplexRejection..." << std::endl;
    Graph graph;
    // A (Constant: Out)
    // B, C (Passthrough: In, Out)
    // D (Merge: In, In, Out)
    NodeHandle nodeA = graph.addNode(NodeType::Constant, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Passthrough, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Passthrough, "C");
    NodeHandle nodeD = graph.addNode(NodeType::Merge, "D");

    auto* pA = graph.getNode(nodeA);
    auto* pB = graph.getNode(nodeB);
    auto* pC = graph.getNode(nodeC);
    auto* pD = graph.getNode(nodeD);

    // Diamond: A->B, A->C, B->D, C->D
    graph.tryAddLink(pA->outputs[0], pB->inputs[0]);
    graph.tryAddLink(pA->outputs[0], pC->inputs[0]);
    graph.tryAddLink(pB->outputs[0], pD->inputs[0]);
    graph.tryAddLink(pC->outputs[0], pD->inputs[1]);

    // Attempt D -> B (Cycle)
    // D has an output, B has an input (already occupied but tryAddLink handles replacement)
    bool success = graph.tryAddLink(pD->outputs[0], pB->inputs[0]);
    ASSERT_WITH_LOG(success == false, "Cycle D->B should fail");

    const auto& order = graph.getTopologicalOrder();
    ASSERT_WITH_LOG(order.size() == 4, "Topo order size should be 4");
    ASSERT_WITH_LOG(order[0] == nodeA, "Diamond: First node should be A");

    auto itB = std::find(order.begin(), order.end(), nodeB);
    auto itC = std::find(order.begin(), order.end(), nodeC);
    auto itD = std::find(order.begin(), order.end(), nodeD);

    ASSERT_WITH_LOG(itB < itD, "B must be before D");
    ASSERT_WITH_LOG(itC < itD, "C must be before D");

    std::cout << "[PASS] Complex Rejection Test" << std::endl;
}

void testSelfLoopRejection() {
    std::cout << "Starting testSelfLoopRejection..." << std::endl;
    Graph graph;
    NodeHandle nodeA = graph.addNode(NodeType::Passthrough, "A");
    auto* pA = graph.getNode(nodeA);

    // Attempt A -> A
    bool success = graph.tryAddLink(pA->outputs[0], pA->inputs[0]);
    ASSERT_WITH_LOG(success == false, "Self-loop A->A should fail");

    std::cout << "[PASS] Self-Loop Rejection Test" << std::endl;
}

void testMassiveGraph() {
    std::cout << "Starting testMassiveGraph..." << std::endl;
    Graph graph;
    const int COUNT = 1000;
    std::vector<NodeHandle> nodes;
    for (int i = 0; i < COUNT; ++i) {
        nodes.push_back(graph.addNode(NodeType::Passthrough));
    }

    // Link in a long chain
    for (int i = 0; i < COUNT - 1; ++i) {
        auto* src = graph.getNode(nodes[i]);
        auto* dst = graph.getNode(nodes[i + 1]);
        bool ok = graph.tryAddLink(src->outputs[0], dst->inputs[0]);
        ASSERT_WITH_LOG(ok, "Chain link should succeed");
    }

    const auto& order = graph.getTopologicalOrder();
    ASSERT_WITH_LOG(order.size() == COUNT, "Massive topo order size mismatch");
    for (int i = 0; i < COUNT; ++i) {
        ASSERT_WITH_LOG(order[i] == nodes[i], "Massive chain order mismatch");
    }

    // Attempt a massive backlink
    auto* last = graph.getNode(nodes[COUNT - 1]);
    auto* first = graph.getNode(nodes[0]);
    bool cycle = graph.tryAddLink(last->outputs[0], first->inputs[0]);
    ASSERT_WITH_LOG(!cycle, "Massive backlink should be rejected");

    std::cout << "[PASS] Massive Graph Test (" << COUNT << " nodes)" << std::endl;
}

int main() {
    try {
        testLinearSort();
        testCycleRejection();
        testComplexRejection();
        testSelfLoopRejection();
        testMassiveGraph();
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\nAll 1.3 Topological Sort & Cycle Detection tests passed successfully!"
              << std::endl;
    return 0;
}
