#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <memory>

#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/Nodes.hpp"
#include "core/RenderCache.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;
namespace platform = loom::platform;

class PushPullTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        if (!glfwInit()) return;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = std::make_unique<platform::Window>(100, 100, "PushPullTest");
        ctx = std::make_unique<gpu::VulkanContext>();
        try {
            ctx->init(*window, "PushPullTest");

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
            std::cerr << "Vulkan init failed: " << e.what() << std::endl;
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
        if (!m_initialized) GTEST_SKIP();
        imagePool = std::make_unique<gpu::TransientImagePool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
        pipelineCache = std::make_unique<gpu::PipelineCache>(ctx->getDevice(), pipelineLayout);

        VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx->getCommandPool(),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkAllocateCommandBuffers(ctx->getDevice(), &allocInfo, &cmd);

        evalCtx.requestedExtent = {100, 100};
        evalCtx.imagePool = imagePool.get();
        evalCtx.pipelineCache = pipelineCache.get();
        evalCtx.renderCache = &renderCache;
        evalCtx.allocator = ctx->getVmaAllocator();
        evalCtx.cmd = cmd;

        testRegion.tiles.push_back(
            {0, 0, evalCtx.requestedExtent.width, evalCtx.requestedExtent.height});
    }

    void TearDown() override {
        if (m_initialized) {
            vkFreeCommandBuffers(ctx->getDevice(), ctx->getCommandPool(), 1, &cmd);
            pipelineCache.reset();
            imagePool.reset();
            renderCache.clear();  // Ensure all images are released
        }
    }

    void endFrameCleanup() {
        vkQueueWaitIdle(ctx->getGraphicsQueue());
        for (auto& pair : evalCtx.pendingBufferFrees) {
            vmaDestroyBuffer(evalCtx.allocator, pair.first, pair.second);
        }
        evalCtx.pendingBufferFrees.clear();
        for (auto handle : renderCache.takePendingReleases()) {
            imagePool->release(handle);
        }
        imagePool->flushPendingReleases();
    }

    static std::unique_ptr<platform::Window> window;
    static std::unique_ptr<gpu::VulkanContext> ctx;
    static VkPipelineLayout pipelineLayout;
    static bool m_initialized;
    std::unique_ptr<gpu::TransientImagePool> imagePool;
    std::unique_ptr<gpu::PipelineCache> pipelineCache;
    VkCommandBuffer cmd;
    core::RenderCache renderCache;
    core::EvaluationContext evalCtx;
    core::Region testRegion;
};

std::unique_ptr<platform::Window> PushPullTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> PushPullTest::ctx = nullptr;
VkPipelineLayout PushPullTest::pipelineLayout = VK_NULL_HANDLE;
bool PushPullTest::m_initialized = false;

TEST_F(PushPullTest, BasicEval) {
    core::Graph graph;
    core::NodeHandle hA = graph.addNode(core::NodeType::Constant, "A");
    core::NodeHandle hMerge = graph.addNode(core::NodeType::Merge, "Merge");
    core::NodeHandle hViewer = graph.addNode(core::NodeType::Viewer, "Viewer");

    core::Node* nodeA = graph.getNode(hA);
    core::Node* nodeMerge = graph.getNode(hMerge);
    core::Node* nodeViewer = graph.getNode(hViewer);

    ASSERT_TRUE(graph.tryAddLink(nodeA->outputs[0], nodeMerge->inputs[0]));
    ASSERT_TRUE(graph.tryAddLink(nodeA->outputs[0], nodeMerge->inputs[1]));
    ASSERT_TRUE(graph.tryAddLink(nodeMerge->outputs[0], nodeViewer->inputs[0]));

    // Frame 1
    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);

    graph.execute(evalCtx, testRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(ctx->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);

    endFrameCleanup();

    EXPECT_FALSE(nodeA->isDirty);
    EXPECT_FALSE(nodeMerge->isDirty);
    EXPECT_TRUE(((core::ViewerNode*)nodeViewer)->lastOutput.isValid());
}

TEST_F(PushPullTest, DirtyPropagation) {
    core::Graph graph;
    core::NodeHandle hA = graph.addNode(core::NodeType::Constant, "A");
    core::NodeHandle hMerge = graph.addNode(core::NodeType::Merge, "Merge");
    core::NodeHandle hViewer = graph.addNode(core::NodeType::Viewer, "Viewer");

    core::Node* nodeA = graph.getNode(hA);
    core::Node* nodeMerge = graph.getNode(hMerge);
    core::Node* nodeViewer = graph.getNode(hViewer);

    ASSERT_TRUE(graph.tryAddLink(nodeA->outputs[0], nodeMerge->inputs[0]));
    ASSERT_TRUE(graph.tryAddLink(nodeMerge->outputs[0], nodeViewer->inputs[0]));

    // Initial eval to clear dirty flags
    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);
    graph.execute(evalCtx, testRegion);
    vkEndCommandBuffer(cmd);
    endFrameCleanup();

    nodeA->isDirty = false;
    nodeMerge->isDirty = false;
    nodeViewer->isDirty = false;

    // Mark dirty
    graph.markDirty(hA);
    EXPECT_TRUE(nodeA->isDirty);
    EXPECT_TRUE(nodeMerge->isDirty);
    EXPECT_TRUE(nodeViewer->isDirty);
}

TEST_F(PushPullTest, CachePersistence) {
    core::Graph graph;
    core::NodeHandle hA = graph.addNode(core::NodeType::Constant, "A");
    core::NodeHandle hViewer = graph.addNode(core::NodeType::Viewer, "Viewer");

    core::Node* nodeA = graph.getNode(hA);
    core::Node* nodeViewer = graph.getNode(hViewer);

    ASSERT_TRUE(graph.tryAddLink(nodeA->outputs[0], nodeViewer->inputs[0]));

    // Frame 1
    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);
    graph.execute(evalCtx, testRegion);
    vkEndCommandBuffer(cmd);
    endFrameCleanup();

    gpu::ImageHandle firstHandle = ((core::ViewerNode*)nodeViewer)->lastOutput;
    EXPECT_TRUE(firstHandle.isValid());
    EXPECT_FALSE(nodeA->isDirty);

    // Frame 2 - Should reuse cache
    evalCtx.tasks.clear();
    vkBeginCommandBuffer(cmd, &beginInfo);
    graph.execute(evalCtx, testRegion);
    vkEndCommandBuffer(cmd);
    endFrameCleanup();

    gpu::ImageHandle secondHandle = ((core::ViewerNode*)nodeViewer)->lastOutput;
    EXPECT_EQ(firstHandle.poolIndex, secondHandle.poolIndex);
    EXPECT_EQ(firstHandle.generation, secondHandle.generation);
    EXPECT_EQ(evalCtx.tasks.size(), 0);  // No compute tasks should be generated
}

TEST_F(PushPullTest, DeletionGC) {
    core::Graph graph;
    core::NodeHandle hA = graph.addNode(core::NodeType::Constant, "A");
    core::NodeHandle hViewer = graph.addNode(core::NodeType::Viewer, "Viewer");

    core::PinHandle outA = graph.getNode(hA)->outputs[0];
    ASSERT_TRUE(graph.tryAddLink(outA, graph.getNode(hViewer)->inputs[0]));

    // Frame 1: Eval to populate cache
    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);
    graph.execute(evalCtx, testRegion);
    vkEndCommandBuffer(cmd);

    // Delete node A
    graph.removeNode(hA);

    // Manually evict using the saved handle. evict is keyed on (pin, region)
    // so we pass the same region the executor used to populate the cache.
    renderCache.evict(outA, testRegion);

    EXPECT_EQ(renderCache.takePendingReleases().size(), 1);

    endFrameCleanup();
}
