#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <memory>

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

// Composition root for the Vulkan stack. Owns the subsystems and forwards
// the public API; carries no Vulkan state of its own.
class VulkanContext {
  public:
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void init(const loom::platform::Window& window, const char* appName);

    // Called from main() before any destructor runs to ensure the GPU has
    // finished all in-flight work.
    void waitIdle() const;

    [[nodiscard]] VkCommandBuffer beginFrame() { return m_frameLoop->beginFrame(); }
    void endFrame(VkCommandBuffer cmd, loom::ui::ImGuiRenderer& imgui) {
        m_frameLoop->endFrame(cmd, imgui);
    }

    // Monotonically increasing value of the frame-loop timeline semaphore
    // after the last submit. Exposed for deferred-retirement consumers
    // (BindlessHeap, TransientImagePool) in Phase 6.
    [[nodiscard]] uint64_t currentFrameValue() const { return m_frameLoop->currentFrameValue(); }

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() {
        return m_resourceFactory->beginSingleTimeCommands();
    }
    void endSingleTimeCommands(VkCommandBuffer cmd) {
        m_resourceFactory->endSingleTimeCommands(cmd);
    }

    [[nodiscard]] VkInstance getVkInstance() const { return m_instance->get(); }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_device->getPhysical(); }
    [[nodiscard]] VkDevice getDevice() const { return m_device->get(); }

    [[nodiscard]] VkQueue getGraphicsQueue() const { return m_device->getGraphicsQueue(); }
    [[nodiscard]] uint32_t getGraphicsQueueFamily() const {
        return m_device->getGraphicsQueueFamily();
    }

    [[nodiscard]] VkDescriptorPool getDescriptorPool() const {
        return m_resourceFactory->getDescriptorPool();
    }
    [[nodiscard]] VkCommandPool getCommandPool() const {
        return m_resourceFactory->getCommandPool();
    }
    [[nodiscard]] VmaAllocator getVmaAllocator() const {
        return m_resourceFactory->getVmaAllocator();
    }
    [[nodiscard]] BindlessHeap& getBindlessHeap() { return m_resourceFactory->getBindlessHeap(); }

    [[nodiscard]] VkFormat getSwapchainImageFormat() const { return m_swapchain->getFormat(); }
    [[nodiscard]] uint32_t getSwapchainImageCount() const { return m_swapchain->getImageCount(); }

  private:
    std::unique_ptr<Instance> m_instance;
    std::unique_ptr<Device> m_device;
    std::unique_ptr<Swapchain> m_swapchain;
    std::unique_ptr<ResourceFactory> m_resourceFactory;
    std::unique_ptr<FrameLoop> m_frameLoop;
};

}  // namespace loom::gpu
