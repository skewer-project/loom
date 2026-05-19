#include <algorithm>
#include <cstdlib>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
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

// Clear the viewport image to opaque black and leave it in
// `SHADER_READ_ONLY_OPTIMAL`. Used as the fallback when neither the flat
// nor the point-cloud pass would otherwise touch the viewport this frame
// — without it, ImGui samples an `UNDEFINED` image and the validation
// layers emit a cascade of layout warnings every frame. The clear is
// cheap (one barrier, one `vkCmdClearColorImage`, one barrier) and
// produces a clean black panel instead of garbage colours.
void clearViewportToBlack(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier2 pre{};
    pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    pre.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    pre.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    pre.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    pre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // discard prior contents
    pre.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre.image = image;
    pre.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo preDep{};
    preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    preDep.imageMemoryBarrierCount = 1;
    preDep.pImageMemoryBarriers = &pre;
    vkCmdPipelineBarrier2(cmd, &preDep);

    VkClearColorValue clear{};
    clear.float32[0] = 0.0f;
    clear.float32[1] = 0.0f;
    clear.float32[2] = 0.0f;
    clear.float32[3] = 1.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    VkImageMemoryBarrier2 post{};
    post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    post.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    post.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    post.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    post.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post.image = image;
    post.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.imageMemoryBarrierCount = 1;
    postDep.pImageMemoryBarriers = &post;
    vkCmdPipelineBarrier2(cmd, &postDep);
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

        // Tracks the most-recently-auto-framed deep samples buffer. An
        // invalid handle means "no frame has been taken yet" — the
        // per-frame check below fires the first auto-frame as soon as a
        // valid deep payload with non-empty bounds reaches the cache.
        loom::gpu::BufferHandle lastFramedSamples;

        // Scene bounds remembered from the most recent auto-frame. Used by
        // the per-frame clip-plane refresh to keep the near / far planes
        // tracking the camera's current orbit distance — without this the
        // user can zoom past the back of the bounding sphere and the far
        // plane (set once by `frameToBounds`) clips the geometry.
        glm::vec3 lastFramedCenter{0.0f};
        float lastFramedSceneRadius = 0.0f;

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

                // Auto-frame the camera the first time a valid deep payload
                // appears, and again whenever the payload's identity
                // changes (a re-read after edit, an animation frame swap,
                // ...). Identity is the (`poolIndex`, `generation`) of the
                // samples buffer — `uploadDeepImage` always acquires a
                // fresh buffer per call, so the pair flips on every real
                // re-upload. Skip if the bounds reduction came back
                // invalid (background-sentinel-only file, half-Z fallback):
                // the camera keeps its prior pose rather than snapping to
                // a degenerate fit.
                if (deepRef.samples.isValid() && deepRef.sceneBounds.valid) {
                    const bool changed =
                        (deepRef.samples.poolIndex != lastFramedSamples.poolIndex ||
                         deepRef.samples.generation != lastFramedSamples.generation);
                    if (changed) {
                        camera.frameToBounds(deepRef.sceneBounds.center(),
                                             deepRef.sceneBounds.radius());
                        imgui.resyncOrbitFromCamera(camera);
                        lastFramedSamples = deepRef.samples;
                        lastFramedCenter = deepRef.sceneBounds.center();
                        lastFramedSceneRadius = deepRef.sceneBounds.radius();
                    }
                }

                // Per-frame clip-plane refresh. The orbit controller mutates
                // the camera's position each frame; near / far must track so
                // the bounding sphere stays inside the frustum at every
                // zoom level. `frameToBounds` set the planes once at
                // framing time; without this update zooming in past
                // distance ≈ scene_radius walks geometry through the far
                // plane. Skip when no scene has been framed (demo graph
                // / Flat 2D mode without a deep payload).
                if (lastFramedSceneRadius > 0.0f) {
                    const float distance = glm::length(camera.position() - lastFramedCenter);
                    const float margin = std::max(0.1f * lastFramedSceneRadius, 0.01f);
                    const float nearP = std::max(distance - lastFramedSceneRadius - margin, 0.001f);
                    const float farP = distance + lastFramedSceneRadius + margin;
                    camera.setClipPlanes(nearP, farP);
                }

                dispatchManager.submit(cmd, evalCtx.tasks, viewerOutput, bindlessSet,
                                       pipelineLayout, &imagePool);

                // Bridge during the migration: while Step 8 hasn't yet
                // wired the CameraNode + PointCloudRenderNode chain,
                // selecting Orbit 3D from the input-mode dropdown still
                // engages the engine-managed PointCloudPass call below.
                // Step 8 deletes this branch once the node graph drives
                // the render.
                const bool wantPointCloud =
                    imgui.getViewportInputMode() == loom::ui::ViewportInputMode::Orbit3D;
                const uint32_t vpW = static_cast<uint32_t>(imgui.getViewportSize().x);
                const uint32_t vpH = static_cast<uint32_t>(imgui.getViewportSize().y);

                if (wantPointCloud && deepRef.samples.isValid() && vpW > 0 && vpH > 0) {
                    // Snapshot the engine camera into a CameraRef — Step 8
                    // will switch to pulling this from a CameraNode in the
                    // graph; for now, materialise from the engine-owned
                    // Camera so PointCloudPass keeps working under the new
                    // signature.
                    loom::gpu::ResourceRef::CameraRef camRef;
                    camRef.view = camera.viewMatrix();
                    camRef.proj = camera.projectionMatrix();
                    camRef.eyePos = camera.position();
                    camRef.nearPlane = camera.nearPlane();
                    camRef.farPlane = camera.farPlane();
                    camRef.fovY = camera.fovY();
                    pointCloudPass.record(cmd, deepRef, imgui.getViewportImage(),
                                          imgui.getViewportImageView(), bindlessSet, vpW, vpH,
                                          camRef);
                } else if (viewerOutput.isValid()) {
                    const auto displayTransform = loom::color::pickTransformForSwapchainFormat(
                        static_cast<uint32_t>(vulkan.getSwapchainImageFormat()));
                    // Source extent (the viewer's HDR image native size) is
                    // queried from the pool so DisplayPass can aspect-fit
                    // the source into the viewport regardless of EXR
                    // resolution — fixes the unrendered-region glitch where
                    // an out-of-range imageLoad returned garbage.
                    const VkExtent2D srcExtent = imagePool.getExtent(viewerOutput);
                    displayPass.record(cmd, imagePool.getImage(viewerOutput),
                                       imgui.getViewportImage(), imgui.getViewportImageView(),
                                       bindlessSet, viewerOutput.bindlessSlot, vpW, vpH,
                                       srcExtent.width, srcExtent.height,
                                       /*toneMapMode=*/0, static_cast<uint32_t>(displayTransform),
                                       /*exposure=*/1.0f);
                } else if (vpW > 0 && vpH > 0) {
                    // Neither pass wrote the viewport this frame (load
                    // failure, staging OOM, unsupported layout, etc.).
                    // Without this fallback ImGui samples an UNDEFINED
                    // image — see `clearViewportToBlack` above.
                    clearViewportToBlack(cmd, imgui.getViewportImage());
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
