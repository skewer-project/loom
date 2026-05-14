#include "gpu/VulkanContext.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "core/Assert.hpp"
#include "gpu/LayoutTransitions.hpp"
#include "imgui.h"
#include "platform/Window.hpp"

namespace loom::gpu {

namespace core = loom::core;
namespace platform = loom::platform;
namespace ui = loom::ui;

VulkanContext::VulkanContext() {}

VulkanContext::~VulkanContext() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        m_swapchainObj.reset();
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

        m_device = VK_NULL_HANDLE;
    }

    // Device destructs before Instance so that vkDestroyDevice runs before
    // vkDestroySurfaceKHR / vkDestroyInstance. Explicit resets make the order
    // obvious.
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
    createCommandPool();
    allocateCommandBuffers();
    createSyncObjects();
    createDescriptorPool();

    // VMA initialization
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instanceObj->get();

    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VMA allocator!");
    }

    m_bindlessHeap = std::make_unique<BindlessHeap>(m_device);
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
    LOOM_VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    // ONE_TIME_SUBMIT tells the driver this buffer is recorded
    // once, submitted once, and never reused. Allows optimization.

    LOOM_VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    return commandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    LOOM_VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    LOOM_VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    LOOM_VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));
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
    m_imagesInFlight.resize(m_swapchainObj->getImageCount(), VK_NULL_HANDLE);

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

void VulkanContext::recreateSwapchain() {
    m_swapchainObj->recreate();
    // Image count may have changed; resize the per-image fence tracker.
    m_imagesInFlight.assign(m_swapchainObj->getImageCount(), VK_NULL_HANDLE);
}

void VulkanContext::waitIdle() const { vkDeviceWaitIdle(m_device); }

VkCommandBuffer VulkanContext::beginFrame() {
    // Minimization Guard: Check the GLFW window size.
    // If width or height is 0, call glfwWaitEvents() and return VK_NULL_HANDLE.
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    if (width == 0 || height == 0) {
        glfwWaitEvents();
        return VK_NULL_HANDLE;
    }

    // Retrieve Window wrapper class from the GLFW window
    auto loomWindow = reinterpret_cast<loom::platform::Window*>(glfwGetWindowUserPointer(m_window));

    // Check if the window was resized
    if (loomWindow->wasResized()) {
        recreateSwapchain();
        loomWindow->resetResizedFlag();
        return VK_NULL_HANDLE;  // Skip this frame and try again next loop
    }

    // Step A — Wait for previous frame's fence:
    // Block the CPU until the GPU has finished rendering the previous use of this frame slot's
    // resources. UINT64_MAX disables the timeout — wait indefinitely.
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // Step B — Acquire next swapchain image:
    if (!m_swapchainObj->acquire(m_imageAvailableSemaphores[m_currentFrame], m_currentImageIndex)) {
        // Surface out-of-date — typically caused by a window resize.
        recreateSwapchain();
        return VK_NULL_HANDLE;
    }

    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if (m_imagesInFlight[m_currentImageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_device, 1, &m_imagesInFlight[m_currentImageIndex], VK_TRUE, UINT64_MAX);
    }
    // Mark the image as now being in use by this frame
    m_imagesInFlight[m_currentImageIndex] = m_inFlightFences[m_currentFrame];

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

    return m_commandBuffers[m_currentFrame];
}

void VulkanContext::endFrame(VkCommandBuffer cmd, loom::ui::ImGuiRenderer& imgui) {
    // Step F — Transition image to COLOR_ATTACHMENT_OPTIMAL:
    // The swapchain image starts in an undefined
    // state each frame. Transition it to the layout required
    // for color writes before vkCmdBeginRendering.
    transitionImageLayout(cmd, m_swapchainObj->getImage(m_currentImageIndex),
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Step G — Begin dynamic rendering:
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchainObj->getImageView(m_currentImageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    // Dark grey clear color.

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = m_swapchainObj->getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // Step H — Record ImGui draw calls:
    // endFrame calls ImGui::Render() then
    // ImGui_ImplVulkan_RenderDrawData() internally.
    imgui.endFrame(cmd);

    // Step I — End dynamic rendering:
    vkCmdEndRendering(cmd);

    // Step J — Transition image to PRESENT_SRC_KHR:
    transitionImageLayout(cmd, m_swapchainObj->getImage(m_currentImageIndex),
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // Step K — End command buffer:
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    // Step L — Submit:
    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentImageIndex]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    // Step M — Present:
    VkResult presentResult =
        m_swapchainObj->present(m_presentQueue, signalSemaphores[0], m_currentImageIndex);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        auto loomWindow =
            reinterpret_cast<loom::platform::Window*>(glfwGetWindowUserPointer(m_window));
        loomWindow->resetResizedFlag();  // Ensure flag is set for next frame
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image!");
    }

    // Step N — Advance frame index:
    m_currentFrame = (m_currentFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;
}

}  // namespace loom::gpu
