#include <gtest/gtest.h>

#include <string>

#include "core/Handle.hpp"
#include "core/SlotMap.hpp"

namespace core = loom::core;

struct MyNode {
    std::string name;
    int value;
    MyNode(std::string n, int v) : name(std::move(n)), value(v) {}
};

TEST(SlotMapTest, InsertionReturnsValidHandle) {
    core::SlotMap<MyNode, core::NodeHandle> map;
    core::NodeHandle h1 = map.emplace("Node A", 42);
    EXPECT_TRUE(h1.isValid());
    EXPECT_TRUE(map.isValid(h1));
}

TEST(SlotMapTest, GetReturnsCorrectData) {
    core::SlotMap<MyNode, core::NodeHandle> map;
    core::NodeHandle h1 = map.emplace("Node A", 42);
    MyNode* n1 = map.get(h1);
    ASSERT_NE(n1, nullptr);
    EXPECT_EQ(n1->name, "Node A");
    EXPECT_EQ(n1->value, 42);
}

TEST(SlotMapTest, RemovalInvalidatesHandle) {
    core::SlotMap<MyNode, core::NodeHandle> map;
    core::NodeHandle h1 = map.emplace("Node A", 42);
    EXPECT_TRUE(map.remove(h1));
    EXPECT_FALSE(map.isValid(h1));
    EXPECT_EQ(map.get(h1), nullptr);
}

TEST(SlotMapTest, SlotReusesMemoryWithNewGeneration) {
    core::SlotMap<MyNode, core::NodeHandle> map;
    core::NodeHandle h1 = map.emplace("Node A", 42);
    uint32_t originalIndex = h1.index;
    uint32_t originalGen = h1.generation;

    map.remove(h1);
    core::NodeHandle h2 = map.emplace("Node B", 99);

    EXPECT_EQ(h2.index, originalIndex);
    EXPECT_GT(h2.generation, originalGen);
    EXPECT_EQ(map.get(h1), nullptr);
}

TEST(SlotMapTest, IterationSkipsRemovedItems) {
    core::SlotMap<MyNode, core::NodeHandle> map;
    map.emplace("Node A", 1);
    core::NodeHandle h2 = map.emplace("Node B", 2);
    map.emplace("Node C", 3);

    map.remove(h2);

    int count = 0;
    map.forEach([&](core::NodeHandle h, MyNode& node) { count++; });
    EXPECT_EQ(count, 2);
}
