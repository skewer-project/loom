#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <iostream>
#include <memory>
#include <vector>

#include "core/Constants.hpp"
#include "gpu/BindlessHeap.hpp"
#include "gpu/Device.hpp"
#include "gpu/Instance.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanContext {
  public:
    VulkanContext();
    ~VulkanContext();

    void init(const loom::platform::Window& window, const char* appName);
    void createSwapchain();
    void createImageViews();
    void createCommandPool();
    void allocateCommandBuffers();
    void createSyncObjects();
    void createDescriptorPool();
    void cleanupSwapchain();
    void recreateSwapchain();
    void cleanupSyncObjects();

    void waitIdle() const;  // Called from main() before any destructor runs to ensure the GPU has
                            // finished all in-flight work.

    VkCommandBuffer beginFrame();
    void endFrame(VkCommandBuffer cmd, loom::ui::ImGuiRenderer& imgui);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    VkInstance getVkInstance() const {
        return m_instanceObj ? m_instanceObj->get() : VK_NULL_HANDLE;
    }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkDescriptorPool getDescriptorPool() const {
        return m_descriptorPool;
    }  // Passed to ImGui_ImplVulkan_InitInfo during UI initialization.
    VkFormat getSwapchainImageFormat() const { return m_swapchainImageFormat; }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkCommandPool getCommandPool() const { return m_commandPool; }
    uint32_t getSwapchainImageCount() const {
        return static_cast<uint32_t>(m_swapchainImages.size());
    }

    VmaAllocator getVmaAllocator() const { return m_vmaAllocator; }
    BindlessHeap& getBindlessHeap() { return *m_bindlessHeap; }

  private:
    GLFWwindow* m_window = nullptr;  // Non-owning pointer. The Window object in main() owns the
                                     // GLFW window and outlives VulkanContext.
    std::unique_ptr<Instance> m_instanceObj;
    std::unique_ptr<Device> m_deviceObj;

    // Shadow handles from m_deviceObj for convenience inside this façade.
    // Removed in Phase 5.7 when the rest of VulkanContext is slimmed down.
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    std::unique_ptr<BindlessHeap> m_bindlessHeap;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_computeQueueFamily = UINT32_MAX;
    uint32_t m_presentQueueFamily = UINT32_MAX;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR m_oldSwapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImageView> m_swapchainImageViews;

    // One command buffer per frame in flight. Allocated from m_commandPool and is
    // destroyed implicitly when the pool is destroyed.
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Signaled when the swapchain image is ready to be rendered into
    // GPU-to-GPU signal.
    std::vector<VkSemaphore> m_imageAvailableSemaphores;

    // Signaled when rendering is complete and the image
    // is ready to be presented. GPU-to-GPU signal.
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    // Blocks the CPU from recording the next frame until
    // the GPU has finished the previous use of this frame's resources.
    // CPU-to-GPU signal.
    std::vector<VkFence> m_inFlightFences;

    // Keeps track of which in-flight fence is using which swapchain image
    std::vector<VkFence> m_imagesInFlight;

    // Cycles 0..MAX_FRAMES_IN_FLIGHT-1 each frame.
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;

    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};

}  // namespace loom::gpu
