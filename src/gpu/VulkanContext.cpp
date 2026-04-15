#include "gpu/VulkanContext.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "imgui.h"
#include "platform/Window.hpp"

namespace loom::gpu {

namespace core = loom::core;
namespace platform = loom::platform;
namespace ui = loom::ui;

namespace {
// Helper to load the debug messenger extension function
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

// Helper to load the debug messenger destruction function
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "Vulkan Validation Layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}
}  // anonymous namespace

VulkanContext::VulkanContext() {}

VulkanContext::~VulkanContext() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        cleanupSwapchain();
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }

        cleanupSyncObjects();

        m_bindlessHeap.reset();
        if (m_vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_vmaAllocator);
            m_vmaAllocator = VK_NULL_HANDLE;
        }

        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }

        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }

        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        if (m_enableValidationLayers && m_debugMessenger != VK_NULL_HANDLE) {
            DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }

        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::init(const loom::platform::Window& window, const char* appName) {
    m_window = window.getNativeWindow();
    createInstance(appName);
    setupDebugMessenger();
    createSurface(m_window);
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createCommandPool();
    allocateCommandBuffers();
    createSyncObjects();
    createDescriptorPool();

    // VMA initialization
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;

    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VMA allocator!");
    }

    m_bindlessHeap = std::make_unique<BindlessHeap>(m_device);
}

void VulkanContext::createInstance(const char* appName) {
    if (m_enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Loom Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef __APPLE__
    // Required bit for macOS/MoltenVK to enumerate devices correctly on latest SDKs
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;

        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan instance!");
    }
}

void VulkanContext::createSurface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    DeviceScore bestScore;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    // Find the device with the highest score
    for (const auto& device : devices) {
        DeviceScore currentScore = rateDeviceSuitability(device);

        if (currentScore.score > bestScore.score) {
            bestScore = currentScore;
            bestDevice = device;
        }
    }

    if (bestScore.score == 0 || bestDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    // Save the device and the queue indices to our class members
    m_physicalDevice = bestDevice;
    m_graphicsQueueFamily = bestScore.graphicsFamily;
    m_computeQueueFamily = bestScore.computeFamily;
    m_presentQueueFamily = bestScore.presentFamily;

    // Debug logging
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    std::cout << "Selected GPU: " << props.deviceName << " (score: " << bestScore.score << ")"
              << std::endl;
}

void VulkanContext::createLogicalDevice() {
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

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature{};
    dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeature.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceSynchronization2Features sync2Features{};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2Features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
    descriptorIndexingFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;

    dynamicRenderingFeature.pNext = &sync2Features;
    sync2Features.pNext = &descriptorIndexingFeatures;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &dynamicRenderingFeature;
    // pNext chain: createInfo -> dynamicRenderingFeature
    //              -> sync2Features -> nullptr
    // Both dynamic rendering and synchronization2 must be
    // explicitly enabled. vkCmdPipelineBarrier2 and
    // VkImageMemoryBarrier2 require synchronization2.
    // Using them without enabling this feature produces
    // validation errors and undefined driver behavior.
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
}

void VulkanContext::createSwapchain() {
    SwapchainSupportDetails swapchainSupport = querySwapchainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapchainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapchainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapchainSupport.capabilities);

    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapchainSupport.capabilities.maxImageCount) {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {m_graphicsQueueFamily, m_presentQueueFamily};

    if (m_graphicsQueueFamily != m_presentQueueFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    createInfo.oldSwapchain = m_oldSwapchain;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain!");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
}

void VulkanContext::createImageViews() {
    m_swapchainImageViews.resize(m_swapchainImages.size());

    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount =
            1;  // >1 is used for stereoscopic 3D — not needed here.

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create image views for swapchain image " +
                                     std::to_string(i) + "!");
        }
    }
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // Allows individual command buffers to be re-recorded
    // each frame without resetting the entire pool.
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
    // Command buffers from this pool can only be submitted
    // to queues from this family.

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void VulkanContext::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}
        // 1000 allows one descriptor per node preview
        // image in the compositor. Expand if needed.
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    // This flag is mandatory to allow external UI systems or node graphs to free individual
    // descriptor sets internally without resetting the entire pool.
    poolInfo.maxSets = 1000;
    // Must be >= the total number of descriptor sets
    // that will ever be allocated from this pool simultaneously.
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;
    // Allocate a temporary one-shot command buffer.
    // This is separate from the per-frame command buffers
    // in m_commandBuffers — it is used only for one-off
    // GPU transfers and freed immediately after submission.

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    // ONE_TIME_SUBMIT tells the driver this buffer is recorded
    // once, submitted once, and never reused. Allows optimization.

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    // vkQueueWaitIdle is a hard sync — the CPU blocks until the
    // GPU finishes. This is acceptable for one-time setup operations
    // like texture uploads. Never use this in the render loop.

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanContext::allocateCommandBuffers() {
    m_commandBuffers.resize(core::MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    // Primary buffers are submitted directly to a queue.
    // Secondary buffers are called from primary buffers — not needed here.
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void VulkanContext::createSyncObjects() {
    m_imageAvailableSemaphores.resize(core::MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(core::MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_swapchainImages.size(), VK_NULL_HANDLE);

    // Size this one to our safe maximum
    m_renderFinishedSemaphores.resize(core::MAX_SWAPCHAIN_IMAGES);

    // Semaphores have no configuration — they are purely a GPU-side signal with no CPU-visible
    // state
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Created in the signaled state so the very first
    // frame does not block waiting on a fence that was never submitted.
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Create frame-linked objects
    for (size_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) !=
                VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create frame synchronization objects!");
        }
    }

    // Create image-linked objects
    for (size_t i = 0; i < core::MAX_SWAPCHAIN_IMAGES; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create image synchronization objects!");
        }
    }
}

void VulkanContext::cleanupSyncObjects() {
    for (size_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }
    for (size_t i = 0; i < core::MAX_SWAPCHAIN_IMAGES; i++) {
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();
}

void VulkanContext::cleanupSwapchain() {
    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();
}

void VulkanContext::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_device);

    m_oldSwapchain = m_swapchain;
    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    // Called when VK_ERROR_OUT_OF_DATE_KHR is returned from the render loop.

    vkDestroySwapchainKHR(m_device, m_oldSwapchain, nullptr);  // safe to destroy now
    m_oldSwapchain = VK_NULL_HANDLE;
}

SwapchainSupportDetails VulkanContext::querySwapchainSupport(VkPhysicalDevice device) {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
                                             details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount,
                                                  details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanContext::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    // TODO: Evaluate VK_FORMAT_B8G8R8A8_UNORM for linear compositing pipeline.
    return availableFormats[0];
}

VkPresentModeKHR VulkanContext::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanContext::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);

        VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    // Check every required extension is in the available list
    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    return required.empty();
}

DeviceScore VulkanContext::rateDeviceSuitability(VkPhysicalDevice device) {
    DeviceScore result;

    if (!checkDeviceExtensionSupport(device)) {
        return result;
    }  // fail fast check

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // Find the required queue families
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
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (result.presentFamily == UINT32_MAX && presentSupport) {
            result.presentFamily = i;
        }

        if (result.isComplete()) {
            break;
        }
    }

    // If it doesn't have the required queues, it's completely unsuitable (score = 0)
    if (!result.isComplete()) {
        return result;
    }

    // Check for swapchain support
    SwapchainSupportDetails swapchainSupport = querySwapchainSupport(device);
    if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty()) {
        return result;
    }

    // Calculate the score
    result.score = 1;  // Base score for meeting minimum requirements

    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        result.score += 1000;
    }

    // can continue adding to the score here (max texture size, VRAM size)
    result.score += deviceProperties.limits.maxImageDimension2D;

    return result;
}

void VulkanContext::setupDebugMessenger() {
    if (!m_enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

bool VulkanContext::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> VulkanContext::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (m_enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#ifdef __APPLE__
    // Required extensions for macOS/MoltenVK
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    return extensions;
}

void VulkanContext::waitIdle() const { vkDeviceWaitIdle(m_device); }

// Dynamic rendering requires manual image layout transitions.
// Swapchain images start in UNDEFINED or PRESENT_SRC_KHR layout
// and must be transitioned to COLOR_ATTACHMENT_OPTIMAL before
// rendering, then back to PRESENT_SRC_KHR before presentation.
// Render passes handled this automatically via initialLayout /
// finalLayout — we now own this responsibility explicitly.
void VulkanContext::transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                          VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = 0;
    }

    if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void VulkanContext::drawFrame(loom::ui::ImGuiRenderer& imgui) {
    // Retrieve Window wrapper class from the GLFW window
    auto loomWindow = reinterpret_cast<loom::platform::Window*>(glfwGetWindowUserPointer(m_window));

    // Check if the window was resized
    if (loomWindow->wasResized()) {
        recreateSwapchain();
        loomWindow->resetResizedFlag();
        return;  // Skip this frame and try again next loop
    }

    // Step A — Wait for previous frame's fence:
    // Block the CPU until the GPU has finished rendering the previous use of this frame slot's
    // resources. UINT64_MAX disables the timeout — wait indefinitely.
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // Step B — Acquire next swapchain image:
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain is no longer compatible with the surface — typically caused by a window resize.
        // Recreate and skip this frame.
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image!");
    }

    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    // Mark the image as now being in use by this frame
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        loomWindow->resetResizedFlag();  // Clear it here too, just in case
        return;
    }

    // Step C — Reset fence AFTER successful acquire:
    // Reset only after confirming we will submit
    // work. Resetting before the acquire result check risks
    // leaving the fence unsignaled if we returned early,
    // causing the next frame to wait forever.
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    // Step E — Reset and begin command buffer:
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    if (vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer!");
    }

    // Step F — Transition image to COLOR_ATTACHMENT_OPTIMAL:
    // The swapchain image starts in an undefined
    // state each frame. Transition it to the layout required
    // for color writes before vkCmdBeginRendering.
    transitionImageLayout(m_commandBuffers[m_currentFrame], m_swapchainImages[imageIndex],
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Step G — Begin dynamic rendering:
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchainImageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    // Dark grey clear color.
    // Replace with black or a compositor-appropriate default
    // once the node editor UI is established.

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = m_swapchainExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(m_commandBuffers[m_currentFrame], &renderingInfo);

    // Step H — Record ImGui draw calls:
    // endFrame calls ImGui::Render() then
    // ImGui_ImplVulkan_RenderDrawData() internally.
    // Translates all ImGui geometry into Vulkan draw calls
    // recorded into the active command buffer.
    // Must be called inside an active vkCmdBeginRendering block.
    imgui.endFrame(m_commandBuffers[m_currentFrame]);

    // Step I — End dynamic rendering:
    vkCmdEndRendering(m_commandBuffers[m_currentFrame]);

    // Step J — Transition image to PRESENT_SRC_KHR:
    // Transition the image to the layout required
    // for presentation. The swapchain will reject images that
    // are not in PRESENT_SRC_KHR when vkQueuePresentKHR is called.
    transitionImageLayout(m_commandBuffers[m_currentFrame], m_swapchainImages[imageIndex],
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // Step K — End command buffer:
    if (vkEndCommandBuffer(m_commandBuffers[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    // Step L — Submit:
    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[imageIndex]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }
    // The fence signals when the GPU finishes
    // this submission. The CPU will wait on it at the start
    // of the next use of this frame slot in Step A.

    // Step M — Present:
    VkSwapchainKHR swapchains[] = {m_swapchain};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        // Recreate here catches both the
        // suboptimal case deferred from Step B and any
        // out-of-date result returned by present itself.
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image!");
    }

    // Step N — Advance frame index. THIS LINE IS MANDATORY:
    // Cycle to the next frame slot.
    // Omitting this line means every frame uses slot 0,
    // breaking the entire frames-in-flight system silently.
    m_currentFrame = (m_currentFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;
}

}  // namespace loom::gpu
