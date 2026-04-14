#pragma once

#include <iostream>

#include "core/EvaluationContext.hpp"
#include "core/Types.hpp"
#include "gpu/TransientImagePool.hpp"

namespace loom::core {

class ConstantNode : public Node {
  public:
    ConstantNode(NodeHandle h, std::string n) : Node(h, NodeType::Constant, std::move(n)) {}

    void evaluate(EvaluationContext& ctx) override {
        // Implementation will follow in Graph.hpp or separate cpp
    }
};

class MergeNode : public Node {
  public:
    MergeNode(NodeHandle h, std::string n) : Node(h, NodeType::Merge, std::move(n)) {}

    void evaluate(EvaluationContext& ctx) override {
        // Implementation will follow
    }
};

class ViewerNode : public Node {
  public:
    gpu::ImageHandle lastOutput;
    ViewerNode(NodeHandle h, std::string n) : Node(h, NodeType::Viewer, std::move(n)) {}

    void evaluate(EvaluationContext& ctx) override {
        // Implementation will follow
    }
};

class PassthroughNode : public Node {
  public:
    PassthroughNode(NodeHandle h, std::string n) : Node(h, NodeType::Passthrough, std::move(n)) {}

    void evaluate(EvaluationContext& ctx) override {
        // Implementation will follow
    }
};

}  // namespace loom::core
