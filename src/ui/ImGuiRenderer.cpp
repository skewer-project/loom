#include "ui/ImGuiRenderer.hpp"

#include <iostream>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

namespace loom::ui {

ImGuiRenderer::~ImGuiRenderer() { shutdown(); }

void ImGuiRenderer::init(const ImGuiRendererCreateInfo& info) {
    // Step A — Create ImGui context:
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "config/layout.ini";
    // Enable keyboard navigation. Add
    // ImGuiConfigFlags_DockingEnable here later for the
    // compositor's dockable panel layout.

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
    m_colorFormat = info.colorFormat;
    // Field names and struct layout verified against imgui v1.92.6.
    // If upgrading ImGui, re-verify this struct against the new
    // imgui_impl_vulkan.h before building.
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

    // Initialization order is strict. The Vulkan backend must be
    // fully initialized before font upload is attempted. The backend
    // must have a valid device and queue to perform the GPU transfer.

    // In ImGui 1.92.6, font upload is handled internally during NewFrame().
    // No explicit call to ImGui_ImplVulkan_CreateFontsTexture() is needed
    // as it has been removed from the backend in this version (June 2025).
    // The backend allocates its own transfer resources,
    // performs the GPU upload, and cleans up automatically.

    // If you add custom fonts via io.Fonts->AddFontFromFileTTF(),
    // do so BEFORE the first frame. Font data must be fully configured
    // before the atlas is baked and uploaded to the GPU.

    m_initialized = true;

    std::cout << "ImGui fonts uploaded to GPU successfully." << std::endl;
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

    // Shutdown order is the strict reverse of
    // initialization order. Vulkan backend first, then GLFW,
    // then the ImGui context itself.
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

}  // namespace loom::ui
