#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>

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

  private:
    // Guards against double-shutdown if the destructor and an explicit shutdown() call overlap
    bool m_initialized = false;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
};

}  // namespace loom::ui
