#include "core/Nodes.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/RenderCache.hpp"
#include "gpu/ComputeTask.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::core {

gpu::ImageHandle Node::pullInput(EvaluationContext& ctx, uint32_t inputIndex) {
    if (!graph || inputIndex >= inputs.size()) return {};

    PinHandle inPinHandle = inputs[inputIndex];
    Pin* inPin = graph->getPin(inPinHandle);
    if (!inPin || !inPin->link.isValid()) return {};

    Link* link = graph->getLink(inPin->link);
    if (!link) return {};

    PinHandle srcPinHandle = link->startPin;
    // Regions are not fully implemented for tiling yet, so we pass an empty region for now.
    Region r;
    return ctx.renderCache->retrieve(srcPinHandle, r);
}

// -----------------------------------------------------------------------------
// ConstantNode
// -----------------------------------------------------------------------------

void ConstantNode::markRequiredTiles(const Region& requestedRegion,
                                     std::unordered_set<NodeHandle>& activeNodes) {
    activeNodes.insert(id);
    // No inputs to propagate to.
}

void ConstantNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    // For now, we still allocate a full image, but eventually this will be tile-based.
    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle handle = ctx.imagePool->acquire(spec);

    gpu::ComputeTask task{};
    task.label = "ConstantNode.fill";
    task.pipeline = ctx.pipelineCache->getOrCreate("Fill.comp.spv");

    struct {
        float color[4];
        uint32_t outputSlot;
        uint32_t width;
        uint32_t height;
    } pc;
    pc.color[0] = 1.0f;
    pc.color[1] = 0.0f;
    pc.color[2] = 0.0f;
    pc.color[3] = 1.0f;
    pc.outputSlot = handle.bindlessSlot;
    pc.width = ctx.requestedExtent.width;
    pc.height = ctx.requestedExtent.height;

    memcpy(task.pushConstants.data(), &pc, sizeof(pc));
    task.pushConstantSize = sizeof(pc);
    task.groupCountX = (ctx.requestedExtent.width + 15) / 16;
    task.groupCountY = (ctx.requestedExtent.height + 15) / 16;
    task.groupCountZ = 1;
    task.writeDependencies.push_back(handle);

    ctx.tasks.push_back(task);
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

    gpu::ImageHandle in1 = pullInput(ctx, 0);
    gpu::ImageHandle in2 = pullInput(ctx, 1);

    if (in1.isValid() && !in2.isValid()) {
        ctx.renderCache->store(outputs[0], region, in1);
        return;
    }
    if (!in1.isValid() && in2.isValid()) {
        ctx.renderCache->store(outputs[0], region, in2);
        return;
    }

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle handle = ctx.imagePool->acquire(spec);

    gpu::ComputeTask task{};
    task.label = "MergeNode.fill";
    task.pipeline = ctx.pipelineCache->getOrCreate("Fill.comp.spv");

    struct {
        float color[4];
        uint32_t outputSlot;
        uint32_t width;
        uint32_t height;
    } pc;

    if (in1.isValid() && in2.isValid()) {
        pc.color[0] = 1.0f;
        pc.color[1] = 0.0f;
        pc.color[2] = 1.0f;
        pc.color[3] = 1.0f;
        task.readDependencies.push_back(in1);
        task.readDependencies.push_back(in2);
    } else {
        pc.color[0] = 0.1f;
        pc.color[1] = 0.1f;
        pc.color[2] = 0.1f;
        pc.color[3] = 1.0f;
    }

    pc.outputSlot = handle.bindlessSlot;
    pc.width = ctx.requestedExtent.width;
    pc.height = ctx.requestedExtent.height;

    memcpy(task.pushConstants.data(), &pc, sizeof(pc));
    task.pushConstantSize = sizeof(pc);
    task.groupCountX = (ctx.requestedExtent.width + 15) / 16;
    task.groupCountY = (ctx.requestedExtent.height + 15) / 16;
    task.groupCountZ = 1;

    task.writeDependencies.push_back(handle);

    ctx.tasks.push_back(task);
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
    lastOutput = pullInput(ctx, 0);
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

    gpu::ImageHandle in = pullInput(ctx, 0);

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle out = ctx.imagePool->acquire(spec);

    gpu::ComputeTask task{};

    if (in.isValid()) {
        task.label = "PassthroughNode.copy";
        task.pipeline = ctx.pipelineCache->getOrCreate("Passthrough.comp.spv");
        struct {
            uint32_t inputSlot;
            uint32_t outputSlot;
            uint32_t width;
            uint32_t height;
        } pc;
        pc.inputSlot = in.bindlessSlot;
        pc.outputSlot = out.bindlessSlot;
        pc.width = ctx.requestedExtent.width;
        pc.height = ctx.requestedExtent.height;
        memcpy(task.pushConstants.data(), &pc, sizeof(pc));
        task.pushConstantSize = sizeof(pc);
        task.readDependencies.push_back(in);
    } else {
        task.label = "PassthroughNode.fill";
        task.pipeline = ctx.pipelineCache->getOrCreate("Fill.comp.spv");
        struct {
            float color[4];
            uint32_t outputSlot;
            uint32_t width;
            uint32_t height;
        } pc;
        pc.color[0] = 0.1f;
        pc.color[1] = 0.1f;
        pc.color[2] = 0.1f;
        pc.color[3] = 1.0f;
        pc.outputSlot = out.bindlessSlot;
        pc.width = ctx.requestedExtent.width;
        pc.height = ctx.requestedExtent.height;
        memcpy(task.pushConstants.data(), &pc, sizeof(pc));
        task.pushConstantSize = sizeof(pc);
    }

    task.groupCountX = (ctx.requestedExtent.width + 15) / 16;
    task.groupCountY = (ctx.requestedExtent.height + 15) / 16;
    task.groupCountZ = 1;
    task.writeDependencies.push_back(out);

    ctx.tasks.push_back(task);
    ctx.renderCache->store(outputs[0], region, out);
}

}  // namespace loom::core
