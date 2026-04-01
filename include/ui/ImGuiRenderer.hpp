#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <cstdint>

namespace loom {

struct ImGuiRendererCreateInfo {
    GLFWwindow*      window;
    VkInstance       instance;
    VkPhysicalDevice physicalDevice;
    VkDevice         device;
    uint32_t         graphicsQueueFamily;
    VkQueue          graphicsQueue;
    VkRenderPass     renderPass;
    VkDescriptorPool descriptorPool;
    uint32_t         imageCount;
    uint32_t         minImageCount;
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
    bool m_initialized = false;
    // Guards against double-shutdown if the
    // destructor and an explicit shutdown() call overlap.
};

} // namespace loom
