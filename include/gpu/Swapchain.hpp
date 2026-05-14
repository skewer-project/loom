#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <vector>

namespace loom::gpu {

class Instance;
class Device;

// Owns the VkSwapchainKHR, swapchain images + image views, and the format /
// extent metadata. The frame loop talks to Swapchain via acquire / present
// and asks for a recreate when the surface is out of date.
class Swapchain {
  public:
    Swapchain(Instance& instance, Device& device, GLFWwindow* window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Returns true on success, false if VK_ERROR_OUT_OF_DATE_KHR was observed
    // (caller should recreate and retry). Other failures throw.
    [[nodiscard]] bool acquire(VkSemaphore signalSem, uint32_t& outImageIndex);

    // Returns VkResult so the caller can detect OUT_OF_DATE / SUBOPTIMAL and
    // schedule a recreate.
    [[nodiscard]] VkResult present(VkQueue queue, VkSemaphore waitSem, uint32_t imageIndex);

    // Tears down + rebuilds the swapchain (e.g. after a window resize). Waits
    // on the device idle first.
    void recreate();

    [[nodiscard]] VkSwapchainKHR get() const { return m_swapchain; }
    [[nodiscard]] VkFormat getFormat() const { return m_swapchainImageFormat; }
    [[nodiscard]] VkExtent2D getExtent() const { return m_swapchainExtent; }
    [[nodiscard]] uint32_t getImageCount() const {
        return static_cast<uint32_t>(m_swapchainImages.size());
    }
    [[nodiscard]] VkImage getImage(uint32_t i) const { return m_swapchainImages[i]; }
    [[nodiscard]] VkImageView getImageView(uint32_t i) const { return m_swapchainImageViews[i]; }

  private:
    Instance& m_instance;
    Device& m_device;
    GLFWwindow* m_window = nullptr;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{0, 0};

    void createSwapchain(VkSwapchainKHR oldSwapchain);
    void createImageViews();
    void destroyImageViews();
};

}  // namespace loom::gpu
