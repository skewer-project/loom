#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "core/Constants.hpp"

namespace loom::ui {
class ImGuiRenderer;
}

namespace loom::gpu {

class Device;
class Swapchain;
class ResourceFactory;

// Per-frame command-buffer + synchronisation manager.
//
// Sync design: a single timeline semaphore monotonically advances on every
// queue submit. "Frame N has retired" is `vkGetSemaphoreCounterValue(timeline)
// >= N` rather than the older binary-fence + per-image-fence dance. The
// timeline value is exposed via `currentFrameValue()` for downstream
// retirement signals (Phase 6: BindlessHeap, TransientImagePool).
//
// Binary semaphores remain in two places where the Vulkan API requires them:
//   - `imageAvailable[currentFrame]` is signaled by vkAcquireNextImageKHR
//     (the only signal the call accepts). Indexed by frame slot because the
//     timeline wait at frame start guarantees the prior submit using that slot
//     has retired before we reuse the semaphore.
//   - `renderFinished[imageIndex]` is waited on by vkQueuePresentKHR (likewise
//     the only wait it accepts). Indexed per swapchain image because a single
//     image may be acquired again by a later frame; the present-wait of the
//     previous use must have consumed its signal before the next render
//     submits on that image.
class FrameLoop {
  public:
    FrameLoop(Device& device, Swapchain& swapchain, ResourceFactory& factory, GLFWwindow* window);
    ~FrameLoop();

    FrameLoop(const FrameLoop&) = delete;
    FrameLoop& operator=(const FrameLoop&) = delete;

    // Returns a recording command buffer for the current frame, or
    // VK_NULL_HANDLE if the frame should be skipped (window minimised or
    // swapchain just recreated).
    [[nodiscard]] VkCommandBuffer beginFrame();

    // Closes the command buffer, submits with timeline-semaphore signalling,
    // and presents.
    void endFrame(VkCommandBuffer cmd, loom::ui::ImGuiRenderer& imgui);

    // Monotonically increasing value of the timeline semaphore *after* the
    // last submit. Frame N's work is considered retired when
    // `vkGetSemaphoreCounterValue(timeline) >= N`. Used by deferred-retirement
    // consumers (Phase 6).
    [[nodiscard]] uint64_t currentFrameValue() const { return m_frameValue; }

    // Live query of the timeline counter. Returns the highest frame value the
    // GPU has actually retired. Consumers (BindlessHeap, TransientImagePool)
    // use this to drain entries whose `releaseAtFrame <= retiredValue`.
    [[nodiscard]] uint64_t getRetiredFrameValue() const;

    void waitIdle() const;

  private:
    Device& m_device;
    Swapchain& m_swapchain;
    ResourceFactory& m_factory;
    GLFWwindow* m_window = nullptr;

    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    // Timeline semaphore. Initial value 0; every submit signals
    // m_frameValue+1 and advances m_frameValue.
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    uint64_t m_frameValue = 0;

    uint32_t m_currentFrame = 0;       // Frame slot in [0, MAX_FRAMES_IN_FLIGHT).
    uint32_t m_currentImageIndex = 0;  // Last acquired swapchain image index.

    void allocateCommandBuffers();
    void createSyncObjects();
    void destroySyncObjects();
    void recreateSwapchain();
};

}  // namespace loom::gpu
