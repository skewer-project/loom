#include <cassert>
#include <iostream>
#include <string>

#include "../include/core/Handle.hpp"
#include "../include/core/SlotMap.hpp"

using namespace loom::core;

struct MyNode {
    std::string name;
    int value;

    MyNode(std::string n, int v) : name(std::move(n)), value(v) {}
};

int main() {
    SlotMap<MyNode, NodeHandle> map;

    // 1. Inserting an item returns a valid handle.
    NodeHandle h1 = map.emplace("Node A", 42);
    assert(h1.isValid());
    assert(map.isValid(h1));
    std::cout << "[PASS] Inserting an item returns a valid handle." << std::endl;

    // 2. get() returns the correct data.
    MyNode* n1 = map.get(h1);
    assert(n1 != nullptr);
    assert(n1->name == "Node A");
    assert(n1->value == 42);
    std::cout << "[PASS] get() returns the correct data." << std::endl;

    // 3. Removing an item makes the original handle invalid.
    bool removed = map.remove(h1);
    assert(removed == true);
    assert(map.isValid(h1) == false);
    assert(map.get(h1) == nullptr);
    std::cout << "[PASS] Removing an item makes the original handle invalid." << std::endl;

    // 4. Inserting a new item reuses the memory slot of the removed item.
    NodeHandle h2 = map.emplace("Node B", 99);
    assert(h2.index == h1.index);
    assert(h2.generation == h1.generation + 1);
    std::cout << "[PASS] Inserting a new item reuses the memory slot of the removed item."
              << std::endl;

    // 5. Attempting to get() the new item with the old handle returns nullptr.
    assert(map.get(h1) == nullptr);
    std::cout << "[PASS] Attempting to get() the new item with the old handle returns nullptr."
              << std::endl;

    MyNode* n2 = map.get(h2);
    assert(n2 != nullptr);
    assert(n2->name == "Node B");

    // Test Iteration
    map.emplace("Node C", 100);
    map.emplace("Node D", 101);

    int count = 0;
    map.forEach([&](NodeHandle h, MyNode& node) { count++; });
    assert(count == 3);  // Node B, Node C, Node D
    std::cout << "[PASS] Iteration correctly skips removed items." << std::endl;

    std::cout << "\nAll SlotMap tests passed successfully!" << std::endl;
    return 0;
}
