#include <gtest/gtest.h>

#include "core/Graph.hpp"
#include "core/RenderCache.hpp"
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
    core::RenderCache renderCache;

    void runFrame(core::EvaluationContext& evalCtx, core::ViewerNode* viewerNode,
                  bool clearCache = true) {
        evalCtx.tasks.clear();
        evalCtx.pendingBufferFrees.clear();
        evalCtx.renderCache = &renderCache;

        if (clearCache) {
            renderCache.clear();
        }

        core::Region region;
        region.tiles.push_back(
            {0, 0, evalCtx.requestedExtent.width, evalCtx.requestedExtent.height});

        graph->execute(evalCtx, region);

        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        dispatchManager->submit(cmd, evalCtx.tasks, viewerNode->lastOutput,
                                ctx->getBindlessHeap().getDescriptorSet(), pipelineLayout,
                                imagePool.get());
        ctx->endSingleTimeCommands(cmd);

        // Process releases
        for (auto h : renderCache.takePendingReleases()) {
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

    // 4. Persistence Test: Run again without marking dirty.
    // We pass clearCache = false to test persistent lazy evaluation.
    runFrame(evalCtx, nV1, false);
    EXPECT_EQ(evalCtx.tasks.size(), 0);  // Should be 0 because nothing is dirty and cache is valid
    EXPECT_FALSE(nC2->isDirty);
}
