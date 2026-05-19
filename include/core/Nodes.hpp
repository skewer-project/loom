#pragma once

#include "core/EvaluationContext.hpp"
#include "core/Types.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::core {

class ConstantNode : public Node {
  public:
    ConstantNode(NodeHandle h, std::string n) : Node(h, NodeType::Constant, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

class MergeNode : public Node {
  public:
    MergeNode(NodeHandle h, std::string n) : Node(h, NodeType::Merge, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

class ViewerNode : public Node {
  public:
    // The viewer only displays images in v1; multi-kind viewers (deep
    // inspector, buffer inspector) land with their respective node types.
    gpu::ImageHandle lastOutput;
    ViewerNode(NodeHandle h, std::string n) : Node(h, NodeType::Viewer, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

class PassthroughNode : public Node {
  public:
    PassthroughNode(NodeHandle h, std::string n) : Node(h, NodeType::Passthrough, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

// First file-I/O node. Reads a deep EXR via `EvaluationContext::deepReader`
// and stages it onto the GPU via `gpu::uploadDeepImage`. Output pin is
// `Kind::Deep`; downstream consumers in Phase B.3+ (DeepFlatten, deep merge,
// point-cloud passes) accept that payload.
//
// The node holds onto the loaded `DeepFrame` between frames in `m_cache` so
// scrubbing parameters that don't affect the path (e.g. a future
// `frame_index` re-key) re-uploads without re-parsing. The cache is keyed on
// `(path, frame_index)`; a mismatch reparses.
class DeepEXRReadNode : public Node {
  public:
    DeepEXRReadNode(NodeHandle h, std::string n) : Node(h, NodeType::DeepEXRRead, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

// Front-to-back deep composite. Input pin: `Kind::Deep`. Output pin:
// `Kind::Image` (RGBA32F sized to the source deep image, not the viewport).
// First end-to-end deep-visualisation node: `DeepEXRRead → DeepFlatten →
// Viewer → DisplayPass` renders a deep EXR as a flat 2D image.
//
// v1 assumes the standard deep-EXR layout (Z + ZBack + Float16 RGBA, stride
// 16 bytes). `execute` validates the input layout and emits no dispatch on
// a mismatch (with a warn log). Phase D will add layout-flexible variants.
class DeepFlattenNode : public Node {
  public:
    DeepFlattenNode(NodeHandle h, std::string n) : Node(h, NodeType::DeepFlatten, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

// Camera-as-node. Pure source: zero input pins, one output pin of type
// `Kind::Camera`. Materialises a `core::Camera` snapshot from the node's
// param values each frame and stores the resulting `CameraRef` in the render
// cache for 3D renderer nodes (`PointCloudRenderNode`) to consume.
//
// Params (knob-editable in the node editor, JSON-serialisable):
//   - `position` (vec3): camera world position.
//   - `target`   (vec3): point the camera is looking at.
//   - `fov_y_deg` (float, [5, 150]): vertical field of view in degrees.
//   - `near`     (float, [0.001, 1000]): near clip plane.
//   - `far`      (float, [0.01, 100000]): far clip plane.
//
// Aspect ratio is auto-derived from `EvaluationContext::requestedExtent`
// (not a knob) so the camera always matches the active viewport.
// See docs/CONVENTIONS.md §21 for the camera-as-node pattern.
class CameraNode : public Node {
  public:
    CameraNode(NodeHandle h, std::string n) : Node(h, NodeType::Camera, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

// Renders a deep payload as a depth-tested point cloud, driven by an
// upstream `CameraNode`. Input pins: `Kind::Deep` + `Kind::Camera`. Output
// pin: `Kind::Image` (RGBA32F at `EvaluationContext::requestedExtent`).
//
// Params:
//   - `z_scale`    (float, [0.01, 10], default 1.0): multiplies the
//                  synthesised Z when no `world_pos.*` channels are
//                  available. Ignored on NVS deep payloads (those have
//                  real world positions). Per-payload auto-normalisation
//                  uses `DeepRef::recommendedZScale` so default 1.0 fits
//                  the navigable cube cleanly.
//   - `point_size` (float, [1, 20], default 2.0): pixel-space point size
//                  for the splat.
//
// Internally delegates to `gpu::PointCloudPass` (stored on
// `EvaluationContext::pointCloudPass`) — see CONVENTIONS §21 for the
// "node delegates to engine pass" pattern.
class PointCloudRenderNode : public Node {
  public:
    PointCloudRenderNode(NodeHandle h, std::string n)
        : Node(h, NodeType::PointCloudRender, std::move(n)) {}
    [[nodiscard]] std::vector<PinSpec> getPinSchema() const override;
    void buildParams() override;
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

}  // namespace loom::core
