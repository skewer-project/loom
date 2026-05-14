#include "gpu/VulkanContext.hpp"

#include "platform/Window.hpp"

namespace loom::gpu {

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    // Wait for the GPU to idle before tearing anything down, then destroy in
    // reverse construction order. Explicit resets pin the order against
    // accidental member reordering.
    if (m_device) {
        m_device->waitIdle();
    }
    m_frameLoop.reset();
    m_swapchain.reset();
    m_resourceFactory.reset();
    m_device.reset();
    m_instance.reset();
}

void VulkanContext::init(const loom::platform::Window& window, const char* appName) {
    m_instance = std::make_unique<Instance>(window, appName);
    m_device = std::make_unique<Device>(*m_instance);
    m_swapchain = std::make_unique<Swapchain>(*m_instance, *m_device, window.getNativeWindow());
    m_resourceFactory = std::make_unique<ResourceFactory>(*m_instance, *m_device);
    m_frameLoop = std::make_unique<FrameLoop>(*m_device, *m_swapchain, *m_resourceFactory,
                                              window.getNativeWindow());
}

void VulkanContext::waitIdle() const {
    if (m_device) m_device->waitIdle();
}

}  // namespace loom::gpu
