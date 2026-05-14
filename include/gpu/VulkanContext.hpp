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
#include "gpu/FrameLoop.hpp"
#include "gpu/Instance.hpp"
#include "gpu/ResourceFactory.hpp"
#include "gpu/Swapchain.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"
#include "vk_mem_alloc.h"

namespace loom::gpu {

class VulkanContext {
  public:
    VulkanContext();
    ~VulkanContext();

    void init(const loom::platform::Window& window, const char* appName);

    void waitIdle() const;  // Called from main() before any destructor runs to ensure the GPU has
                            // finished all in-flight work.

    VkCommandBuffer beginFrame() { return m_frameLoop->beginFrame(); }
    void endFrame(VkCommandBuffer cmd, loom::ui::ImGuiRenderer& imgui) {
        m_frameLoop->endFrame(cmd, imgui);
    }

    // Monotonically increasing value of the frame-loop timeline semaphore
    // after the last submit. Exposed for deferred-retirement consumers
    // (BindlessHeap, TransientImagePool) in Phase 6.
    [[nodiscard]] uint64_t currentFrameValue() const { return m_frameLoop->currentFrameValue(); }

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    VkInstance getVkInstance() const {
        return m_instanceObj ? m_instanceObj->get() : VK_NULL_HANDLE;
    }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkDescriptorPool getDescriptorPool() const {
        return m_resourceFactory ? m_resourceFactory->getDescriptorPool() : VK_NULL_HANDLE;
    }  // Passed to ImGui_ImplVulkan_InitInfo during UI initialization.
    VkFormat getSwapchainImageFormat() const {
        return m_swapchainObj ? m_swapchainObj->getFormat() : VK_FORMAT_UNDEFINED;
    }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkCommandPool getCommandPool() const {
        return m_resourceFactory ? m_resourceFactory->getCommandPool() : VK_NULL_HANDLE;
    }
    uint32_t getSwapchainImageCount() const {
        return m_swapchainObj ? m_swapchainObj->getImageCount() : 0;
    }

    VmaAllocator getVmaAllocator() const {
        return m_resourceFactory ? m_resourceFactory->getVmaAllocator() : VK_NULL_HANDLE;
    }
    BindlessHeap& getBindlessHeap() { return m_resourceFactory->getBindlessHeap(); }

  private:
    GLFWwindow* m_window = nullptr;  // Non-owning pointer. The Window object in main() owns the
                                     // GLFW window and outlives VulkanContext.
    std::unique_ptr<Instance> m_instanceObj;
    std::unique_ptr<Device> m_deviceObj;
    std::unique_ptr<Swapchain> m_swapchainObj;
    std::unique_ptr<ResourceFactory> m_resourceFactory;
    std::unique_ptr<FrameLoop> m_frameLoop;

    // Shadow handles from m_deviceObj for convenience inside this façade.
    // Removed in Phase 5.7 when the rest of VulkanContext is slimmed down.
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_computeQueueFamily = UINT32_MAX;
    uint32_t m_presentQueueFamily = UINT32_MAX;
};

}  // namespace loom::gpu
