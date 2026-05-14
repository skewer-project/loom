#include "gpu/Swapchain.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#include "core/Assert.hpp"
#include "gpu/Device.hpp"
#include "gpu/Instance.hpp"

namespace loom::gpu {

namespace {

struct SupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SupportDetails querySupport(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    SupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    if (formatCount > 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount,
                                             details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &presentModeCount,
                                                  details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats[0];
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto& m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    VkExtent2D actual = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    actual.width = std::clamp(actual.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    actual.height = std::clamp(actual.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return actual;
}

}  // namespace

Swapchain::Swapchain(Instance& instance, Device& device, GLFWwindow* window)
    : m_instance(instance), m_device(device), m_window(window) {
    createSwapchain(VK_NULL_HANDLE);
    createImageViews();
}

Swapchain::~Swapchain() {
    destroyImageViews();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device.get(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::createSwapchain(VkSwapchainKHR oldSwapchain) {
    auto support = querySupport(m_device.getPhysical(), m_instance.getSurface());
    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    VkExtent2D extent = chooseExtent(support.capabilities, m_window);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_instance.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t graphicsFam = m_device.getGraphicsQueueFamily();
    uint32_t presentFam = m_device.getPresentQueueFamily();
    uint32_t queueFamilyIndices[] = {graphicsFam, presentFam};
    if (graphicsFam != presentFam) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    LOOM_VK_CHECK(vkCreateSwapchainKHR(m_device.get(), &createInfo, nullptr, &m_swapchain));

    uint32_t actualImageCount = 0;
    LOOM_VK_CHECK(vkGetSwapchainImagesKHR(m_device.get(), m_swapchain, &actualImageCount, nullptr));
    m_swapchainImages.resize(actualImageCount);
    LOOM_VK_CHECK(vkGetSwapchainImagesKHR(m_device.get(), m_swapchain, &actualImageCount,
                                          m_swapchainImages.data()));

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
}

void Swapchain::createImageViews() {
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
        createInfo.subresourceRange.layerCount = 1;

        LOOM_VK_CHECK(
            vkCreateImageView(m_device.get(), &createInfo, nullptr, &m_swapchainImageViews[i]));
    }
}

void Swapchain::destroyImageViews() {
    for (auto view : m_swapchainImageViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(m_device.get(), view, nullptr);
    }
    m_swapchainImageViews.clear();
}

void Swapchain::recreate() {
    // Block on minimisation — wait until the window has non-zero extent.
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_device.get());

    VkSwapchainKHR oldSwapchain = m_swapchain;
    destroyImageViews();
    createSwapchain(oldSwapchain);
    createImageViews();
    vkDestroySwapchainKHR(m_device.get(), oldSwapchain, nullptr);
}

bool Swapchain::acquire(VkSemaphore signalSem, uint32_t& outImageIndex) {
    VkResult result = vkAcquireNextImageKHR(m_device.get(), m_swapchain, UINT64_MAX, signalSem,
                                            VK_NULL_HANDLE, &outImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image!");
    }
    return true;
}

VkResult Swapchain::present(VkQueue queue, VkSemaphore waitSem, uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSem;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(queue, &presentInfo);
}

}  // namespace loom::gpu
