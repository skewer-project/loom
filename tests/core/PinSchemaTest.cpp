#include <gtest/gtest.h>

#include "core/Graph.hpp"
#include "core/Nodes.hpp"
#include "core/Types.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;

namespace {

// Test-only node that declares a buffer-typed output. Not registered in
// production NodeType — it exercises the type system end-to-end without
// committing to a buffer node in shippable code. PinType::DeepBuffer stands
// in for any non-image kind; canAddLink only checks that the two pin types
// match.
//
// We pick NodeType::Passthrough as the closest existing enum value so the
// Node base class's `type` field is well-formed; the schema we return
// overrides what Graph::setupNodePins creates.
class BufferTestNode : public core::Node {
  public:
    explicit BufferTestNode(core::NodeHandle h)
        : Node(h, core::NodeType::Passthrough, "BufferTest") {}

    [[nodiscard]] std::vector<core::PinSpec> getPinSchema() const override {
        return {{core::PinDirection::Output, core::PinType::DeepBuffer}};
    }

    void markRequiredTiles(const core::Region&, std::unordered_set<core::NodeHandle>&) override {}
    void execute(core::EvaluationContext&, const core::Region&) override {}
};

}  // namespace

TEST(PinSchemaTest, RegisteredNodesUseTheirOwnSchema) {
    // Each concrete node returns its own PinSpec list; the centralised switch
    // is gone. Smoke-test that the pin counts come out right.
    core::Graph graph;

    auto hConst = graph.addNode(core::NodeType::Constant);
    auto hMerge = graph.addNode(core::NodeType::Merge);
    auto hPass = graph.addNode(core::NodeType::Passthrough);
    auto hViewer = graph.addNode(core::NodeType::Viewer);

    EXPECT_EQ(graph.getNode(hConst)->outputs.size(), 1u);
    EXPECT_EQ(graph.getNode(hConst)->inputs.size(), 0u);

    EXPECT_EQ(graph.getNode(hMerge)->inputs.size(), 2u);
    EXPECT_EQ(graph.getNode(hMerge)->outputs.size(), 1u);

    EXPECT_EQ(graph.getNode(hPass)->inputs.size(), 1u);
    EXPECT_EQ(graph.getNode(hPass)->outputs.size(), 1u);

    EXPECT_EQ(graph.getNode(hViewer)->inputs.size(), 1u);
    EXPECT_EQ(graph.getNode(hViewer)->outputs.size(), 0u);
}

TEST(PinSchemaTest, BufferOutputCannotWireToImageInput) {
    // The test-only buffer node has a DeepBuffer-typed output pin. The
    // Passthrough node has Float-typed (image) input pins. canAddLink must
    // reject the link before it ever reaches evaluation.
    //
    // We bypass Graph::addNode for the buffer node because addNode constructs
    // by NodeType enum and BufferTestNode isn't registered there. Instead we
    // build the pin manually using the schema we declared.
    core::Graph graph;

    auto hPass = graph.addNode(core::NodeType::Passthrough);
    auto passIn = graph.getNode(hPass)->inputs[0];

    // Insert a Pin matching the BufferTestNode schema directly via Graph's
    // SlotMap. We don't need the BufferTestNode object itself for the wiring
    // check — canAddLink only inspects the pins. We do still need a valid
    // NodeHandle owner so the pin is well-formed.
    auto hOwner = graph.addNode(core::NodeType::Constant);
    // Manually mutate the Constant's existing output pin to DeepBuffer so we
    // can exercise the type mismatch. (Avoids exposing a "create raw pin"
    // helper on Graph just for this test.)
    auto bufferOutPin = graph.getNode(hOwner)->outputs[0];
    graph.getPin(bufferOutPin)->type = core::PinType::DeepBuffer;

    EXPECT_FALSE(graph.canAddLink(bufferOutPin, passIn));
    EXPECT_FALSE(graph.tryAddLink(bufferOutPin, passIn));
}

TEST(PinSchemaTest, BufferTestNodeReturnsItsSchema) {
    // The schema accessor is exercised by code that doesn't go through
    // Graph::addNode (e.g. introspection tooling, or registered-node-type
    // discovery in a future feature branch).
    BufferTestNode node(core::NodeHandle{});
    const auto schema = node.getPinSchema();
    ASSERT_EQ(schema.size(), 1u);
    EXPECT_EQ(schema[0].direction, core::PinDirection::Output);
    EXPECT_EQ(schema[0].type, core::PinType::DeepBuffer);
}
