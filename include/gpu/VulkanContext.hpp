#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>

namespace loom {

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    void createInstance(const char* appName);
    void setupDebugMessenger();

    VkInstance getVkInstance() const { return m_instance; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

#ifndef NDEBUG
    const bool m_enableValidationLayers = true;
#else
    const bool m_enableValidationLayers = false;
#endif

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
};

} // namespace loom
