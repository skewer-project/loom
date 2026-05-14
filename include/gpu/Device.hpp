#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "core/Assert.hpp"

namespace loom::gpu {

class Instance;

// Owns the physical device, logical device, and queue handles. Explicitly
// requests the Vulkan 1.3 features Loom relies on (synchronization2,
// dynamicRendering, timelineSemaphore, descriptorIndexing). A device that
// cannot satisfy the requested features is rejected at construction with a
// contextual error.
class Device {
  public:
    explicit Device(Instance& instance);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] VkPhysicalDevice getPhysical() const { return m_physicalDevice; }
    [[nodiscard]] VkDevice get() const { return m_device; }

    [[nodiscard]] VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] VkQueue getComputeQueue() const { return m_computeQueue; }
    [[nodiscard]] VkQueue getPresentQueue() const { return m_presentQueue; }

    [[nodiscard]] uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    [[nodiscard]] uint32_t getComputeQueueFamily() const { return m_computeQueueFamily; }
    [[nodiscard]] uint32_t getPresentQueueFamily() const { return m_presentQueueFamily; }

    void waitIdle() const { vkDeviceWaitIdle(m_device); }

  private:
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_computeQueueFamily = UINT32_MAX;
    uint32_t m_presentQueueFamily = UINT32_MAX;

    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
    };

    void pickPhysicalDevice(Instance& instance);
    void createLogicalDevice(Instance& instance);
    [[nodiscard]] bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
};

}  // namespace loom::gpu
