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
    NodeHandle nodeA = graph.addNode(NodeType::Constant, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Passthrough, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Viewer, "C");

    // A -> B -> C
    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeB)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeB)->outputs[0], graph.getNode(nodeC)->inputs[0]);

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
    NodeHandle nodeA = graph.addNode(NodeType::Passthrough, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Passthrough, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Passthrough, "C");

    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeB)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeB)->outputs[0], graph.getNode(nodeC)->inputs[0]);

    // Attempt C -> A (Cycle)
    bool success =
        graph.tryAddLink(graph.getNode(nodeC)->outputs[0], graph.getNode(nodeA)->inputs[0]);
    ASSERT_WITH_LOG(success == false, "Cycle link C->A should fail");

    ASSERT_WITH_LOG(graph.getTopologicalOrder().size() == 3,
                    "Topo order size should be 3 after failed link");

    std::cout << "[PASS] Cycle Rejection Test" << std::endl;
}

void testComplexRejection() {
    std::cout << "Starting testComplexRejection..." << std::endl;
    Graph graph;
    NodeHandle nodeA = graph.addNode(NodeType::Constant, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Passthrough, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Passthrough, "C");
    NodeHandle nodeD = graph.addNode(NodeType::Merge, "D");

    std::cout << "Nodes added." << std::endl;

    auto* pA = graph.getNode(nodeA);
    auto* pB = graph.getNode(nodeB);
    auto* pC = graph.getNode(nodeC);
    auto* pD = graph.getNode(nodeD);

    ASSERT_WITH_LOG(pA && pB && pC && pD, "All nodes must exist");

    // Diamond: A->B, A->C, B->D, C->D
    std::cout << "Adding A->B link..." << std::endl;
    bool l1 = graph.tryAddLink(pA->outputs[0], pB->inputs[0]);
    std::cout << "Adding A->C link..." << std::endl;
    bool l2 = graph.tryAddLink(pA->outputs[0], pC->inputs[0]);
    std::cout << "Adding B->D link..." << std::endl;
    bool l3 = graph.tryAddLink(pB->outputs[0], pD->inputs[0]);
    std::cout << "Adding C->D link..." << std::endl;
    bool l4 = graph.tryAddLink(pC->outputs[0], pD->inputs[1]);

    ASSERT_WITH_LOG(l1 && l2 && l3 && l4, "Diamond links must succeed");

    // Attempt D -> A (Cycle)
    std::cout << "Attempting D->A cycle..." << std::endl;
    bool success = graph.tryAddLink(pD->outputs[0], pA->inputs[0]);
    ASSERT_WITH_LOG(success == false, "Cycle D->A should fail");

    std::cout << "Getting topological order..." << std::endl;
    const auto& order = graph.getTopologicalOrder();
    ASSERT_WITH_LOG(order.size() == 4, "Topo order size should be 4");
    ASSERT_WITH_LOG(order[0] == nodeA, "Diamond: First node should be A");

    auto itB = std::find(order.begin(), order.end(), nodeB);
    auto itC = std::find(order.begin(), order.end(), nodeC);
    auto itD = std::find(order.begin(), order.end(), nodeD);

    ASSERT_WITH_LOG(itB != order.end(), "B must be in order");
    ASSERT_WITH_LOG(itC != order.end(), "C must be in order");
    ASSERT_WITH_LOG(itD != order.end(), "D must be in order");
    ASSERT_WITH_LOG(itB < itD, "B must be before D");
    ASSERT_WITH_LOG(itC < itD, "C must be before D");

    std::cout << "[PASS] Complex Rejection Test" << std::endl;
}

void testDisjointGraphs() {
    std::cout << "Starting testDisjointGraphs..." << std::endl;
    Graph graph;
    NodeHandle nodeA = graph.addNode(NodeType::Constant, "A");
    NodeHandle nodeB = graph.addNode(NodeType::Viewer, "B");
    NodeHandle nodeC = graph.addNode(NodeType::Constant, "C");
    NodeHandle nodeD = graph.addNode(NodeType::Viewer, "D");

    graph.tryAddLink(graph.getNode(nodeA)->outputs[0], graph.getNode(nodeB)->inputs[0]);
    graph.tryAddLink(graph.getNode(nodeC)->outputs[0], graph.getNode(nodeD)->inputs[0]);

    const auto& order = graph.getTopologicalOrder();
    ASSERT_WITH_LOG(order.size() == 4, "Disjoint: size should be 4");

    auto itA = std::find(order.begin(), order.end(), nodeA);
    auto itB = std::find(order.begin(), order.end(), nodeB);
    auto itC = std::find(order.begin(), order.end(), nodeC);
    auto itD = std::find(order.begin(), order.end(), nodeD);

    ASSERT_WITH_LOG(itA < itB, "A must be before B");
    ASSERT_WITH_LOG(itC < itD, "C must be before D");

    std::cout << "[PASS] Disjoint Graphs Test" << std::endl;
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

int main() {
    try {
        testLinearSort();
        testCycleRejection();
        testComplexRejection();
        testDisjointGraphs();
        testSelfLoopRejection();
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\nAll 1.3 Topological Sort & Cycle Detection tests passed!" << std::endl;
    return 0;
}
