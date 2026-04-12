#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Handle.hpp"

namespace loom::core {

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

    Node(NodeHandle h, NodeType t, std::string n)
        : id(h), type(t), name(std::move(n)), inputs(), outputs(), isDirty(true) {}
};

}  // namespace loom::core
