#include "core/Nodes.hpp"

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/NodeTaskBuilders.hpp"
#include "core/RenderCache.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::core {

namespace {

gpu::ImageSpec defaultColorSpec(VkExtent2D extent) {
    return gpu::ImageSpec{VK_FORMAT_R32G32B32A32_SFLOAT, extent,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
}

}  // namespace

gpu::ImageHandle Node::pullInput(EvaluationContext& ctx, const Region& region,
                                 uint32_t inputIndex) {
    if (!graph || inputIndex >= inputs.size()) return {};

    PinHandle inPinHandle = inputs[inputIndex];
    Pin* inPin = graph->getPin(inPinHandle);
    if (!inPin || !inPin->link.isValid()) return {};

    Link* link = graph->getLink(inPin->link);
    if (!link) return {};

    PinHandle srcPinHandle = link->startPin;
    return ctx.renderCache->retrieve(srcPinHandle, region);
}

// -----------------------------------------------------------------------------
// ConstantNode
// -----------------------------------------------------------------------------

void ConstantNode::markRequiredTiles(const Region& /*requestedRegion*/,
                                     std::unordered_set<NodeHandle>& activeNodes) {
    activeNodes.insert(id);
}

void ConstantNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    gpu::ImageHandle handle = ctx.imagePool->acquire(defaultColorSpec(ctx.requestedExtent));
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    ctx.tasks.push_back(buildFillTask(ctx, handle, red, "ConstantNode.fill"));
    ctx.renderCache->store(outputs[0], region, handle);
}

// -----------------------------------------------------------------------------
// MergeNode
// -----------------------------------------------------------------------------

void MergeNode::markRequiredTiles(const Region& requestedRegion,
                                  std::unordered_set<NodeHandle>& activeNodes) {
    if (activeNodes.count(id)) return;
    activeNodes.insert(id);

    for (auto inPinHandle : inputs) {
        Pin* inPin = graph->getPin(inPinHandle);
        if (inPin && inPin->link.isValid()) {
            Link* link = graph->getLink(inPin->link);
            Pin* srcPin = graph->getPin(link->startPin);
            Node* srcNode = graph->getNode(srcPin->node);
            srcNode->markRequiredTiles(requestedRegion, activeNodes);
        }
    }
}

void MergeNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    gpu::ImageHandle in1 = pullInput(ctx, region, 0);
    gpu::ImageHandle in2 = pullInput(ctx, region, 1);

    if (in1.isValid() && !in2.isValid()) {
        ctx.renderCache->store(outputs[0], region, in1);
        return;
    }
    if (!in1.isValid() && in2.isValid()) {
        ctx.renderCache->store(outputs[0], region, in2);
        return;
    }

    gpu::ImageHandle handle = ctx.imagePool->acquire(defaultColorSpec(ctx.requestedExtent));

    if (in1.isValid() && in2.isValid()) {
        const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
        gpu::ComputeTask task = buildFillTask(ctx, handle, magenta, "MergeNode.fill");
        task.readDependencies.push_back(in1);
        task.readDependencies.push_back(in2);
        ctx.tasks.push_back(std::move(task));
    } else {
        const float grey[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        ctx.tasks.push_back(buildFillTask(ctx, handle, grey, "MergeNode.fill"));
    }
    ctx.renderCache->store(outputs[0], region, handle);
}

// -----------------------------------------------------------------------------
// ViewerNode
// -----------------------------------------------------------------------------

void ViewerNode::markRequiredTiles(const Region& requestedRegion,
                                   std::unordered_set<NodeHandle>& activeNodes) {
    if (activeNodes.count(id)) return;
    activeNodes.insert(id);

    if (!inputs.empty()) {
        Pin* inPin = graph->getPin(inputs[0]);
        if (inPin && inPin->link.isValid()) {
            Link* link = graph->getLink(inPin->link);
            Pin* srcPin = graph->getPin(link->startPin);
            Node* srcNode = graph->getNode(srcPin->node);
            srcNode->markRequiredTiles(requestedRegion, activeNodes);
        }
    }
}

void ViewerNode::execute(EvaluationContext& ctx, const Region& region) {
    lastOutput = pullInput(ctx, region, 0);
}

// -----------------------------------------------------------------------------
// PassthroughNode
// -----------------------------------------------------------------------------

void PassthroughNode::markRequiredTiles(const Region& requestedRegion,
                                        std::unordered_set<NodeHandle>& activeNodes) {
    if (activeNodes.count(id)) return;
    activeNodes.insert(id);

    if (!inputs.empty()) {
        Pin* inPin = graph->getPin(inputs[0]);
        if (inPin && inPin->link.isValid()) {
            Link* link = graph->getLink(inPin->link);
            Pin* srcPin = graph->getPin(link->startPin);
            Node* srcNode = graph->getNode(srcPin->node);
            srcNode->markRequiredTiles(requestedRegion, activeNodes);
        }
    }
}

void PassthroughNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    gpu::ImageHandle in = pullInput(ctx, region, 0);
    gpu::ImageHandle out = ctx.imagePool->acquire(defaultColorSpec(ctx.requestedExtent));

    if (in.isValid()) {
        ctx.tasks.push_back(buildPassthroughTask(ctx, in, out, "PassthroughNode.copy"));
    } else {
        const float grey[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        ctx.tasks.push_back(buildFillTask(ctx, out, grey, "PassthroughNode.fill"));
    }
    ctx.renderCache->store(outputs[0], region, out);
}

}  // namespace loom::core
