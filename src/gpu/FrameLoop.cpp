#include "gpu/FrameLoop.hpp"

#include <stdexcept>

#include "core/Assert.hpp"
#include "core/Profile.hpp"
#include "gpu/Device.hpp"
#include "gpu/LayoutTransitions.hpp"
#include "gpu/ResourceFactory.hpp"
#include "gpu/Swapchain.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"

namespace loom::gpu {

namespace core = loom::core;

FrameLoop::FrameLoop(Device& device, Swapchain& swapchain, ResourceFactory& factory,
                     GLFWwindow* window)
    : m_device(device), m_swapchain(swapchain), m_factory(factory), m_window(window) {
    allocateCommandBuffers();
    createSyncObjects();
}

FrameLoop::~FrameLoop() {
    VkDevice device = m_device.get();
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);
    destroySyncObjects();
    // Command buffers are owned by the resource factory's command pool and
    // freed when the pool is destroyed.
}

void FrameLoop::waitIdle() const { vkDeviceWaitIdle(m_device.get()); }

uint64_t FrameLoop::getRetiredFrameValue() const {
    if (m_timeline == VK_NULL_HANDLE) return 0;
    uint64_t value = 0;
    vkGetSemaphoreCounterValue(m_device.get(), m_timeline, &value);
    return value;
}

void FrameLoop::allocateCommandBuffers() {
    m_commandBuffers.resize(core::MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_factory.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    LOOM_VK_CHECK(vkAllocateCommandBuffers(m_device.get(), &allocInfo, m_commandBuffers.data()));
}

void FrameLoop::createSyncObjects() {
    m_imageAvailableSemaphores.resize(core::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    // renderFinished is sized to the safe maximum so a swapchain recreate
    // with a different image count doesn't force semaphore recreation.
    m_renderFinishedSemaphores.resize(core::MAX_SWAPCHAIN_IMAGES, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo binaryInfo{};
    binaryInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (auto& sem : m_imageAvailableSemaphores) {
        LOOM_VK_CHECK(vkCreateSemaphore(m_device.get(), &binaryInfo, nullptr, &sem));
    }
    for (auto& sem : m_renderFinishedSemaphores) {
        LOOM_VK_CHECK(vkCreateSemaphore(m_device.get(), &binaryInfo, nullptr, &sem));
    }

    // Timeline semaphore: starts at 0; first submit signals 1.
    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;

    VkSemaphoreCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    timelineInfo.pNext = &typeInfo;
    LOOM_VK_CHECK(vkCreateSemaphore(m_device.get(), &timelineInfo, nullptr, &m_timeline));
}

void FrameLoop::destroySyncObjects() {
    VkDevice device = m_device.get();
    for (auto sem : m_imageAvailableSemaphores) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
    }
    for (auto sem : m_renderFinishedSemaphores) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
    }
    if (m_timeline != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, m_timeline, nullptr);
        m_timeline = VK_NULL_HANDLE;
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
}

void FrameLoop::recreateSwapchain() { m_swapchain.recreate(); }

VkCommandBuffer FrameLoop::beginFrame() {
    LOOM_PROFILE_SCOPE("FrameLoop::beginFrame");
    // Minimisation guard.
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    if (width == 0 || height == 0) {
        glfwWaitEvents();
        return VK_NULL_HANDLE;
    }

    auto* loomWindow =
        reinterpret_cast<loom::platform::Window*>(glfwGetWindowUserPointer(m_window));
    if (loomWindow->wasResized()) {
        recreateSwapchain();
        loomWindow->resetResizedFlag();
        return VK_NULL_HANDLE;
    }

    // Step A — Wait for the slot's previous frame to retire.
    // Slot (m_frameValue+1) reuses resources first used by frame
    // (m_frameValue+1 - MAX_FRAMES_IN_FLIGHT). Don't wait until at least that
    // many submits have happened.
    if (m_frameValue + 1 > core::MAX_FRAMES_IN_FLIGHT) {
        uint64_t waitValue = m_frameValue + 1 - core::MAX_FRAMES_IN_FLIGHT;
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_timeline;
        waitInfo.pValues = &waitValue;
        LOOM_VK_CHECK(vkWaitSemaphores(m_device.get(), &waitInfo, UINT64_MAX));
    }

    // Step B — Acquire next swapchain image.
    if (!m_swapchain.acquire(m_imageAvailableSemaphores[m_currentFrame], m_currentImageIndex)) {
        recreateSwapchain();
        return VK_NULL_HANDLE;
    }

    // Step C — Reset and begin command buffer.
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    LOOM_VK_CHECK(vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo));

    return m_commandBuffers[m_currentFrame];
}

void FrameLoop::endFrame(VkCommandBuffer cmd, loom::ui::ImGuiRenderer& imgui) {
    LOOM_PROFILE_SCOPE("FrameLoop::endFrame");
    // Step D — Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL.
    transitionImageLayout(cmd, m_swapchain.getImage(m_currentImageIndex), VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Step E — Begin dynamic rendering and let ImGui record its draws.
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchain.getImageView(m_currentImageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = m_swapchain.getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    imgui.endFrame(cmd);
    vkCmdEndRendering(cmd);

    // Step F — Transition to PRESENT_SRC_KHR.
    transitionImageLayout(cmd, m_swapchain.getImage(m_currentImageIndex),
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    LOOM_VK_CHECK(vkEndCommandBuffer(cmd));

    // Step G — Submit with timeline + binary signal.
    const uint64_t newFrameValue = m_frameValue + 1;

    VkSemaphore waitSems[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSems[] = {m_timeline, m_renderFinishedSemaphores[m_currentImageIndex]};
    uint64_t signalValues[] = {newFrameValue, 0};
    // The 0 paired with the binary semaphore is ignored by the driver. The
    // wait-value count must equal waitSemaphoreCount; a single dummy value
    // covers the binary image-available wait.
    uint64_t waitValues[] = {0};

    VkTimelineSemaphoreSubmitInfo timelineSubmit{};
    timelineSubmit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineSubmit.waitSemaphoreValueCount = 1;
    timelineSubmit.pWaitSemaphoreValues = waitValues;
    timelineSubmit.signalSemaphoreValueCount = 2;
    timelineSubmit.pSignalSemaphoreValues = signalValues;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineSubmit;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSems;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores = signalSems;

    LOOM_VK_CHECK(vkQueueSubmit(m_device.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
    m_frameValue = newFrameValue;

    // Step H — Present.
    VkResult presentResult =
        m_swapchain.present(m_device.getPresentQueue(),
                            m_renderFinishedSemaphores[m_currentImageIndex], m_currentImageIndex);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        auto* loomWindow =
            reinterpret_cast<loom::platform::Window*>(glfwGetWindowUserPointer(m_window));
        loomWindow->resetResizedFlag();
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;
}

}  // namespace loom::gpu
