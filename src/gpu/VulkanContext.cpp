#include "gpu/VulkanContext.hpp"

#include "core/Assert.hpp"
#include "platform/Window.hpp"

namespace loom::gpu {

VulkanContext::VulkanContext() {}

VulkanContext::~VulkanContext() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        // Teardown order: frame loop (per-frame command buffers + semaphores)
        // -> swapchain -> resource factory (command pool, descriptor pool,
        // VMA, BindlessHeap) -> device -> instance. The explicit resets make
        // the order obvious.
        m_frameLoop.reset();
        m_swapchainObj.reset();
        m_resourceFactory.reset();

        m_device = VK_NULL_HANDLE;
    }

    m_deviceObj.reset();
    m_instanceObj.reset();
}

void VulkanContext::init(const loom::platform::Window& window, const char* appName) {
    m_window = window.getNativeWindow();
    m_instanceObj = std::make_unique<Instance>(window, appName);
    m_deviceObj = std::make_unique<Device>(*m_instanceObj);

    // Mirror handles into the legacy raw members until Phase 5.7 retires them.
    m_physicalDevice = m_deviceObj->getPhysical();
    m_device = m_deviceObj->get();
    m_graphicsQueue = m_deviceObj->getGraphicsQueue();
    m_computeQueue = m_deviceObj->getComputeQueue();
    m_presentQueue = m_deviceObj->getPresentQueue();
    m_graphicsQueueFamily = m_deviceObj->getGraphicsQueueFamily();
    m_computeQueueFamily = m_deviceObj->getComputeQueueFamily();
    m_presentQueueFamily = m_deviceObj->getPresentQueueFamily();

    m_swapchainObj = std::make_unique<Swapchain>(*m_instanceObj, *m_deviceObj, m_window);
    m_resourceFactory = std::make_unique<ResourceFactory>(*m_instanceObj, *m_deviceObj);
    m_frameLoop =
        std::make_unique<FrameLoop>(*m_deviceObj, *m_swapchainObj, *m_resourceFactory, m_window);
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
    return m_resourceFactory->beginSingleTimeCommands();
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    m_resourceFactory->endSingleTimeCommands(commandBuffer);
}

void VulkanContext::waitIdle() const { vkDeviceWaitIdle(m_device); }

}  // namespace loom::gpu
