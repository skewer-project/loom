#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "core/Camera.hpp"
#include "core/ColorManagement.hpp"
#include "core/Graph.hpp"
#include "core/Log.hpp"
#include "core/RenderCache.hpp"
#include "gpu/DispatchManager.hpp"
#include "gpu/DisplayPass.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/PointCloudPass.hpp"
#include "gpu/StagingArena.hpp"
#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "io/DeepReader.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"
#include "ui/NodeEditorPanel.hpp"

namespace {

// Build the default `DeepEXRRead → DeepFlatten → Viewer` chain. Returns the
// handle of the DeepEXRRead node so the per-frame code can fish its deep
// output out of the render cache for the PointCloud path.
loom::core::NodeHandle buildDeepViewChain(loom::core::Graph& graph, const std::string& path) {
    auto reader = graph.addNode(loom::core::NodeType::DeepEXRRead);
    auto flatten = graph.addNode(loom::core::NodeType::DeepFlatten);
    auto viewer = graph.addNode(loom::core::NodeType::Viewer);

    if (auto* r = graph.getNode(reader)) {
        r->setParam(0, path);  // file_path
    }

    auto* readerNode = graph.getNode(reader);
    auto* flattenNode = graph.getNode(flatten);
    auto* viewerNode = graph.getNode(viewer);
    if (readerNode && flattenNode && viewerNode) {
        if (!graph.tryAddLink(readerNode->outputs[0], flattenNode->inputs[0])) {
            loom::log::warn("startup: failed to wire DeepEXRRead -> DeepFlatten");
        }
        if (!graph.tryAddLink(flattenNode->outputs[0], viewerNode->inputs[0])) {
            loom::log::warn("startup: failed to wire DeepFlatten -> Viewer");
        }
    }
    return reader;
}

// Build the legacy demo chain (`Constant → Viewer`) used when no EXR path
// was given. Returns an invalid handle since there's no deep reader.
loom::core::NodeHandle buildDemoChain(loom::core::Graph& graph) {
    auto constant = graph.addNode(loom::core::NodeType::Constant);
    auto viewer = graph.addNode(loom::core::NodeType::Viewer);
    auto* c = graph.getNode(constant);
    auto* v = graph.getNode(viewer);
    if (c && v) {
        if (!graph.tryAddLink(c->outputs[0], v->inputs[0])) {
            loom::log::warn("startup: failed to wire Constant -> Viewer");
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        loom::log::info("Initializing Loom...");

        std::string deepPath;
        if (argc >= 2) {
            deepPath = argv[1];
            loom::log::info("Will open deep EXR: ", deepPath);
        }

        loom::platform::Window window(1280, 720, "Loom");

        loom::gpu::VulkanContext vulkan;
        vulkan.init(window, "Loom");

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
        loom::gpu::TransientBufferPool bufferPool(vulkan.getDevice(), vulkan.getVmaAllocator(),
                                                  vulkan.getBindlessHeap());

        // Phase B.7: staging arena for one-shot deep-EXR uploads + the
        // sync reader. The arena is intentionally not reset every frame —
        // v1 loads the EXR once and reuses its GPU resources across frames
        // (see docs/archive/feature-open-exr-2026.md §B.7). A future PR
        // adds per-frame slabs for animation playback (Phase C).
        loom::gpu::StagingArena stagingArena(vulkan.getVmaAllocator());
        loom::io::SyncDeepReader deepReader;

        loom::gpu::DisplayPass displayPass(vulkan.getDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
                                           setLayout);

        // Point-cloud pass. Constructed regardless of CLI args so the
        // viewport-mode dropdown is always available — switching to
        // PointCloud3D with no deep payload is a no-op (the pass's
        // `record` early-returns on an invalid `DeepRef`).
        loom::gpu::PointCloudPass pointCloudPass(vulkan.getDevice(), vulkan.getVmaAllocator(),
                                                 setLayout, pipelineCache,
                                                 VK_FORMAT_R32G32B32A32_SFLOAT);

        loom::core::Camera camera;
        camera.setPosition({0.0f, 0.0f, 3.0f});
        camera.setTarget({0.0f, 0.0f, 0.0f});

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
        loom::core::RenderCache renderCache;
        loom::ui::NodeEditorPanel nodeEditor(&graph);

        const loom::core::NodeHandle deepReaderHandle =
            deepPath.empty() ? buildDemoChain(graph) : buildDeepViewChain(graph, deepPath);

        loom::log::info("Loom initialized successfully.");

        while (!window.shouldClose()) {
            window.pollEvents();

            if (VkCommandBuffer cmd = vulkan.beginFrame()) {
                imgui.beginFrame();
                imgui.drawDockspace(&camera);
                nodeEditor.draw("Node Editor");

                loom::core::EvaluationContext evalCtx{};
                evalCtx.requestedExtent = {static_cast<uint32_t>(imgui.getViewportSize().x),
                                           static_cast<uint32_t>(imgui.getViewportSize().y)};
                evalCtx.imagePool = &imagePool;
                evalCtx.bufferPool = &bufferPool;
                evalCtx.stagingArena = &stagingArena;
                evalCtx.pipelineCache = &pipelineCache;
                evalCtx.renderCache = &renderCache;
                evalCtx.allocator = vulkan.getVmaAllocator();
                evalCtx.cmd = cmd;
                evalCtx.deepReader = &deepReader;
                evalCtx.camera = &camera;
                evalCtx.frame = vulkan.currentFrameValue();

                loom::core::Region region;
                region.tiles.push_back({0, 0, (uint32_t)imgui.getViewportSize().x,
                                        (uint32_t)imgui.getViewportSize().y});
                graph.execute(evalCtx, region);

                // Viewer's flat-image output. May be invalid if the graph's
                // viewer isn't wired this frame.
                loom::gpu::ImageHandle viewerOutput;
                auto viewers = graph.getViewers();
                if (!viewers.empty()) {
                    if (auto* node = graph.getNode(viewers[0])) {
                        viewerOutput = static_cast<loom::core::ViewerNode*>(node)->lastOutput;
                    }
                }

                // Deep payload sourced from the DeepEXRRead's output pin.
                // Pulled directly from the cache so the PointCloud path can
                // bypass the flatten composite. An empty / unconfigured
                // reader produces a `Kind::None` ref; PointCloudPass::record
                // early-returns on that.
                loom::gpu::ResourceRef::DeepRef deepRef;
                if (deepReaderHandle.isValid()) {
                    if (auto* node = graph.getNode(deepReaderHandle);
                        node && !node->outputs.empty()) {
                        auto ref = renderCache.retrieve(node->outputs[0], region);
                        if (ref.kind == loom::gpu::ResourceRef::Kind::Deep) deepRef = ref.deep;
                    }
                }

                dispatchManager.submit(cmd, evalCtx.tasks, viewerOutput, bindlessSet,
                                       pipelineLayout, &imagePool);

                const bool wantPointCloud =
                    imgui.getViewportMode() == loom::ui::ViewportMode::PointCloud3D;
                const uint32_t vpW = static_cast<uint32_t>(imgui.getViewportSize().x);
                const uint32_t vpH = static_cast<uint32_t>(imgui.getViewportSize().y);

                if (wantPointCloud && deepRef.samples.isValid() && vpW > 0 && vpH > 0) {
                    pointCloudPass.record(cmd, deepRef, imgui.getViewportImage(),
                                          imgui.getViewportImageView(), bindlessSet, vpW, vpH,
                                          camera);
                } else if (viewerOutput.isValid()) {
                    const auto displayTransform = loom::color::pickTransformForSwapchainFormat(
                        static_cast<uint32_t>(vulkan.getSwapchainImageFormat()));
                    displayPass.record(cmd, imagePool.getImage(viewerOutput),
                                       imgui.getViewportImage(), imgui.getViewportImageView(),
                                       bindlessSet, viewerOutput.bindlessSlot, vpW, vpH,
                                       /*toneMapMode=*/0, static_cast<uint32_t>(displayTransform),
                                       /*exposure=*/1.0f);
                }

                vulkan.endFrame(cmd, imgui);

                renderCache.garbageCollect(&graph);

                const uint64_t releaseAtFrame =
                    vulkan.currentFrameValue() + loom::core::MAX_FRAMES_IN_FLIGHT;
                for (auto handle : renderCache.takePendingReleases()) {
                    imagePool.release(handle, releaseAtFrame);
                }
            }

            const uint64_t retired = vulkan.getRetiredFrameValue();
            imagePool.onFrameRetired(retired);
            bufferPool.onFrameRetired(retired);
            vulkan.getBindlessHeap().onFrameRetired(retired);
        }

        vulkan.waitIdle();
        vkDestroyPipelineLayout(vulkan.getDevice(), pipelineLayout, nullptr);

        loom::log::info("Shutting down Loom...");

    } catch (const std::exception& e) {
        loom::log::error("Fatal error: ", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
