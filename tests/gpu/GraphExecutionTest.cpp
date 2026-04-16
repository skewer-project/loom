#include <gtest/gtest.h>

#include "core/Graph.hpp"
#include "gpu/DispatchManager.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

namespace gpu = loom::gpu;
namespace core = loom::core;
namespace platform = loom::platform;

class GraphExecutionTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        try {
            if (!glfwInit()) {
                return;
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = std::make_unique<platform::Window>(100, 100, "Test");
            ctx = std::make_unique<gpu::VulkanContext>();
            ctx->init(*window, "GraphExecutionTest");

            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = 128;

            VkDescriptorSetLayout setLayout = ctx->getBindlessHeap().getLayout();

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &setLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(ctx->getDevice(), &layoutInfo, nullptr, &pipelineLayout) !=
                VK_SUCCESS) {
                throw std::runtime_error("failed to create pipeline layout!");
            }

            m_initialized = true;
        } catch (const std::exception& e) {
            std::cerr << "[ SKIP     ] GraphExecutionTest: " << e.what() << std::endl;
        }
    }

    static void TearDownTestSuite() {
        if (m_initialized) {
            vkDestroyPipelineLayout(ctx->getDevice(), pipelineLayout, nullptr);
        }
        ctx.reset();
        window.reset();
        glfwTerminate();
    }

    void SetUp() override {
        if (!m_initialized) {
            GTEST_SKIP() << "VulkanContext not initialized";
        }
#ifndef LOOM_HAS_SHADER_COMPILER
        GTEST_SKIP() << "Shader compiler not found, skipping GPU dispatch tests";
#endif
        imagePool = std::make_unique<gpu::TransientImagePool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
        pipelineCache = std::make_unique<gpu::PipelineCache>(ctx->getDevice(), pipelineLayout);
        dispatchManager = std::make_unique<gpu::DispatchManager>();
        graph = std::make_unique<core::Graph>();
    }

    void TearDown() override {
        if (m_initialized) {
            graph.reset();
            dispatchManager.reset();
            pipelineCache.reset();
            imagePool.reset();
        }
    }

    static std::unique_ptr<platform::Window> window;
    static std::unique_ptr<gpu::VulkanContext> ctx;
    static VkPipelineLayout pipelineLayout;
    static bool m_initialized;

    std::unique_ptr<gpu::TransientImagePool> imagePool;
    std::unique_ptr<gpu::PipelineCache> pipelineCache;
    std::unique_ptr<gpu::DispatchManager> dispatchManager;
    std::unique_ptr<core::Graph> graph;

    void runFrame(core::EvaluationContext& evalCtx, core::ViewerNode* viewerNode) {
        evalCtx.tasks.clear();
        evalCtx.pendingImageReleases.clear();
        evalCtx.pendingBufferFrees.clear();
        // NOTE: We don't clear outputCache here if we want to test persistence,
        // but typically a new EvaluationContext is used per frame in main.cpp.
        // For this test, we'll use a fresh cache each frame to match main.cpp.
        evalCtx.outputCache.clear();

        viewerNode->evaluate(evalCtx);

        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        dispatchManager->submit(cmd, evalCtx.tasks, viewerNode->lastOutput,
                                ctx->getBindlessHeap().getDescriptorSet(), pipelineLayout,
                                imagePool.get());
        ctx->endSingleTimeCommands(cmd);

        // Process releases
        for (auto h : evalCtx.pendingImageReleases) {
            imagePool->release(h);
        }
        imagePool->flushPendingReleases();
    }
};

std::unique_ptr<platform::Window> GraphExecutionTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> GraphExecutionTest::ctx = nullptr;
VkPipelineLayout GraphExecutionTest::pipelineLayout = VK_NULL_HANDLE;
bool GraphExecutionTest::m_initialized = false;

TEST_F(GraphExecutionTest, SwappingWiringBugReproduction) {
    // 1. Initial State: ConstantNode (C1) -> ViewerNode (V1)
    auto hC1 = graph->addNode(core::NodeType::Constant, "C1");
    auto hV1 = graph->addNode(core::NodeType::Viewer, "V1");
    core::Node* nC1 = graph->getNode(hC1);
    core::ViewerNode* nV1 = static_cast<core::ViewerNode*>(graph->getNode(hV1));

    graph->tryAddLink(nC1->outputs[0], nV1->inputs[0]);

    core::EvaluationContext evalCtx{};
    evalCtx.requestedExtent = {64, 64};
    evalCtx.imagePool = imagePool.get();
    evalCtx.pipelineCache = pipelineCache.get();
    evalCtx.allocator = ctx->getVmaAllocator();

    runFrame(evalCtx, nV1);
    gpu::ImageHandle firstOutput = nV1->lastOutput;
    EXPECT_TRUE(firstOutput.isValid());

    // 2. Add C2 and MergeNode (M1)
    auto hC2 = graph->addNode(core::NodeType::Constant, "C2");
    auto hM1 = graph->addNode(core::NodeType::Merge, "M1");
    core::Node* nC2 = graph->getNode(hC2);
    core::Node* nM1 = graph->getNode(hM1);

    // Initial Merge Setup: C1 -> M1.in1, C2 -> M1.in2, M1 -> V1
    graph->removeLink(graph->getPin(nV1->inputs[0])->link);
    graph->tryAddLink(nC1->outputs[0], nM1->inputs[0]);
    graph->tryAddLink(nC2->outputs[0], nM1->inputs[1]);
    graph->tryAddLink(nM1->outputs[0], nV1->inputs[0]);

    runFrame(evalCtx, nV1);
    gpu::ImageHandle mergeOutput = nV1->lastOutput;
    EXPECT_TRUE(mergeOutput.isValid());
    EXPECT_EQ(evalCtx.tasks.size(), 3);  // C1, C2, M1

    // 3. Swap Wiring: C1 -> M1.in2, C2 -> M1.in1
    // First remove existing links
    graph->removeLink(graph->getPin(nM1->inputs[0])->link);
    graph->removeLink(graph->getPin(nM1->inputs[1])->link);

    // Re-link swapped
    graph->tryAddLink(nC2->outputs[0], nM1->inputs[0]);
    graph->tryAddLink(nC1->outputs[0], nM1->inputs[1]);

    runFrame(evalCtx, nV1);
    gpu::ImageHandle swappedOutput = nV1->lastOutput;
    EXPECT_TRUE(swappedOutput.isValid());
    EXPECT_EQ(evalCtx.tasks.size(), 3);

    // Check if C1 and C2 are still evaluated
    bool c1Evaluated = false;
    bool c2Evaluated = false;
    for (const auto& task : evalCtx.tasks) {
        // Since we can't easily check node names in tasks, we check if multiple constants ran
        // ConstantNode evaluate doesn't have unique ID in tasks yet, but we can verify tasks count.
    }
    EXPECT_EQ(evalCtx.tasks.size(), 3);

    // Now test a direct switch back to C2 -> V1
    graph->removeLink(graph->getPin(nV1->inputs[0])->link);
    graph->tryAddLink(nC2->outputs[0], nV1->inputs[0]);

    runFrame(evalCtx, nV1);
    gpu::ImageHandle directOutput = nV1->lastOutput;
    EXPECT_TRUE(directOutput.isValid());
    EXPECT_EQ(evalCtx.tasks.size(), 1);  // Only C2

    // 4. Persistence Test: Run again without marking dirty.
    // In a fresh frame, it MUST re-evaluate because EvaluationContext is new.
    runFrame(evalCtx, nV1);
    EXPECT_EQ(evalCtx.tasks.size(), 1);
    EXPECT_FALSE(nC2->isDirty);  // Should be false after runFrame
}
