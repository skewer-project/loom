#pragma once

#include <algorithm>
#include <stdexcept>

#include "core/SlotMap.hpp"
#include "core/Types.hpp"

namespace loom::core {

class Graph {
  public:
    NodeHandle addNode(NodeType type, std::string name = "") {
        NodeHandle nodeHandle = nodes.emplace(NodeHandle(), type, name);
        Node* node = nodes.get(nodeHandle);
        node->id = nodeHandle;
        if (node->name.empty()) {
            node->name = getDefaultNodeName(type);
        }

        // Based on NodeType, instantiate the necessary Pin objects.
        setupNodePins(node);

        return nodeHandle;
    }

    void removeNode(NodeHandle nodeHandle) {
        Node* node = nodes.get(nodeHandle);
        if (!node) return;

        // Iterate its input/output pins. Call removeLink on all connected links.
        for (PinHandle pinHandle : node->inputs) {
            Pin* pin = pins.get(pinHandle);
            if (pin && pin->link.isValid()) {
                removeLink(pin->link);
            }
            pins.remove(pinHandle);
        }

        for (PinHandle pinHandle : node->outputs) {
            Pin* pin = pins.get(pinHandle);
            if (pin) {
                // Copy links to avoid iterator invalidation during removal
                std::vector<LinkHandle> outboundLinks = pin->links;
                for (LinkHandle linkHandle : outboundLinks) {
                    removeLink(linkHandle);
                }
            }
            pins.remove(pinHandle);
        }

        nodes.remove(nodeHandle);
    }

    bool tryAddLink(PinHandle startPinHandle, PinHandle endPinHandle) {
        Pin* startPin = pins.get(startPinHandle);
        Pin* endPin = pins.get(endPinHandle);

        // Validate: Ensure both handles are valid
        if (!startPin || !endPin) return false;

        // Ensure startPin is Output, endPin is Input
        if (startPin->direction != PinDirection::Output) return false;
        if (endPin->direction != PinDirection::Input) return false;

        // Ensure startPin.type == endPin.type
        if (startPin->type != endPin->type) return false;

        // TODO 1.3: Run DFS cycle detection.

        // Execute: If endPin already has a valid link, call removeLink on it.
        if (endPin->link.isValid()) {
            removeLink(endPin->link);
        }

        // Create the new Link and add it to the link slot map.
        LinkHandle linkHandle = links.emplace(LinkHandle(), startPinHandle, endPinHandle);
        Link* link = links.get(linkHandle);
        link->id = linkHandle;

        // Update endPin.link with the new handle.
        endPin->link = linkHandle;

        // Add the new handle to startPin.links.
        startPin->links.push_back(linkHandle);

        // Mark the destination Node as dirty.
        Node* endNode = nodes.get(endPin->node);
        if (endNode) {
            endNode->isDirty = true;
        }

        return true;
    }

    void removeLink(LinkHandle linkHandle) {
        Link* link = links.get(linkHandle);
        if (!link) return;

        Pin* startPin = pins.get(link->startPin);
        Pin* endPin = pins.get(link->endPin);

        // Remove the link reference from the startPin's vector
        if (startPin) {
            auto& v = startPin->links;
            v.erase(std::remove(v.begin(), v.end(), linkHandle), v.end());
        }

        // Clear the endPin's link handle
        if (endPin) {
            endPin->link = LinkHandle();  // Invalid handle

            // Mark the destination node as dirty
            Node* endNode = nodes.get(endPin->node);
            if (endNode) {
                endNode->isDirty = true;
            }
        }

        links.remove(linkHandle);
    }

    // Accessors for testing/UI
    Node* getNode(NodeHandle h) { return nodes.get(h); }
    Pin* getPin(PinHandle h) { return pins.get(h); }
    Link* getLink(LinkHandle h) { return links.get(h); }

    const SlotMap<Node, NodeHandle>& getNodes() const { return nodes; }
    const SlotMap<Pin, PinHandle>& getPins() const { return pins; }
    const SlotMap<Link, LinkHandle>& getLinks() const { return links; }

  private:
    SlotMap<Node, NodeHandle> nodes;
    SlotMap<Pin, PinHandle> pins;
    SlotMap<Link, LinkHandle> links;

    std::string getDefaultNodeName(NodeType type) {
        switch (type) {
            case NodeType::Constant:
                return "Constant";
            case NodeType::Merge:
                return "Merge";
            case NodeType::Viewer:
                return "Viewer";
            case NodeType::Passthrough:
                return "Passthrough";
            default:
                return "Unknown";
        }
    }

    void setupNodePins(Node* node) {
        switch (node->type) {
            case NodeType::Constant:
                createPin(node, PinDirection::Output, PinType::Float);
                break;
            case NodeType::Merge:
                createPin(node, PinDirection::Input, PinType::Float);
                createPin(node, PinDirection::Input, PinType::Float);
                createPin(node, PinDirection::Output, PinType::Float);
                break;
            case NodeType::Viewer:
                createPin(node, PinDirection::Input, PinType::Float);
                break;
            case NodeType::Passthrough:
                createPin(node, PinDirection::Input, PinType::Float);
                createPin(node, PinDirection::Output, PinType::Float);
                break;
        }
    }

    PinHandle createPin(Node* node, PinDirection dir, PinType type) {
        PinHandle h = pins.emplace(PinHandle(), node->id, dir, type);
        Pin* pin = pins.get(h);
        pin->id = h;

        if (dir == PinDirection::Input) {
            node->inputs.push_back(h);
        } else {
            node->outputs.push_back(h);
        }
        return h;
    }
};

}  // namespace loom::core
