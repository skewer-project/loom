#pragma once

#include <algorithm>
#include <cassert>
#include <queue>
#include <stdexcept>
#include <vector>

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

        setupNodePins(node);

        isTopoDirty = true;
        return nodeHandle;
    }

    void removeNode(NodeHandle nodeHandle) {
        Node* node = nodes.get(nodeHandle);
        if (!node) return;

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
                std::vector<LinkHandle> outboundLinks = pin->links;
                for (LinkHandle linkHandle : outboundLinks) {
                    removeLink(linkHandle);
                }
            }
            pins.remove(pinHandle);
        }

        nodes.remove(nodeHandle);
        isTopoDirty = true;
    }

    bool tryAddLink(PinHandle startPinHandle, PinHandle endPinHandle) {
        Pin* startPin = pins.get(startPinHandle);
        Pin* endPin = pins.get(endPinHandle);

        if (!startPin || !endPin) return false;
        if (startPin->direction != PinDirection::Output) return false;
        if (endPin->direction != PinDirection::Input) return false;
        if (startPin->type != endPin->type) return false;

        NodeHandle nodeA = startPin->node;
        NodeHandle nodeB = endPin->node;

        // Step 2: Self-loop guard
        if (nodeA == nodeB) return false;

        // Step 2: Cycle check
        if (isReachable(nodeB, nodeA)) return false;

        if (endPin->link.isValid()) {
            removeLink(endPin->link);
        }

        LinkHandle linkHandle = links.emplace(LinkHandle(), startPinHandle, endPinHandle);
        Link* link = links.get(linkHandle);
        link->id = linkHandle;

        endPin->link = linkHandle;
        startPin->links.push_back(linkHandle);

        Node* endNode = nodes.get(nodeB);
        if (endNode) {
            endNode->isDirty = true;
        }

        isTopoDirty = true;
        return true;
    }

    void removeLink(LinkHandle linkHandle) {
        Link* link = links.get(linkHandle);
        if (!link) return;

        Pin* startPin = pins.get(link->startPin);
        Pin* endPin = pins.get(link->endPin);

        if (startPin) {
            auto& v = startPin->links;
            v.erase(std::remove(v.begin(), v.end(), linkHandle), v.end());
        }

        if (endPin) {
            endPin->link = LinkHandle();
            Node* endNode = nodes.get(endPin->node);
            if (endNode) {
                endNode->isDirty = true;
            }
        }

        links.remove(linkHandle);
        isTopoDirty = true;
    }

    // Step 4: Expose Sorted Output
    const std::vector<NodeHandle>& getTopologicalOrder() {
        if (isTopoDirty) {
            computeTopologicalOrder();
            isTopoDirty = false;
        }
        return topoOrder;
    }

    Node* getNode(NodeHandle h) { return nodes.get(h); }
    Pin* getPin(PinHandle h) { return pins.get(h); }
    Link* getLink(LinkHandle h) { return links.get(h); }

  private:
    SlotMap<Node, NodeHandle> nodes;
    SlotMap<Pin, PinHandle> pins;
    SlotMap<Link, LinkHandle> links;

    std::vector<NodeHandle> topoOrder;
    bool isTopoDirty = true;

    // Allocation pressure fix: reusable scratch buffer
    mutable std::vector<uint8_t> m_visitedScratch;

    // Step 1: Cycle Detection (DFS)
    bool isReachable(NodeHandle startNode, NodeHandle targetNode) const {
        if (!startNode.isValid() || !targetNode.isValid()) return false;
        if (startNode == targetNode) return true;

        uint32_t cap = nodes.capacity();
        if (m_visitedScratch.size() < cap) {
            m_visitedScratch.resize(cap);
        }
        std::fill(m_visitedScratch.begin(), m_visitedScratch.begin() + cap, 0);

        std::vector<NodeHandle> stack;
        stack.push_back(startNode);

        while (!stack.empty()) {
            NodeHandle current = stack.back();
            stack.pop_back();

            if (!nodes.isValid(current)) continue;
            if (current == targetNode) return true;

            if (current.index >= cap) continue;
            if (m_visitedScratch[current.index]) continue;
            m_visitedScratch[current.index] = 1;

            const Node* node = nodes.get(current);
            if (!node) continue;

            for (PinHandle outPinHandle : node->outputs) {
                const Pin* outPin = pins.get(outPinHandle);
                if (!outPin) continue;

                for (LinkHandle lh : outPin->links) {
                    if (!lh.isValid()) continue;
                    const Link* link = links.get(lh);
                    if (!link) continue;

                    const Pin* endPin = pins.get(link->endPin);
                    if (endPin && endPin->node.isValid()) {
                        stack.push_back(endPin->node);
                    }
                }
            }
        }
        return false;
    }

    // Step 3: Topological Sorting (Kahn's Algorithm)
    void computeTopologicalOrder() {
        topoOrder.clear();
        uint32_t activeNodeCount = nodes.size();
        if (activeNodeCount == 0) return;

        std::vector<int> inDegree(nodes.capacity(), 0);

        // Populate in-degree
        nodes.forEach([&](NodeHandle h, const Node& node) {
            int count = 0;
            for (PinHandle inPinHandle : node.inputs) {
                const Pin* pin = pins.get(inPinHandle);
                if (pin && pin->link.isValid()) {
                    count++;
                }
            }
            inDegree[h.index] = count;
        });

        std::queue<NodeHandle> queue;
        nodes.forEach([&](NodeHandle h, const Node& node) {
            if (inDegree[h.index] == 0) {
                queue.push(h);
            }
        });

        while (!queue.empty()) {
            NodeHandle uHandle = queue.front();
            queue.pop();
            topoOrder.push_back(uHandle);

            const Node* uNode = nodes.get(uHandle);
            if (!uNode) continue;

            for (PinHandle outPinHandle : uNode->outputs) {
                const Pin* outPin = pins.get(outPinHandle);
                if (!outPin) continue;

                for (LinkHandle lh : outPin->links) {
                    const Link* link = links.get(lh);
                    if (!link) continue;

                    const Pin* vPin = pins.get(link->endPin);
                    if (vPin) {
                        NodeHandle vHandle = vPin->node;
                        inDegree[vHandle.index]--;
                        if (inDegree[vHandle.index] == 0) {
                            queue.push(vHandle);
                        }
                    }
                }
            }
        }

        // Defensive Invariant
        assert(topoOrder.size() == activeNodeCount);
    }

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
        if (dir == PinDirection::Input)
            node->inputs.push_back(h);
        else
            node->outputs.push_back(h);
        return h;
    }
};

}  // namespace loom::core
