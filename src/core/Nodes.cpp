#include "core/Nodes.hpp"

#include <glm/vec3.hpp>

#include "core/Assert.hpp"
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

// Lookup a vec3 param by name. Returns the default if the param is missing
// or the wrong type — keeps node `execute` bodies linear and avoids burying
// a LOOM_ASSERT for a soft contract.
glm::vec3 vec3Param(const Node& node, const char* name, glm::vec3 fallback) {
    for (const auto& p : node.params) {
        if (p.name() != name) continue;
        if (auto* v = std::get_if<glm::vec3>(&p.value())) return *v;
        return fallback;
    }
    return fallback;
}

}  // namespace

crude_json::value Node::paramsToJson() const {
    crude_json::array arr;
    for (const auto& p : params) arr.push_back(p.toJson());
    return crude_json::value(std::move(arr));
}

void Node::paramsFromJson(const crude_json::value& j) {
    if (!j.is_array()) return;
    const auto& a = j.get<crude_json::array>();
    // Match incoming entries against existing params by name. Unknown
    // names are ignored (forward-compatibility: a future build may have
    // dropped a param). Missing names keep their current value.
    for (const auto& entry : a) {
        Param decoded = Param::fromJson(entry);
        for (auto& existing : params) {
            if (existing.name() == decoded.name()) {
                existing = std::move(decoded);
                break;
            }
        }
    }
}

gpu::ResourceRef Node::pullInput(EvaluationContext& ctx, const Region& region,
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

gpu::ImageHandle Node::pullImageInput(EvaluationContext& ctx, const Region& region,
                                      uint32_t inputIndex) {
    gpu::ResourceRef ref = pullInput(ctx, region, inputIndex);
    if (!ref.isValid()) return {};
    LOOM_ASSERT(ref.kind == gpu::ResourceRef::Kind::Image,
                "pullImageInput called on a non-image pin payload");
    return ref.image;
}

// -----------------------------------------------------------------------------
// ConstantNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> ConstantNode::getPinSchema() const {
    return {{PinDirection::Output, PinType::Float}};
}

void ConstantNode::buildParams() {
    ParamRange unit;
    unit.min = 0.0f;
    unit.max = 1.0f;
    unit.step = 0.01f;
    unit.hasBounds = true;
    params.emplace_back("color", Param::Value{glm::vec3(1.0f, 0.0f, 0.0f)}, unit);
}

void ConstantNode::markRequiredTiles(const Region& /*requestedRegion*/,
                                     std::unordered_set<NodeHandle>& activeNodes) {
    activeNodes.insert(id);
}

void ConstantNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    gpu::ImageHandle handle = ctx.imagePool->acquire(defaultColorSpec(ctx.requestedExtent));
    const glm::vec3 c = vec3Param(*this, "color", glm::vec3(1.0f, 0.0f, 0.0f));
    const float rgba[4] = {c.r, c.g, c.b, 1.0f};
    ctx.tasks.push_back(buildFillTask(ctx, handle, rgba, "ConstantNode.fill"));
    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(handle));
}

// -----------------------------------------------------------------------------
// MergeNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> MergeNode::getPinSchema() const {
    return {{PinDirection::Input, PinType::Float},
            {PinDirection::Input, PinType::Float},
            {PinDirection::Output, PinType::Float}};
}

void MergeNode::buildParams() {
    ParamRange unit;
    unit.min = 0.0f;
    unit.max = 1.0f;
    unit.step = 0.01f;
    unit.hasBounds = true;
    params.emplace_back("mergeColor", Param::Value{glm::vec3(1.0f, 0.0f, 1.0f)}, unit);
}

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

    gpu::ImageHandle in1 = pullImageInput(ctx, region, 0);
    gpu::ImageHandle in2 = pullImageInput(ctx, region, 1);

    if (in1.isValid() && !in2.isValid()) {
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(in1));
        return;
    }
    if (!in1.isValid() && in2.isValid()) {
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(in2));
        return;
    }

    gpu::ImageHandle handle = ctx.imagePool->acquire(defaultColorSpec(ctx.requestedExtent));

    if (in1.isValid() && in2.isValid()) {
        const glm::vec3 c = vec3Param(*this, "mergeColor", glm::vec3(1.0f, 0.0f, 1.0f));
        const float rgba[4] = {c.r, c.g, c.b, 1.0f};
        gpu::ComputeTask task = buildFillTask(ctx, handle, rgba, "MergeNode.fill");
        task.readDependencies.push_back(in1);
        task.readDependencies.push_back(in2);
        ctx.tasks.push_back(std::move(task));
    } else {
        const float grey[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        ctx.tasks.push_back(buildFillTask(ctx, handle, grey, "MergeNode.fill"));
    }
    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(handle));
}

// -----------------------------------------------------------------------------
// ViewerNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> ViewerNode::getPinSchema() const {
    return {{PinDirection::Input, PinType::Float}};
}

void ViewerNode::buildParams() {
    // v1 viewer has no user-facing knobs. The override exists so the Param
    // serialiser sees a stable empty-array entry for every node — load-time
    // shape parity matters more than the absence of values.
}

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
    lastOutput = pullImageInput(ctx, region, 0);
}

// -----------------------------------------------------------------------------
// PassthroughNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> PassthroughNode::getPinSchema() const {
    return {{PinDirection::Input, PinType::Float}, {PinDirection::Output, PinType::Float}};
}

void PassthroughNode::buildParams() {
    // v1 passthrough has no user-facing knobs (no gain / colour-correct
    // controls). A `bypass: bool` parameter would belong here once a
    // production caller needs it.
}

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

    gpu::ImageHandle in = pullImageInput(ctx, region, 0);
    gpu::ImageHandle out = ctx.imagePool->acquire(defaultColorSpec(ctx.requestedExtent));

    if (in.isValid()) {
        ctx.tasks.push_back(buildPassthroughTask(ctx, in, out, "PassthroughNode.copy"));
    } else {
        const float grey[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        ctx.tasks.push_back(buildFillTask(ctx, out, grey, "PassthroughNode.fill"));
    }
    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(out));
}

}  // namespace loom::core
