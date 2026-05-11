#pragma once

#include <iostream>

#include "core/EvaluationContext.hpp"
#include "core/Types.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::core {

class ConstantNode : public Node {
  public:
    ConstantNode(NodeHandle h, std::string n) : Node(h, NodeType::Constant, std::move(n)) {}
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

class MergeNode : public Node {
  public:
    MergeNode(NodeHandle h, std::string n) : Node(h, NodeType::Merge, std::move(n)) {}
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

class ViewerNode : public Node {
  public:
    gpu::ImageHandle lastOutput;
    ViewerNode(NodeHandle h, std::string n) : Node(h, NodeType::Viewer, std::move(n)) {}
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

class PassthroughNode : public Node {
  public:
    PassthroughNode(NodeHandle h, std::string n) : Node(h, NodeType::Passthrough, std::move(n)) {}
    void markRequiredTiles(const Region& requestedRegion,
                           std::unordered_set<NodeHandle>& activeNodes) override;
    void execute(EvaluationContext& ctx, const Region& region) override;
};

}  // namespace loom::core
