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

}  // namespace loom::core
