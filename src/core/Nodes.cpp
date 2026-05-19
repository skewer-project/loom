#include "core/Nodes.hpp"

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include "core/Assert.hpp"
#include "core/Camera.hpp"
#include "core/DeepLayout.hpp"
#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Log.hpp"
#include "core/NodeTaskBuilders.hpp"
#include "core/Param.hpp"
#include "core/RenderCache.hpp"
#include "gpu/DeepUpload.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/PointCloudPass.hpp"
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

float floatParam(const Node& node, const char* name, float fallback) {
    for (const auto& p : node.params) {
        if (p.name() != name) continue;
        if (auto* v = std::get_if<float>(&p.value())) return *v;
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

gpu::ResourceRef::CameraRef Node::pullCameraInput(EvaluationContext& ctx, const Region& region,
                                                  uint32_t inputIndex) {
    gpu::ResourceRef ref = pullInput(ctx, region, inputIndex);
    if (!ref.isValid()) return {};
    LOOM_ASSERT(ref.kind == gpu::ResourceRef::Kind::Camera,
                "pullCameraInput called on a non-camera pin payload");
    return ref.camera;
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

// v1 layout match. OpenEXR enumerates header channels alphabetically, so
// `DeepReader` builds layouts in that order. We support the two common
// production variants — both share alphabetical `A B G R Z ZBack` ordering;
// they differ only in whether RGBA is Float16 (compact, common in
// game-engine deep) or Float32 (HDR, common in path-traced deep from
// Arnold / V-Ray / Skewer / etc.).
//
//   Half RGBA:  A0 B2 G4 R6 Z8 ZBack12, stride 16
//   Float RGBA: A0 B4 G8 R12 Z16 ZBack20, stride 24
//
// Phase D generalises this to a layout-flexible shader keyed off arbitrary
// per-channel byte offsets via push constants — at that point this check
// shrinks to "does the layout name the required channels" and the
// per-format branch disappears.
enum class V1FlattenFormat {
    Unsupported = 0,
    HalfRGBA = 1,
    FloatRGBA = 2,
};

V1FlattenFormat detectV1FlattenFormat(const core::DeepLayout& layout) {
    if (layout.findChannel("A") < 0 || layout.findChannel("B") < 0 || layout.findChannel("G") < 0 ||
        layout.findChannel("R") < 0 || layout.findChannel("Z") < 0 ||
        layout.findChannel("ZBack") < 0) {
        return V1FlattenFormat::Unsupported;
    }
    if (layout.stride() == 16 && layout.byteOffset("A") == 0 && layout.byteOffset("B") == 2 &&
        layout.byteOffset("G") == 4 && layout.byteOffset("R") == 6 && layout.byteOffset("Z") == 8 &&
        layout.byteOffset("ZBack") == 12) {
        return V1FlattenFormat::HalfRGBA;
    }
    if (layout.stride() == 24 && layout.byteOffset("A") == 0 && layout.byteOffset("B") == 4 &&
        layout.byteOffset("G") == 8 && layout.byteOffset("R") == 12 &&
        layout.byteOffset("Z") == 16 && layout.byteOffset("ZBack") == 20) {
        return V1FlattenFormat::FloatRGBA;
    }
    return V1FlattenFormat::Unsupported;
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

    const V1FlattenFormat fmt = detectV1FlattenFormat(*src.layout);
    if (fmt == V1FlattenFormat::Unsupported) {
        log::warn(
            "DeepFlattenNode: input layout doesn't match the v1 standard "
            "(A B G R Z ZBack, half or float RGBA) — skipping dispatch");
        ctx.renderCache->store(outputs[0], region, {});
        return;
    }

    gpu::ImageHandle out = ctx.imagePool->acquire(colorSpec(src.width, src.height));
    if (!out.isValid()) {
        log::warn("DeepFlattenNode: imagePool exhausted");
        ctx.renderCache->store(outputs[0], region, {});
        return;
    }

    const bool rgbaIsFloat = (fmt == V1FlattenFormat::FloatRGBA);
    ctx.tasks.push_back(
        buildDeepFlattenTask(ctx, src, out, rgbaIsFloat, "DeepFlattenNode.composite"));
    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(out));
}

// -----------------------------------------------------------------------------
// CameraNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> CameraNode::getPinSchema() const {
    return {{PinDirection::Output, PinType::Camera}};
}

void CameraNode::buildParams() {
    // Position / target: vec3 with no bounds (camera can be placed anywhere).
    // Treating these as `ColorEdit3` would be wrong (vec3 with hasBounds opens
    // the colour picker); leave hasBounds=false so the param widget renders
    // as three drag-floats — appropriate for spatial coordinates.
    ParamRange unbounded;
    unbounded.hasBounds = false;
    params.emplace_back("position", Param::Value{glm::vec3(0.0f, 0.0f, 3.0f)}, unbounded);
    params.emplace_back("target", Param::Value{glm::vec3(0.0f, 0.0f, 0.0f)}, unbounded);

    ParamRange fovRange;
    fovRange.min = 5.0f;
    fovRange.max = 150.0f;
    fovRange.step = 0.5f;
    fovRange.hasBounds = true;
    params.emplace_back("fov_y_deg", Param::Value{60.0f}, fovRange);

    ParamRange nearRange;
    nearRange.min = 0.001f;
    nearRange.max = 1000.0f;
    nearRange.step = 0.01f;
    nearRange.hasBounds = true;
    params.emplace_back("near", Param::Value{0.1f}, nearRange);

    ParamRange farRange;
    farRange.min = 0.01f;
    farRange.max = 100000.0f;
    farRange.step = 1.0f;
    farRange.hasBounds = true;
    params.emplace_back("far", Param::Value{100.0f}, farRange);
}

void CameraNode::markRequiredTiles(const Region& /*requestedRegion*/,
                                   std::unordered_set<NodeHandle>& activeNodes) {
    // Pure source: no inputs to recurse into. Just mark self active.
    activeNodes.insert(id);
}

void CameraNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    const glm::vec3 position = vec3Param(*this, "position", glm::vec3(0.0f, 0.0f, 3.0f));
    const glm::vec3 target = vec3Param(*this, "target", glm::vec3(0.0f));
    const float fovDeg = floatParam(*this, "fov_y_deg", 60.0f);
    const float nearP = floatParam(*this, "near", 0.1f);
    const float farP = floatParam(*this, "far", 100.0f);

    // Aspect ratio is not a knob — it auto-derives from the active viewport
    // so the camera always matches what the user sees. Default to 1.0 if
    // the eval context hasn't supplied a sensible extent yet (test harness,
    // first-frame).
    float aspect = 1.0f;
    if (ctx.requestedExtent.height > 0) {
        aspect = static_cast<float>(ctx.requestedExtent.width) /
                 static_cast<float>(ctx.requestedExtent.height);
    }

    Camera cam;
    cam.setPosition(position);
    cam.setTarget(target);
    cam.setFovY(glm::radians(fovDeg));
    cam.setClipPlanes(nearP, farP);
    cam.setAspect(aspect);

    gpu::ResourceRef::CameraRef snapshot;
    snapshot.view = cam.viewMatrix();
    snapshot.proj = cam.projectionMatrix();
    snapshot.eyePos = position;
    snapshot.nearPlane = nearP;
    snapshot.farPlane = farP;
    snapshot.fovY = glm::radians(fovDeg);

    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromCamera(snapshot));
}

// -----------------------------------------------------------------------------
// PointCloudRenderNode
// -----------------------------------------------------------------------------

std::vector<PinSpec> PointCloudRenderNode::getPinSchema() const {
    return {{PinDirection::Input, PinType::DeepBuffer},
            {PinDirection::Input, PinType::Camera},
            {PinDirection::Output, PinType::Float}};
}

void PointCloudRenderNode::buildParams() {
    ParamRange zRange;
    zRange.min = 0.01f;
    zRange.max = 10.0f;
    zRange.step = 0.01f;
    zRange.hasBounds = true;
    params.emplace_back("z_scale", Param::Value{1.0f}, zRange);

    ParamRange sizeRange;
    sizeRange.min = 1.0f;
    sizeRange.max = 20.0f;
    sizeRange.step = 0.5f;
    sizeRange.hasBounds = true;
    params.emplace_back("point_size", Param::Value{2.0f}, sizeRange);
}

void PointCloudRenderNode::markRequiredTiles(const Region& requestedRegion,
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

void PointCloudRenderNode::execute(EvaluationContext& ctx, const Region& region) {
    if (outputs.empty()) return;

    gpu::ResourceRef::DeepRef deep = pullDeepInput(ctx, region, 0);
    gpu::ResourceRef::CameraRef cam = pullCameraInput(ctx, region, 1);

    // Bail to an invalid Image output when either input is missing or the
    // engine context lacks the shared graphics pass / image pool. Matches
    // the "store invalid downstream" pattern used by `DeepEXRReadNode` and
    // `DeepFlattenNode`: downstream nodes see an unconnected upstream and
    // produce their own placeholders.
    if (!deep.samples.isValid() || deep.totalSamples == 0 || !ctx.pointCloudPass ||
        !ctx.imagePool) {
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage({}));
        return;
    }

    // Allocate an RGBA32F transient at the requested viewport extent.
    // Usage matches the other image-producing nodes (`DeepFlatten`) plus
    // `COLOR_ATTACHMENT_BIT` because PointCloudPass binds it as a colour
    // attachment via `vkCmdBeginRendering`.
    gpu::ImageSpec spec{VK_FORMAT_R32G32B32A32_SFLOAT, ctx.requestedExtent,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    gpu::ImageHandle target = ctx.imagePool->acquire(spec);
    if (!target.isValid()) {
        log::warn("PointCloudRenderNode: imagePool exhausted");
        ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage({}));
        return;
    }

    // Combine the per-payload Z-scale hint (computed CPU-side at upload)
    // with the user knob so non-NVS files navigate cleanly out of the box.
    // NVS payloads carry `recommendedZScale = 1.0` so the knob acts on
    // real units; non-NVS files multiply by `2 / extent.z` so the
    // synthesised cube fits.
    const float userZScale = floatParam(*this, "z_scale", 1.0f);
    const float effectiveZScale = userZScale * deep.recommendedZScale;
    const float pointSize = floatParam(*this, "point_size", 2.0f);

    // PointCloudPass leaves the image in SHADER_READ_ONLY_OPTIMAL — fine
    // for the downstream `DisplayPass` consumer (its pre-barrier expects
    // SHADER_READ_ONLY). We must reflect that to the pool so a subsequent
    // re-acquire of the same slot has the right layout state on entry.
    ctx.pointCloudPass->record(ctx.cmd, deep, ctx.imagePool->getImage(target),
                               ctx.imagePool->getView(target), ctx.bindlessSet,
                               ctx.requestedExtent.width, ctx.requestedExtent.height, cam,
                               effectiveZScale, pointSize);
    ctx.imagePool->setLayout(target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(target));
}

}  // namespace loom::core
