#include "core/Nodes.hpp"

#include <glm/vec3.hpp>

#include "core/Assert.hpp"
#include "core/DeepLayout.hpp"
#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Log.hpp"
#include "core/NodeTaskBuilders.hpp"
#include "core/Param.hpp"
#include "core/RenderCache.hpp"
#include "gpu/DeepUpload.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/StagingArena.hpp"
#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "io/DeepReader.hpp"

namespace loom::core {

namespace {

gpu::ImageSpec defaultColorSpec(VkExtent2D extent) {
    return gpu::ImageSpec{VK_FORMAT_R32G32B32A32_SFLOAT, extent,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
}

gpu::ImageSpec colorSpec(uint32_t width, uint32_t height) {
    return defaultColorSpec({width, height});
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

gpu::ResourceRef::DeepRef Node::pullDeepInput(EvaluationContext& ctx, const Region& region,
                                              uint32_t inputIndex) {
    gpu::ResourceRef ref = pullInput(ctx, region, inputIndex);
    if (!ref.isValid()) return {};
    LOOM_ASSERT(ref.kind == gpu::ResourceRef::Kind::Deep,
                "pullDeepInput called on a non-deep pin payload");
    return ref.deep;
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

// -----------------------------------------------------------------------------
// DeepEXRReadNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> DeepEXRReadNode::getPinSchema() const {
    return {{PinDirection::Output, PinType::DeepBuffer}};
}

void DeepEXRReadNode::buildParams() {
    params.emplace_back("file_path", Param::Value{std::string("")});
    ParamRange r;
    r.min = 0.0f;
    r.max = 9999.0f;
    r.step = 1.0f;
    r.hasBounds = true;
    params.emplace_back("frame_index", Param::Value{0}, r);
}

void DeepEXRReadNode::markRequiredTiles(const Region& /*requestedRegion*/,
                                        std::unordered_set<NodeHandle>& activeNodes) {
    // Pure source — no upstream to mark.
    activeNodes.insert(id);
}

namespace {

const std::string& stringParam(const Node& node, const char* name) {
    static const std::string empty;
    for (const auto& p : node.params) {
        if (p.name() != name) continue;
        if (auto* v = std::get_if<std::string>(&p.value())) return *v;
        return empty;
    }
    return empty;
}

int intParam(const Node& node, const char* name, int fallback) {
    for (const auto& p : node.params) {
        if (p.name() != name) continue;
        if (auto* v = std::get_if<int>(&p.value())) return *v;
        return fallback;
    }
    return fallback;
}

}  // namespace

void DeepEXRReadNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    const std::string& path = stringParam(*this, "file_path");
    const int frameIndex = intParam(*this, "frame_index", 0);

    if (path.empty()) {
        // No path set — emit an invalid deep ref so downstream nodes know to
        // fall back to whatever placeholder behaviour they prefer. Logging at
        // info, not warn: an unconfigured node is a normal first-frame state.
        log::info("DeepEXRReadNode: file_path is empty, skipping upload");
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromDeep({}));
        return;
    }

    if (!ctx.deepReader || !ctx.bufferPool || !ctx.stagingArena) {
        log::warn(
            "DeepEXRReadNode: evaluation context missing reader / bufferPool / "
            "stagingArena — cannot execute");
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromDeep({}));
        return;
    }

    // v1 reader is synchronous; .get() returns immediately. Phase C.1 swaps
    // a worker-thread reader behind this same call.
    io::DeepFrame frame = ctx.deepReader->readFrame(path, frameIndex).get();
    if (!frame.isValid()) {
        log::warn("DeepEXRReadNode: failed to read '", path, "'");
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromDeep({}));
        return;
    }

    gpu::ResourceRef ref = gpu::uploadDeepImage(ctx.cmd, *ctx.stagingArena, *ctx.imagePool,
                                                *ctx.bufferPool, frame.image, *frame.layout);
    ctx.renderCache->store(outputs[0], region, ref);
}

// -----------------------------------------------------------------------------
// DeepFlattenNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> DeepFlattenNode::getPinSchema() const {
    return {{PinDirection::Input, PinType::DeepBuffer}, {PinDirection::Output, PinType::Float}};
}

void DeepFlattenNode::buildParams() {
    // v1 has no user-facing knobs. Future: sample-sort toggle, max-samples,
    // composite-mode dropdown.
}

void DeepFlattenNode::markRequiredTiles(const Region& requestedRegion,
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

namespace {

// v1 layout check: OpenEXR enumerates header channels alphabetically, so
// `DeepReader` builds layouts in that order. For the standard 6-channel
// deep EXR (`A, B, G, R` half + `Z, ZBack` float) the offsets are:
//   A      offset  0  (Float16, 2 bytes)
//   B      offset  2  (Float16, 2 bytes)
//   G      offset  4  (Float16, 2 bytes)
//   R      offset  6  (Float16, 2 bytes)
//   Z      offset  8  (Float32, 4 bytes)
//   ZBack  offset 12  (Float32, 4 bytes)
//   stride          16
//
// `DeepFlatten.comp` hardcodes these offsets via the corresponding
// `uintBitsToFloat` / `unpackHalf2x16` reads. Phase D introduces a
// layout-flexible shader that takes per-channel byte offsets via push
// constants — at that point this check goes away.
bool layoutMatchesV1Flatten(const core::DeepLayout& layout) {
    if (layout.stride() != 16) return false;
    if (layout.byteOffset("A") != 0) return false;
    if (layout.byteOffset("B") != 2) return false;
    if (layout.byteOffset("G") != 4) return false;
    if (layout.byteOffset("R") != 6) return false;
    if (layout.byteOffset("Z") != 8) return false;
    if (layout.byteOffset("ZBack") != 12) return false;
    return true;
}

}  // namespace

void DeepFlattenNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    gpu::ResourceRef::DeepRef src = pullDeepInput(ctx, region, 0);

    if (!src.layout || !src.samples.isValid() || !src.countImage.isValid()) {
        // Upstream produced an empty deep payload (e.g. the read node had no
        // file_path). Store an invalid image ref and let the viewer's
        // existing fallback path render nothing.
        ctx.renderCache->store(outputs[0], region, {});
        return;
    }

    if (!layoutMatchesV1Flatten(*src.layout)) {
        log::warn(
            "DeepFlattenNode: input layout doesn't match the v1 standard "
            "(Z + ZBack + RGBA half, stride 16) — skipping dispatch");
        ctx.renderCache->store(outputs[0], region, {});
        return;
    }

    gpu::ImageHandle out = ctx.imagePool->acquire(colorSpec(src.width, src.height));
    if (!out.isValid()) {
        log::warn("DeepFlattenNode: imagePool exhausted");
        ctx.renderCache->store(outputs[0], region, {});
        return;
    }

    ctx.tasks.push_back(buildDeepFlattenTask(ctx, src, out, "DeepFlattenNode.composite"));
    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(out));
}

}  // namespace loom::core
