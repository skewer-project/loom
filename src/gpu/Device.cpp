#include "gpu/Device.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Log.hpp"
#include "gpu/Instance.hpp"

namespace loom::gpu {

namespace {

struct DeviceScore {
    int score = 0;
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t computeFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;

    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && computeFamily != UINT32_MAX &&
               presentFamily != UINT32_MAX;
    }
};

DeviceScore rateDeviceSuitability(VkPhysicalDevice device, VkSurfaceKHR surface,
                                  const std::vector<const char*>& requiredExtensions) {
    DeviceScore result;

    // Extension support
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());
    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    if (!required.empty()) return result;

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    // Queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (result.graphicsFamily == UINT32_MAX &&
            queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            result.graphicsFamily = i;
        }
        if (result.computeFamily == UINT32_MAX &&
            queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            result.computeFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (result.presentFamily == UINT32_MAX && presentSupport) {
            result.presentFamily = i;
        }

        if (result.isComplete()) break;
    }

    if (!result.isComplete()) return result;

    // Swapchain support (any format / any present mode)
    uint32_t formatCount = 0, presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (formatCount == 0 || presentModeCount == 0) return result;

    // Feature support (sync2, dynamicRendering, timelineSemaphore, descriptorIndexing)
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceVulkan12Features v12{};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features v13{};
    v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    features2.pNext = &v12;
    v12.pNext = &v13;

    vkGetPhysicalDeviceFeatures2(device, &features2);

    if (!v13.synchronization2 || !v13.dynamicRendering) return result;
    if (!v12.timelineSemaphore || !v12.descriptorIndexing || !v12.descriptorBindingPartiallyBound ||
        !v12.runtimeDescriptorArray) {
        return result;
    }

    result.score = 1;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        result.score += 1000;
    }
    result.score += deviceProperties.limits.maxImageDimension2D;
    return result;
}

}  // namespace

Device::Device(Instance& instance) {
    pickPhysicalDevice(instance);
    createLogicalDevice(instance);
}

Device::~Device() {
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
}

void Device::pickPhysicalDevice(Instance& instance) {
    VkInstance vkInstance = instance.get();
    VkSurfaceKHR surface = instance.getSurface();

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());

    DeviceScore bestScore;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    for (const auto& device : devices) {
        DeviceScore currentScore = rateDeviceSuitability(device, surface, m_deviceExtensions);
        if (currentScore.score > bestScore.score) {
            bestScore = currentScore;
            bestDevice = device;
        }
    }

    if (bestScore.score == 0 || bestDevice == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "failed to find a suitable GPU (missing required VK 1.3 features or extensions)!");
    }

    m_physicalDevice = bestDevice;
    m_graphicsQueueFamily = bestScore.graphicsFamily;
    m_computeQueueFamily = bestScore.computeFamily;
    m_presentQueueFamily = bestScore.presentFamily;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    loom::log::info("Selected GPU: ", props.deviceName, " (score: ", bestScore.score, ")");
}

void Device::createLogicalDevice(Instance& instance) {
    LOOM_ASSERT(m_graphicsQueueFamily != UINT32_MAX, "graphics queue family must be set");
    LOOM_ASSERT(m_computeQueueFamily != UINT32_MAX, "compute queue family must be set");
    LOOM_ASSERT(m_presentQueueFamily != UINT32_MAX, "present queue family must be set");

    std::set<uint32_t> uniqueQueueFamilies = {m_graphicsQueueFamily, m_computeQueueFamily,
                                              m_presentQueueFamily};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Request the full set of VK 1.3 features Loom depends on. Using the
    // Vulkan12Features / Vulkan13Features umbrella structs means we don't
    // have to chain the individual feature structs.
    VkPhysicalDeviceFeatures deviceFeatures{};

    VkPhysicalDeviceVulkan12Features v12{};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12.timelineSemaphore = VK_TRUE;
    v12.descriptorIndexing = VK_TRUE;
    v12.descriptorBindingPartiallyBound = VK_TRUE;
    v12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    v12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    v12.runtimeDescriptorArray = VK_TRUE;

    VkPhysicalDeviceVulkan13Features v13{};
    v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    v13.synchronization2 = VK_TRUE;
    v13.dynamicRendering = VK_TRUE;

    v12.pNext = &v13;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &v12;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (instance.validationEnabled()) {
        const auto& layers = instance.validationLayers();
        createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        createInfo.ppEnabledLayerNames = layers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    LOOM_VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
}

bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());
    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    return required.empty();
}

}  // namespace loom::gpu
