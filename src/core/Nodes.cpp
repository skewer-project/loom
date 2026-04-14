#include <iostream>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Types.hpp"

namespace loom::core {

gpu::ImageHandle Node::pullInput(EvaluationContext& ctx, uint32_t inputIndex) {
    if (!graph || inputIndex >= inputs.size()) return {};

    PinHandle inPinHandle = inputs[inputIndex];
    Pin* inPin = graph->getPin(inPinHandle);
    if (!inPin || !inPin->link.isValid()) return {};

    Link* link = graph->getLink(inPin->link);
    if (!link) return {};

    PinHandle srcPinHandle = link->startPin;
    Pin* srcPin = graph->getPin(srcPinHandle);
    if (!srcPin) return {};

    NodeHandle srcNodeHandle = srcPin->node;
    Node* srcNode = graph->getNode(srcNodeHandle);
    if (!srcNode) return {};

    uint64_t key = pinKey(srcPinHandle);

    // Cache Hit
    if (!srcNode->isDirty) {
        auto it = ctx.outputCache.find(key);
        if (it != ctx.outputCache.end()) return it->second;
    }

    // Cache Eviction
    for (PinHandle outPinHandle : srcNode->outputs) {
        uint64_t outKey = pinKey(outPinHandle);
        auto it = ctx.outputCache.find(outKey);
        if (it != ctx.outputCache.end()) {
            ctx.pendingImageReleases.push_back(it->second);
            ctx.outputCache.erase(it);
        }
    }

    // Re-entrancy Guard
    if (srcNode->isEvaluating) {
        std::cerr << "Cycle violation detected!" << std::endl;
        return {};
    }

    struct EvalGuard {
        Node* n;
        EvalGuard(Node* n) : n(n) { n->isEvaluating = true; }
        ~EvalGuard() { n->isEvaluating = false; }
    } guard(srcNode);

    srcNode->evaluate(ctx);
    srcNode->isDirty = false;

    return ctx.outputCache[key];
}

}  // namespace loom::core
