#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>

namespace loom {

struct DeviceScore {
    int score = 0;
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t computeFamily  = UINT32_MAX;

    // Helper to easily check if we found all necessary queues
    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && computeFamily != UINT32_MAX;
    }
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    void createInstance(const char* appName);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();

    VkInstance getVkInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_computeQueueFamily  = UINT32_MAX;

#ifndef NDEBUG
    const bool m_enableValidationLayers = true;
#else
    const bool m_enableValidationLayers = false;
#endif

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> m_deviceExtensions = {
    #ifdef __APPLE__
        "VK_KHR_portability_subset"
    #endif
    };

    DeviceScore rateDeviceSuitability(VkPhysicalDevice device);
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
};

} // namespace loom
