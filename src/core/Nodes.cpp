#include "core/Nodes.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
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

void ConstantNode::evaluate(EvaluationContext& ctx) {
    if (outputs.empty()) return;

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle handle = ctx.imagePool->acquire(spec);

    gpu::ComputeTask task{};
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
    ctx.outputCache[pinKey(outputs[0])] = handle;
}

void MergeNode::evaluate(EvaluationContext& ctx) {
    if (outputs.empty()) return;

    gpu::ImageHandle in1 = pullInput(ctx, 0);
    gpu::ImageHandle in2 = pullInput(ctx, 1);

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle handle = ctx.imagePool->acquire(spec);

    // For now, MergeNode also just fills with purple using Fill.comp
    gpu::ComputeTask task{};
    task.pipeline = ctx.pipelineCache->getOrCreate("Fill.comp.spv");

    struct {
        float color[4];
        uint32_t outputSlot;
        uint32_t width;
        uint32_t height;
    } pc;
    pc.color[0] = 1.0f;
    pc.color[1] = 0.0f;
    pc.color[2] = 1.0f;
    pc.color[3] = 1.0f;
    pc.outputSlot = handle.bindlessSlot;
    pc.width = ctx.requestedExtent.width;
    pc.height = ctx.requestedExtent.height;

    memcpy(task.pushConstants.data(), &pc, sizeof(pc));
    task.pushConstantSize = sizeof(pc);
    task.groupCountX = (ctx.requestedExtent.width + 15) / 16;
    task.groupCountY = (ctx.requestedExtent.height + 15) / 16;
    task.groupCountZ = 1;

    if (in1.isValid()) task.readDependencies.push_back(in1);
    if (in2.isValid()) task.readDependencies.push_back(in2);
    task.writeDependencies.push_back(handle);

    ctx.tasks.push_back(task);
    ctx.outputCache[pinKey(outputs[0])] = handle;
}

void ViewerNode::evaluate(EvaluationContext& ctx) { lastOutput = pullInput(ctx, 0); }

void PassthroughNode::evaluate(EvaluationContext& ctx) {
    gpu::ImageHandle in = pullInput(ctx, 0);
    if (outputs.empty() || !in.isValid()) return;

    gpu::ImageSpec spec{};
    spec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    spec.extent = ctx.requestedExtent;
    spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    gpu::ImageHandle out = ctx.imagePool->acquire(spec);

    gpu::ComputeTask task{};
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
    task.groupCountX = (ctx.requestedExtent.width + 15) / 16;
    task.groupCountY = (ctx.requestedExtent.height + 15) / 16;
    task.groupCountZ = 1;
    task.readDependencies.push_back(in);
    task.writeDependencies.push_back(out);

    ctx.tasks.push_back(task);
    ctx.outputCache[pinKey(outputs[0])] = out;
}

}  // namespace loom::core
