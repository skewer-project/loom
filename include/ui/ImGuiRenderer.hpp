#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>

#include "imgui.h"
#include "vk_mem_alloc.h"

namespace loom::ui {

struct ImGuiRendererCreateInfo {
    GLFWwindow* window;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t graphicsQueueFamily;
    VkQueue graphicsQueue;
    VkDescriptorPool descriptorPool;
    VkFormat colorFormat;
    uint32_t imageCount;
    uint32_t minImageCount;
    VmaAllocator vmaAllocator;
    // TODO: Add custom font path and size fields here when
    // the Loom UI design requires a specific typeface.
};

class ImGuiRenderer {
  public:
    ImGuiRenderer() = default;
    ~ImGuiRenderer();

    // Prevent copying — ImGui context is a singleton
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void init(const ImGuiRendererCreateInfo& info);
    void beginFrame();
    void endFrame(VkCommandBuffer cmd);
    void shutdown();

    // Establishes a fullscreen dockspace and generates the default layout
    // if no persistent state exists in imgui.ini.
    void drawDockspace();

    ImVec2 getViewportSize() const { return m_viewportSize; }

    VkImage getViewportImage() const { return m_viewportImage; }
    VkImageView getViewportImageView() const { return m_viewportImageView; }

  private:
    void createSampler();
    void recreateViewportTarget(uint32_t width, uint32_t height);

    // Guards against double-shutdown if the destructor and an explicit shutdown() call overlap
    bool m_initialized = false;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

    ImVec2 m_viewportSize = {0, 0};

    VkSampler m_viewportSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_viewportTextureId = VK_NULL_HANDLE;
    VkImage m_viewportImage = VK_NULL_HANDLE;
    VkImageView m_viewportImageView = VK_NULL_HANDLE;
    VmaAllocation m_viewportAllocation = VK_NULL_HANDLE;
};

}  // namespace loom::ui
