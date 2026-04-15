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

class ComputeDispatchTest : public ::testing::Test {
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
            ctx->init(*window, "ComputeDispatchTest");

            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = 128;  // Minimum guaranteed

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
            std::cerr << "[ SKIP     ] ComputeDispatchTest: " << e.what() << std::endl;
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
};

std::unique_ptr<platform::Window> ComputeDispatchTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> ComputeDispatchTest::ctx = nullptr;
VkPipelineLayout ComputeDispatchTest::pipelineLayout = VK_NULL_HANDLE;
bool ComputeDispatchTest::m_initialized = false;

TEST_F(ComputeDispatchTest, TaskGeneration) {
    auto hConst = graph->addNode(core::NodeType::Constant, "Const");
    auto hPass = graph->addNode(core::NodeType::Passthrough, "Pass");
    auto hViewer = graph->addNode(core::NodeType::Viewer, "Viewer");

    core::Node* nConst = graph->getNode(hConst);
    core::Node* nPass = graph->getNode(hPass);
    core::Node* nViewer = graph->getNode(hViewer);

    graph->tryAddLink(nConst->outputs[0], nPass->inputs[0]);
    graph->tryAddLink(nPass->outputs[0], nViewer->inputs[0]);

    core::EvaluationContext evalCtx{};
    evalCtx.requestedExtent = {64, 64};
    evalCtx.imagePool = imagePool.get();
    evalCtx.pipelineCache = pipelineCache.get();
    evalCtx.allocator = ctx->getVmaAllocator();

    // Evaluation starts from the viewer
    nViewer->evaluate(evalCtx);

    // ConstantNode generates 1 task, PassthroughNode generates 1 task.
    // Total 2 tasks.
    EXPECT_EQ(evalCtx.tasks.size(), 2);

    // Check task dependencies
    const auto& task1 = evalCtx.tasks[0];  // Constant
    const auto& task2 = evalCtx.tasks[1];  // Passthrough

    EXPECT_EQ(task1.readDependencies.size(), 0);
    EXPECT_EQ(task1.writeDependencies.size(), 1);

    EXPECT_EQ(task2.readDependencies.size(), 1);
    EXPECT_EQ(task2.writeDependencies.size(), 1);
    EXPECT_EQ(task1.writeDependencies[0].bindlessSlot, task2.readDependencies[0].bindlessSlot);
}

TEST_F(ComputeDispatchTest, RAWHazardBarrier) {
    auto hConst = graph->addNode(core::NodeType::Constant, "Const");
    auto hPass = graph->addNode(core::NodeType::Passthrough, "Pass");
    auto hViewer = graph->addNode(core::NodeType::Viewer, "Viewer");

    core::Node* nConst = graph->getNode(hConst);
    core::Node* nPass = graph->getNode(hPass);
    core::Node* nViewer = graph->getNode(hViewer);

    graph->tryAddLink(nConst->outputs[0], nPass->inputs[0]);
    graph->tryAddLink(nPass->outputs[0], nViewer->inputs[0]);

    core::EvaluationContext evalCtx{};
    evalCtx.requestedExtent = {64, 64};
    evalCtx.imagePool = imagePool.get();
    evalCtx.pipelineCache = pipelineCache.get();
    evalCtx.allocator = ctx->getVmaAllocator();

    nViewer->evaluate(evalCtx);

    VkCommandBuffer cmd = ctx->beginSingleTimeCommands();

    // This submission should include a RAW hazard barrier because task2 reads what task1 writes.
    // If it doesn't, and validation layers are on, it might fire (though C2C hazards are sometimes
    // subtle).
    dispatchManager->submit(cmd, evalCtx.tasks, static_cast<core::ViewerNode*>(nViewer)->lastOutput,
                            ctx->getBindlessHeap().getDescriptorSet(), pipelineLayout,
                            imagePool.get());

    ctx->endSingleTimeCommands(cmd);
}

TEST_F(ComputeDispatchTest, LayoutReentry) {
    auto hConst = graph->addNode(core::NodeType::Constant, "Const");
    auto hViewer = graph->addNode(core::NodeType::Viewer, "Viewer");

    core::Node* nConst = graph->getNode(hConst);
    core::Node* nViewer = graph->getNode(hViewer);

    graph->tryAddLink(nConst->outputs[0], nViewer->inputs[0]);

    core::EvaluationContext evalCtx{};
    evalCtx.requestedExtent = {64, 64};
    evalCtx.imagePool = imagePool.get();
    evalCtx.pipelineCache = pipelineCache.get();
    evalCtx.allocator = ctx->getVmaAllocator();

    graph->getNode(hViewer)->evaluate(evalCtx);

    // Frame 1
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        dispatchManager->submit(
            cmd, evalCtx.tasks, static_cast<core::ViewerNode*>(graph->getNode(hViewer))->lastOutput,
            ctx->getBindlessHeap().getDescriptorSet(), pipelineLayout, imagePool.get());
        ctx->endSingleTimeCommands(cmd);
    }

    // At the end of Frame 1, finalViewerImage is in SHADER_READ_ONLY_OPTIMAL.
    gpu::ImageHandle viewerImage =
        static_cast<core::ViewerNode*>(graph->getNode(hViewer))->lastOutput;
    EXPECT_EQ(imagePool->getLayout(viewerImage), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Frame 2
    evalCtx.tasks.clear();
    graph->markDirty(hConst);
    graph->getNode(hViewer)->evaluate(evalCtx);

    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        // DispatchManager should transition viewerImage back to GENERAL in Pass 1.
        dispatchManager->submit(
            cmd, evalCtx.tasks, static_cast<core::ViewerNode*>(graph->getNode(hViewer))->lastOutput,
            ctx->getBindlessHeap().getDescriptorSet(), pipelineLayout, imagePool.get());
        ctx->endSingleTimeCommands(cmd);
    }

    EXPECT_EQ(imagePool->getLayout(viewerImage), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
