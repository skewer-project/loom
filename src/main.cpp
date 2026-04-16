#include <cstdlib>  // For EXIT_SUCCESS and EXIT_FAILURE
#include <iostream>

#include "core/Graph.hpp"
#include "gpu/DispatchManager.hpp"
#include "gpu/DisplayPass.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"
#include "ui/NodeEditorPanel.hpp"

int main() {
    try {
        std::cout << "Initializing Loom..." << std::endl;

        loom::platform::Window window(1280, 720, "Loom");

        loom::gpu::VulkanContext vulkan;
        vulkan.init(window, "Loom");

        // Create Global Pipeline Layout
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 128;

        VkDescriptorSetLayout setLayout = vulkan.getBindlessHeap().getLayout();
        VkDescriptorSet bindlessSet = vulkan.getBindlessHeap().getDescriptorSet();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        VkPipelineLayout pipelineLayout;
        if (vkCreatePipelineLayout(vulkan.getDevice(), &layoutInfo, nullptr, &pipelineLayout) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        loom::gpu::PipelineCache pipelineCache(vulkan.getDevice(), pipelineLayout);
        loom::gpu::DispatchManager dispatchManager;
        loom::gpu::TransientImagePool imagePool(vulkan.getDevice(), vulkan.getVmaAllocator(),
                                                vulkan.getBindlessHeap());

        loom::gpu::DisplayPass displayPass(vulkan.getDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
                                           setLayout);

        loom::ui::ImGuiRendererCreateInfo imguiInfo{};
        imguiInfo.window = window.getNativeWindow();
        imguiInfo.instance = vulkan.getVkInstance();
        imguiInfo.physicalDevice = vulkan.getPhysicalDevice();
        imguiInfo.device = vulkan.getDevice();
        imguiInfo.graphicsQueueFamily = vulkan.getGraphicsQueueFamily();
        imguiInfo.graphicsQueue = vulkan.getGraphicsQueue();
        imguiInfo.descriptorPool = vulkan.getDescriptorPool();
        imguiInfo.colorFormat = vulkan.getSwapchainImageFormat();
        imguiInfo.imageCount = static_cast<uint32_t>(vulkan.getSwapchainImageCount());
        imguiInfo.minImageCount = 2;
        imguiInfo.vmaAllocator = vulkan.getVmaAllocator();

        loom::ui::ImGuiRenderer imgui;
        imgui.init(imguiInfo);

        loom::core::Graph graph;
        loom::ui::NodeEditorPanel nodeEditor(&graph);

        // Step 5: Initial Testing Graph
        {
            auto constNodeHandle = graph.addNode(loom::core::NodeType::Constant);
            auto viewerNodeHandle = graph.addNode(loom::core::NodeType::Viewer);

            loom::core::Node* constNode = graph.getNode(constNodeHandle);
            loom::core::Node* viewerNode = graph.getNode(viewerNodeHandle);

            if (constNode && viewerNode) {
                // Connect Constant output to Viewer input
                graph.tryAddLink(constNode->outputs[0], viewerNode->inputs[0]);
            }
        }

        std::cout << "Loom initialized successfully." << std::endl;

        while (!window.shouldClose()) {
            window.pollEvents();

            if (VkCommandBuffer cmd = vulkan.beginFrame()) {
                // Build UI
                imgui.beginFrame();
                imgui.drawDockspace();
                nodeEditor.draw("Node Editor");

                // Evaluate Graph
                loom::core::EvaluationContext evalCtx{};
                evalCtx.requestedExtent = {static_cast<uint32_t>(imgui.getViewportSize().x),
                                           static_cast<uint32_t>(imgui.getViewportSize().y)};
                evalCtx.imagePool = &imagePool;
                evalCtx.pipelineCache = &pipelineCache;
                evalCtx.allocator = vulkan.getVmaAllocator();

                // Find all viewers and evaluate them
                loom::gpu::ImageHandle viewerOutput;
                graph.forEachNode([&](loom::core::NodeHandle h, loom::core::Node& node) {
                    if (node.type == loom::core::NodeType::Viewer) {
                        node.evaluate(evalCtx);
                        viewerOutput = static_cast<loom::core::ViewerNode&>(node).lastOutput;
                    }
                });

                // Record compute dispatches
                dispatchManager.submit(cmd, evalCtx.tasks, viewerOutput, bindlessSet,
                                       pipelineLayout, &imagePool);

                // If we have a viewer output, render it to the viewport
                if (viewerOutput.isValid()) {
                    displayPass.record(cmd, imagePool.getImage(viewerOutput),
                                       imgui.getViewportImage(), imgui.getViewportImageView(),
                                       bindlessSet, viewerOutput.bindlessSlot,
                                       (uint32_t)imgui.getViewportSize().x,
                                       (uint32_t)imgui.getViewportSize().y, 0);
                }

                vulkan.endFrame(cmd, imgui);
            }

            imagePool.flushPendingReleases();
        }

        vulkan.waitIdle();
        vkDestroyPipelineLayout(vulkan.getDevice(), pipelineLayout, nullptr);

        std::cout << "Shutting down Loom..." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
