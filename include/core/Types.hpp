#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Handle.hpp"

namespace loom::core {

enum class IdTag : uint32_t { Node = 0x00000000, Pin = 0x40000000, Link = 0x80000000 };

inline uint64_t encodeId(uint32_t index, IdTag tag) {
    // Ensure index fits in 30 bits and offset by 1 to avoid 0 (Invalid)
    return (uint64_t)(index + 1) | (uint64_t)tag;
}

inline uint32_t decodeIndex(uint64_t id) {
    // Mask out the tag bits and revert the +1 offset
    return (uint32_t)(id & 0x3FFFFFFF) - 1;
}

enum class PinDirection { Input, Output };
enum class PinType { Float, DeepBuffer };
enum class NodeType { Constant, Merge, Viewer, Passthrough };

struct Pin {
    PinHandle id;
    NodeHandle node;  // Owner
    PinDirection direction;
    PinType type;
    LinkHandle link;                // For Inputs: holds at most one link. Default to invalid.
    std::vector<LinkHandle> links;  // For Outputs: holds multiple outbound links.

    Pin(PinHandle h, NodeHandle n, PinDirection dir, PinType t)
        : id(h), node(n), direction(dir), type(t), link(), links() {}
};

struct Link {
    LinkHandle id;
    PinHandle startPin;  // Must be an Output pin
    PinHandle endPin;    // Must be an Input pin

    Link(LinkHandle h, PinHandle start, PinHandle end) : id(h), startPin(start), endPin(end) {}
};

struct Node {
    NodeHandle id;
    NodeType type;
    std::string name;
    std::vector<PinHandle> inputs;
    std::vector<PinHandle> outputs;
    bool isDirty = true;

    // UI/Spawn state
    bool hasSpawnPos = false;
    float spawnX = 0.0f;
    float spawnY = 0.0f;

    Node(NodeHandle h, NodeType t, std::string n)
        : id(h), type(t), name(std::move(n)), inputs(), outputs(), isDirty(true) {}
};

}  // namespace loom::core
