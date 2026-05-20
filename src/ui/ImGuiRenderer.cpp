#include "ui/ImGuiRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <stdexcept>

#include "core/Camera.hpp"
#include "core/Graph.hpp"
#include "core/Log.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"

namespace loom::ui {

ImGuiRenderer::~ImGuiRenderer() { shutdown(); }

void ImGuiRenderer::init(const ImGuiRendererCreateInfo& info) {
    // Step A — Create ImGui context:
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "config/layout.ini";

    // Step B — Set ImGui style:
    ImGui::StyleColorsDark();
    // TODO: Replace with a custom loom theme
    // once the node editor UI design is established.

    // Step C — Initialize GLFW backend:
    ImGui_ImplGlfw_InitForVulkan(info.window, true);
    // 'true' installs GLFW callbacks automatically.
    // Set to false and install manually if input conflicts arise
    // with the node editor later.

    // Step D — Initialize Vulkan backend:
    m_device = info.device;
    m_vmaAllocator = info.vmaAllocator;
    m_colorFormat = info.colorFormat;

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = info.instance;
    init_info.PhysicalDevice = info.physicalDevice;
    init_info.Device = info.device;
    init_info.QueueFamily = info.graphicsQueueFamily;
    init_info.Queue = info.graphicsQueue;
    init_info.DescriptorPool = info.descriptorPool;
    init_info.MinImageCount = info.minImageCount;
    init_info.ImageCount = info.imageCount;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // Dynamic rendering requires telling ImGui
    // what color format the swapchain uses so it can build
    // its internal pipeline without a render pass object.
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_colorFormat;

    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;  // Optional: could add a callback here

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error("failed to initialize ImGui Vulkan backend!");
    }

    createSampler();

    m_initialized = true;

    loom::log::info("ImGui fonts uploaded to GPU successfully.");
}

void ImGuiRenderer::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_viewportSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create viewport sampler!");
    }
}

void ImGuiRenderer::recreateViewportTarget(uint32_t width, uint32_t height) {
    // Wait for the GPU to finish using the old resources.
    // In a more complex engine, we would use a frame-buffered deletion queue.
    vkDeviceWaitIdle(m_device);

    // 1. Cleanup old resources
    if (m_viewportTextureId != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_viewportTextureId);
        m_viewportTextureId = VK_NULL_HANDLE;
    }
    if (m_viewportImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_viewportImageView, nullptr);
        m_viewportImageView = VK_NULL_HANDLE;
    }
    if (m_viewportImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_vmaAllocator, m_viewportImage, m_viewportAllocation);
        m_viewportImage = VK_NULL_HANDLE;
        m_viewportAllocation = VK_NULL_HANDLE;
    }

    // 2. Allocate new image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;  // Standard for compute output
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &m_viewportImage,
                       &m_viewportAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create viewport image!");
    }

    // 3. Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_viewportImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_viewportImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create viewport image view!");
    }

    // 4. Register with ImGui
    m_viewportTextureId = ImGui_ImplVulkan_AddTexture(m_viewportSampler, m_viewportImageView,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void ImGuiRenderer::beginFrame() {
    // Order matters — Vulkan frame first,
    // then GLFW, then ImGui. Reversing this causes input
    // latency or assertion failures inside ImGui.
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiRenderer::endFrame(VkCommandBuffer cmd) {
    // endFrame must be called inside an active
    // render pass. The command buffer must be in the recording
    // state with vkCmdBeginRenderPass already called.
    // Calling this outside a render pass is a validation error.
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void ImGuiRenderer::shutdown() {
    // Guard with m_initialized to prevent double-shutdown:
    if (!m_initialized) return;

    vkDeviceWaitIdle(m_device);

    if (m_viewportTextureId != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_viewportTextureId);
        m_viewportTextureId = VK_NULL_HANDLE;
    }
    if (m_viewportImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_viewportImageView, nullptr);
        m_viewportImageView = VK_NULL_HANDLE;
    }
    if (m_viewportImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_vmaAllocator, m_viewportImage, m_viewportAllocation);
        m_viewportImage = VK_NULL_HANDLE;
        m_viewportAllocation = VK_NULL_HANDLE;
    }
    if (m_viewportSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_viewportSampler, nullptr);
        m_viewportSampler = VK_NULL_HANDLE;
    }

    // Shutdown order is the strict reverse of
    // initialization order. Vulkan backend first, then GLFW,
    // then the ImGui context itself.
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiRenderer::resyncOrbitFromCamera(const core::Camera& camera) {
    const glm::vec3 offset = camera.position() - camera.target();
    m_orbitRadius = std::max(0.01f, glm::length(offset));
    m_orbitYaw = std::atan2(offset.x, offset.z);
    m_orbitPitch = std::asin(std::clamp(offset.y / m_orbitRadius, -1.0f, 1.0f));
    m_orbitInitialised = true;
}

void ImGuiRenderer::applyOrbitInput(core::Camera& camera) {
    // Initialise orbit state from the camera's current pose on first use.
    // External setters (e.g. `Camera::frameToBounds` from the main loop's
    // auto-frame hook) call `resyncOrbitFromCamera` explicitly so the
    // controller picks up the new pose without losing user-driven yaw /
    // pitch outside of that hand-off.
    if (!m_orbitInitialised) {
        resyncOrbitFromCamera(camera);
    }

    ImGuiIO& io = ImGui::GetIO();

    // Step 1 — Input handling. Read drag / wheel gestures into the
    // spherical-coordinate state. `inputFired` records whether anything
    // actually changed this frame; the pose write below is gated on it
    // so that idle frames don't redirty the CameraNode (which would
    // overwrite any user knob-edit on the position param). See B.8
    // follow-up #4 / Bug #2.
    bool inputFired = false;
    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const ImVec2 delta = io.MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f) {
                // Scale sensitivity by `tan(fovY/2)` so a one-screen-width
                // drag always produces ~a quarter-turn regardless of zoom
                // level. The 1.5× factor reproduces the prior 0.005
                // rad/pixel feel at the engine's default 60° FOV on a
                // 1080p viewport.
                const float kDragBase = 1.5f * std::tan(0.5f * camera.fovY());
                // Normalise by viewport height: a full-height vertical
                // drag always rotates by the same angle regardless of
                // window size.
                const float viewH = std::max(m_viewportSize.y, 1.0f);
                const float dragScale = kDragBase / viewH;
                m_orbitYaw -= delta.x * dragScale;
                m_orbitPitch += delta.y * dragScale;
                // Clamp pitch to avoid gimbal-lock at the poles
                // (cos(pitch) → 0 → up-vector degenerate).
                constexpr float kPitchLimit = 1.5533f;  // ~89° in radians
                m_orbitPitch = std::clamp(m_orbitPitch, -kPitchLimit, kPitchLimit);
                inputFired = true;
            }
        }
        if (io.MouseWheel != 0.0f) {
            // Multiplicative zoom: each scroll tick scales radius by
            // ~1.1×. Scale-invariant by construction — no FOV factor
            // needed. Upper bound widened to 1e6 so scenes whose bounding
            // sphere sits a kilometer out from origin remain reachable.
            const float zoomFactor = std::pow(1.1f, -io.MouseWheel);
            m_orbitRadius = std::clamp(m_orbitRadius * zoomFactor, 0.001f, 1.0e6f);
            inputFired = true;
        }
    }

    // Step 2 — Pose application. Skipped on idle frames so user knob-edits
    // on the CameraNode's `position` param survive (without this gate the
    // orbit's idle-state value overwrote any direct edit every frame).
    if (!inputFired) return;

    const float cy = std::cos(m_orbitYaw);
    const float sy = std::sin(m_orbitYaw);
    const float cp = std::cos(m_orbitPitch);
    const float sp = std::sin(m_orbitPitch);
    const glm::vec3 newPos = camera.target() + m_orbitRadius * glm::vec3(sy * cp, sp, cy * cp);
    camera.setPosition(newPos);

    // Push the new position into the active CameraNode's `position`
    // param so the graph re-evaluates with the user's view next frame.
    // The local Camera mutation above keeps the controller's orbit
    // state coherent within the current frame; the param write drives
    // downstream renderers (PointCloudRenderNode). `setParam` flips the
    // node's dirty flag, which cascades through Graph::markDirty when
    // the graph executes next.
    if (m_cameraGraph && m_cameraNode.isValid()) {
        if (auto* node = m_cameraGraph->getNode(m_cameraNode)) {
            node->setParam(m_cameraPositionParamIndex, newPos);
        }
    }
}

void ImGuiRenderer::drawDockspace(core::Camera* orbitCamera) {
    // Establish the fullscreen dockspace ID
    ImGuiID dockspace_id = ImGui::GetID("##DockSpace");

    // Step 2: The DockBuilder Initialization (Conditional, Not Unconditional)
    // The layout must only be generated when no existing layout is loaded from imgui.ini.
    // On all subsequent launches, imgui.ini populates the dock tree before the first frame,
    // so this block is skipped entirely. Using a bare static bool firstTime would break ini
    // persistence.
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        // Step 3: Layout Topology (Top/Bottom Split)
        ImGuiID nodeEditorId, viewportId;
        // The third parameter (0.4f) is the size ratio of the DIRECTION side.
        // out_id_at_dir (nodeEditorId) = the bottom node, 40% height
        // out_id_opposite (viewportId) = the top node, 60% height
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.4f, &nodeEditorId, &viewportId);

        // Dock the windows by their exact string names.
        // These names must remain stable across sessions for ini persistence to function correctly.
        ImGui::DockBuilderDockWindow("Viewport", viewportId);
        ImGui::DockBuilderDockWindow("Node Editor", nodeEditorId);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    // Establish the fullscreen dockspace using the native helper
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());

    // Step 4: Viewport Panel & Size Tracking
    //
    // NoScrollbar + NoScrollWithMouse keep the mouse wheel from being
    // consumed by ImGui's default panel-scroll handling. Without these
    // flags the viewport panel intercepts the wheel before `applyOrbitInput`
    // (under `ImGui::IsItemHovered()`) sees it, so orbit-zoom is silently
    // dead even though the drag-yaw / drag-pitch branch works. The viewport
    // image fills the dock; the panel never needs its own scrollbar.
    ImGui::Begin("Viewport", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Input-mode selector. Drawn in the viewport panel header above the
    // image so the user can swap drag-pans-2D vs drag-orbits-3D-camera
    // without leaving the panel. The choice controls *gesture
    // interpretation*, not which renderer runs (that's a graph-wiring
    // decision now per CONVENTIONS §21). Only rendered when an orbit
    // camera is available; absent it, mode is implicitly Pan2D.
    if (orbitCamera) {
        const char* items[] = {"Pan 2D", "Orbit 3D"};
        int current = static_cast<int>(m_viewportInputMode);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("##viewport-input-mode", &current, items, IM_ARRAYSIZE(items))) {
            m_viewportInputMode = static_cast<ViewportInputMode>(current);
        }
    }

    ImVec2 currentSize = ImGui::GetContentRegionAvail();

    // GetContentRegionAvail() returns pixel-snapped float values.
    // Direct ImVec2 comparison is correct here — do not introduce an
    // epsilon tolerance, as that would mask legitimate single-pixel resize events.
    if (currentSize.x > 0 && currentSize.y > 0 &&
        (currentSize.x != m_viewportSize.x || currentSize.y != m_viewportSize.y)) {
        m_viewportSize = currentSize;

        // Trigger reallocation of the offscreen render target
        recreateViewportTarget((uint32_t)m_viewportSize.x, (uint32_t)m_viewportSize.y);

        // Aspect ratio change is the camera's concern. The orbit controller
        // mutates position; aspect is independent — push it directly.
        if (orbitCamera && currentSize.y > 0) {
            orbitCamera->setAspect(currentSize.x / currentSize.y);
        }
    }

    if (m_viewportTextureId) {
        // UV mapping: in Pan2D mode the 2D pan/zoom state controls which
        // sub-rect of the source image is shown. In Orbit3D mode the UV
        // is the identity (orbit gestures don't touch the framebuffer
        // crop — they mutate the camera, which re-renders next frame).
        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        if (m_viewportInputMode == ViewportInputMode::Pan2D) {
            const float half = 0.5f / std::max(m_view2DZoom, 1.0e-3f);
            uv0 = {m_view2DCenter.x - half, m_view2DCenter.y - half};
            uv1 = {m_view2DCenter.x + half, m_view2DCenter.y + half};
        }
        // Phase 6 maps UV (0,0) to the top-left, matching ImGui's default
        // exactly. Do NOT flip the V coordinate here.
        ImGui::Image((ImTextureID)m_viewportTextureId, currentSize, uv0, uv1);

        // Route mouse drag / scroll over the image to the active input-
        // mode's handler. Modes are mutually exclusive — only one
        // handler runs per frame.
        if (m_viewportInputMode == ViewportInputMode::Pan2D) {
            applyView2DInput();
        } else if (orbitCamera && m_viewportInputMode == ViewportInputMode::Orbit3D) {
            applyOrbitInput(*orbitCamera);
        }
    } else {
        ImGui::Text("No output available.");
    }

    ImGui::End();
}

void ImGuiRenderer::applyView2DInput() {
    if (!ImGui::IsItemHovered()) return;
    ImGuiIO& io = ImGui::GetIO();

    // Drag-to-pan. The drag delta is in viewport pixels; we convert to UV
    // space by dividing by viewport extent × zoom — at higher zoom one
    // pixel of cursor motion covers proportionally less of the underlying
    // image. The sign is negated so dragging right moves the image right
    // (i.e. the UV window shifts left).
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = io.MouseDelta;
        const float vw = std::max(m_viewportSize.x, 1.0f);
        const float vh = std::max(m_viewportSize.y, 1.0f);
        const float invZoom = 1.0f / std::max(m_view2DZoom, 1.0e-3f);
        m_view2DCenter.x -= delta.x * invZoom / vw;
        m_view2DCenter.y -= delta.y * invZoom / vh;
    }

    // Multiplicative wheel zoom, cursor-anchored. Without the anchor, a
    // zoom always recenters on (0.5, 0.5); with it, the pixel under the
    // cursor stays put. The math: at zoom z the UV at the cursor is
    // `center + (cursorOffset / size) * (1/z)`; we want this expression
    // invariant in z, so when z scales by k, center adjusts so that
    // `center_new + offset * (1/(z*k)) == center_old + offset * (1/z)`.
    if (io.MouseWheel != 0.0f) {
        const float scale = std::pow(1.1f, io.MouseWheel);
        const float newZoom = std::clamp(m_view2DZoom * scale, 0.05f, 64.0f);

        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const float vw = std::max(m_viewportSize.x, 1.0f);
        const float vh = std::max(m_viewportSize.y, 1.0f);
        const float u = (mouse.x - imageMin.x) / vw - 0.5f;  // [-0.5, 0.5]
        const float v = (mouse.y - imageMin.y) / vh - 0.5f;

        // Anchor: UV at cursor before zoom == UV at cursor after zoom.
        m_view2DCenter.x += u * (1.0f / m_view2DZoom - 1.0f / newZoom);
        m_view2DCenter.y += v * (1.0f / m_view2DZoom - 1.0f / newZoom);
        m_view2DZoom = newZoom;
    }
}

}  // namespace loom::ui
