#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>

#include "platform/Window.hpp"
#include "core/Constants.hpp"
#include "ui/ImGuiRenderer.hpp"

namespace loom {

struct DeviceScore {
    int score = 0;
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t computeFamily  = UINT32_MAX;
    uint32_t presentFamily  = UINT32_MAX;

    // Helper to easily check if we found all necessary queues
    bool isComplete() const {
        return graphicsFamily != UINT32_MAX &&
               computeFamily != UINT32_MAX &&
               presentFamily != UINT32_MAX;
    }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    void init(const loom::Window& window, const char* appName);
    void createInstance(const char* appName);
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createCommandPool();
    void allocateCommandBuffers();
    void createSyncObjects();
    void createDescriptorPool();
    void cleanupSwapchain();
    void recreateSwapchain();
    void cleanupSyncObjects();

    void waitIdle() const; // Called from main() before any destructor runs to ensure the GPU has finished all in-flight work.
    void drawFrame(ImGuiRenderer& imgui);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    VkInstance getVkInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkDescriptorPool getDescriptorPool() const { return m_descriptorPool; } // Passed to ImGui_ImplVulkan_InitInfo during UI initialization.
    VkFormat getSwapchainImageFormat() const { return m_swapchainImageFormat; }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(m_swapchainImages.size()); }

private:
    GLFWwindow* m_window = nullptr; // Non-owning pointer. The Window object in main() owns the GLFW window and outlives VulkanContext.
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_computeQueueFamily  = UINT32_MAX;
    uint32_t m_presentQueueFamily  = UINT32_MAX;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR m_oldSwapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImageView> m_swapchainImageViews;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    // One command buffer per frame in flight. Allocated
    // from m_commandPool — destroyed implicitly when the pool is destroyed.

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    // Signaled when the swapchain image is ready to be
    // rendered into. GPU-to-GPU signal.

    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    // Signaled when rendering is complete and the image
    // is ready to be presented. GPU-to-GPU signal.

    std::vector<VkFence> m_inFlightFences;
    // Blocks the CPU from recording the next frame until
    // the GPU has finished the previous use of this frame's resources.
    // CPU-to-GPU signal.

    // Keeps track of which in-flight fence is using which swapchain image
    std::vector<VkFence> m_imagesInFlight;

    uint32_t m_currentFrame = 0;
    // Cycles 0..MAX_FRAMES_IN_FLIGHT-1 each frame.

#ifndef NDEBUG
    const bool m_enableValidationLayers = true;
#else
    const bool m_enableValidationLayers = false;
#endif

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME // Required for vkCmdPipelineBarrier2 and VkImageMemoryBarrier2 used in image layout transitions.
    #ifdef __APPLE__
        , "VK_KHR_portability_subset"
    #endif
    };

    DeviceScore rateDeviceSuitability(VkPhysicalDevice device);
    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
};

} // namespace loom
