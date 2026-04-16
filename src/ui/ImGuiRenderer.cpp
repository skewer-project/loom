#include "ui/ImGuiRenderer.hpp"

#include <iostream>
#include <stdexcept>

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

    std::cout << "ImGui fonts uploaded to GPU successfully." << std::endl;
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

void ImGuiRenderer::drawDockspace() {
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
    ImGui::Begin("Viewport");
    ImVec2 currentSize = ImGui::GetContentRegionAvail();

    // GetContentRegionAvail() returns pixel-snapped float values.
    // Direct ImVec2 comparison is correct here — do not introduce an
    // epsilon tolerance, as that would mask legitimate single-pixel resize events.
    if (currentSize.x > 0 && currentSize.y > 0 &&
        (currentSize.x != m_viewportSize.x || currentSize.y != m_viewportSize.y)) {
        m_viewportSize = currentSize;

        // Trigger reallocation of the offscreen render target
        recreateViewportTarget((uint32_t)m_viewportSize.x, (uint32_t)m_viewportSize.y);
    }

    if (m_viewportTextureId) {
        // Phase 6 maps UV (0,0) to the top-left, matching ImGui's default exactly.
        // Do NOT flip the V coordinate here.
        ImGui::Image((ImTextureID)m_viewportTextureId, currentSize);
    } else {
        ImGui::Text("No output available.");
    }

    ImGui::End();
}

}  // namespace loom::ui
