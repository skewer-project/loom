#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "core/Nodes.hpp"
#include "core/RenderCache.hpp"
#include "core/SlotMap.hpp"
#include "core/Types.hpp"

namespace loom::core {

class Graph {
  public:
    NodeHandle addNode(NodeType type, std::string name = "") {
        if (name.empty()) {
            name = getDefaultNodeName(type);
        }

        NodeHandle nodeHandle = nodes.emplace(nullptr);
        std::unique_ptr<Node> node;
        switch (type) {
            case NodeType::Constant:
                node = std::make_unique<ConstantNode>(nodeHandle, name);
                break;
            case NodeType::Merge:
                node = std::make_unique<MergeNode>(nodeHandle, name);
                break;
            case NodeType::Viewer:
                node = std::make_unique<ViewerNode>(nodeHandle, name);
                break;
            case NodeType::Passthrough:
                node = std::make_unique<PassthroughNode>(nodeHandle, name);
                break;
            case NodeType::DeepRead:
                node = std::make_unique<DeepReadNode>(nodeHandle, name);
                break;
            default:
                throw std::runtime_error("Unknown node type");
        }
        node->graph = this;
        *nodes.get(nodeHandle) = std::move(node);

        Node* nodePtr = nodes.get(nodeHandle)->get();
        setupNodePins(nodePtr);

        isTopoDirty = true;
        return nodeHandle;
    }

    void removeNode(NodeHandle nodeHandle) {
        Node* node = getNode(nodeHandle);
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
        if (!canAddLink(startPinHandle, endPinHandle)) return false;

        Pin* endPin = pins.get(endPinHandle);
        if (endPin->link.isValid()) {
            removeLink(endPin->link);
        }

        LinkHandle linkHandle = links.emplace(LinkHandle(), startPinHandle, endPinHandle);
        Link* link = links.get(linkHandle);
        link->id = linkHandle;

        Pin* startPin = pins.get(startPinHandle);
        endPin->link = linkHandle;
        startPin->links.push_back(linkHandle);

        markDirty(endPin->node);

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
            markDirty(endPin->node);
        }

        links.remove(linkHandle);
        isTopoDirty = true;
    }

    void markDirty(NodeHandle startNodeHandle) {
        Node* startNode = getNode(startNodeHandle);
        if (!startNode || startNode->isDirty) return;

        std::queue<NodeHandle> q;
        q.push(startNodeHandle);

        while (!q.empty()) {
            NodeHandle currentHandle = q.front();
            q.pop();

            Node* currentNode = getNode(currentHandle);
            if (!currentNode) continue;

            currentNode->isDirty = true;

            for (PinHandle outPinHandle : currentNode->outputs) {
                Pin* outPin = pins.get(outPinHandle);
                if (!outPin) continue;

                for (LinkHandle lh : outPin->links) {
                    Link* link = links.get(lh);
                    if (!link) continue;

                    Pin* nextPin = pins.get(link->endPin);
                    if (nextPin) {
                        NodeHandle nextNodeHandle = nextPin->node;
                        Node* nextNode = getNode(nextNodeHandle);
                        if (nextNode && !nextNode->isDirty) {
                            q.push(nextNodeHandle);
                        }
                    }
                }
            }
        }
    }

    const std::vector<NodeHandle>& getTopologicalOrder() {
        if (isTopoDirty) {
            std::unordered_set<NodeHandle> allNodes;
            forEachNode([&](NodeHandle h, Node&) { allNodes.insert(h); });
            computeTopologicalOrder(allNodes);
            isTopoDirty = false;
        }
        return topoOrder;
    }

    void execute(EvaluationContext& ctx, const Region& region) {
        std::unordered_set<NodeHandle> activeNodes;

        // Pass 1: Mark
        forEachNode([&](NodeHandle h, Node& node) {
            if (node.type == NodeType::Viewer) {
                node.markRequiredTiles(region, activeNodes);
            }
        });

        if (activeNodes.empty()) return;

        // Pass 2: Sort
        computeTopologicalOrder(activeNodes);

        // Pass 3: Execute
        for (NodeHandle h : topoOrder) {
            Node* node = getNode(h);
            if (node) {
                if (!node->isDirty && ctx.renderCache->hasValidData(node, region)) {
                    continue;
                }
                node->execute(ctx, region);
                node->isDirty = false;
            }
        }
    }

    NodeHandle getNodeHandleByIndex(uint32_t index) const { return nodes.getHandleByIndex(index); }
    PinHandle getPinHandleByIndex(uint32_t index) const { return pins.getHandleByIndex(index); }
    LinkHandle getLinkHandleByIndex(uint32_t index) const { return links.getHandleByIndex(index); }

    bool canAddLink(PinHandle startPinHandle, PinHandle endPinHandle) const {
        const Pin* startPin = pins.get(startPinHandle);
        const Pin* endPin = pins.get(endPinHandle);

        if (!startPin || !endPin) return false;
        if (startPin->direction != PinDirection::Output) return false;
        if (endPin->direction != PinDirection::Input) return false;
        if (startPin->type != endPin->type) return false;

        NodeHandle nodeA = startPin->node;
        NodeHandle nodeB = endPin->node;

        if (nodeA == nodeB) return false;
        if (isReachable(nodeB, nodeA)) return false;

        return true;
    }

    Node* getNode(NodeHandle h) {
        auto ptr = nodes.get(h);
        return ptr ? ptr->get() : nullptr;
    }
    const Node* getNode(NodeHandle h) const {
        auto ptr = nodes.get(h);
        return ptr ? ptr->get() : nullptr;
    }
    Pin* getPin(PinHandle h) { return pins.get(h); }
    const Pin* getPin(PinHandle h) const { return pins.get(h); }
    Link* getLink(LinkHandle h) { return links.get(h); }
    const Link* getLink(LinkHandle h) const { return links.get(h); }

    void forEachNode(std::function<void(NodeHandle, Node&)> cb) {
        nodes.forEach([&](NodeHandle h, std::unique_ptr<Node>& ptr) {
            if (ptr) cb(h, *ptr);
        });
    }

    void forEachNode(std::function<void(NodeHandle, const Node&)> cb) const {
        nodes.forEach([&](NodeHandle h, const std::unique_ptr<Node>& ptr) {
            if (ptr) cb(h, *ptr);
        });
    }

    void forEachLink(std::function<void(LinkHandle, Link&)> cb) { links.forEach(cb); }

    std::string getPinLabel(PinHandle h) const {
        const Pin* pin = pins.get(h);
        if (!pin) return "Unknown";

        std::string label;
        switch (pin->type) {
            case PinType::Float:
                label = "[Float] ";
                break;
            case PinType::DeepBuffer:
                label = "[Deep] ";
                break;
        }

        // Simple heuristic for labels if not explicitly named
        if (pin->direction == PinDirection::Input)
            label += "In";
        else
            label += "Out";

        return label;
    }

  private:
    SlotMap<std::unique_ptr<Node>, NodeHandle> nodes;
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

            const Node* node = getNode(current);
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
    void computeTopologicalOrder(const std::unordered_set<NodeHandle>& activeNodes) {
        topoOrder.clear();
        if (activeNodes.empty()) return;

        std::unordered_map<uint32_t, int> inDegree;
        std::queue<NodeHandle> queue;

        // Initialize in-degree for active nodes
        for (auto h : activeNodes) {
            const Node* node = getNode(h);
            int count = 0;
            for (PinHandle inPinHandle : node->inputs) {
                const Pin* pin = pins.get(inPinHandle);
                if (pin && pin->link.isValid()) {
                    Link* link = links.get(pin->link);
                    Pin* srcPin = pins.get(link->startPin);
                    if (activeNodes.count(srcPin->node)) {
                        count++;
                    }
                }
            }
            inDegree[h.index] = count;
            if (count == 0) {
                queue.push(h);
            }
        }

        while (!queue.empty()) {
            NodeHandle uHandle = queue.front();
            queue.pop();
            topoOrder.push_back(uHandle);

            const Node* uNode = getNode(uHandle);
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
                        if (activeNodes.count(vHandle)) {
                            inDegree[vHandle.index]--;
                            if (inDegree[vHandle.index] == 0) {
                                queue.push(vHandle);
                            }
                        }
                    }
                }
            }
        }

        // Defensive Invariant: All active nodes should be in the order
        assert(topoOrder.size() == activeNodes.size());
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
