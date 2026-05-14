#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <string_view>
#include <vector>

namespace loom::platform {
class Window;
}

namespace loom::gpu {

// Owns the VkInstance, the debug messenger, and the VkSurfaceKHR. Constructed
// once at engine startup; the surface lives for the lifetime of the window.
class Instance {
  public:
    Instance(const loom::platform::Window& window, std::string_view appName);
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    [[nodiscard]] VkInstance get() const { return m_instance; }
    [[nodiscard]] VkSurfaceKHR getSurface() const { return m_surface; }
    [[nodiscard]] bool validationEnabled() const { return m_enableValidationLayers; }
    [[nodiscard]] const std::vector<const char*>& validationLayers() const {
        return m_validationLayers;
    }

  private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

#ifndef NDEBUG
    static constexpr bool m_enableValidationLayers = true;
#else
    static constexpr bool m_enableValidationLayers = false;
#endif

    const std::vector<const char*> m_validationLayers = {"VK_LAYER_KHRONOS_validation"};

    void createInstance(std::string_view appName);
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    [[nodiscard]] std::vector<const char*> getRequiredExtensions() const;
    [[nodiscard]] bool checkValidationLayerSupport() const;
};

}  // namespace loom::gpu
