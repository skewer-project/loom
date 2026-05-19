#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>

#include "imgui.h"
#include "vk_mem_alloc.h"

namespace loom::core {
class Camera;
}

namespace loom::ui {

// Which renderer drives the viewport panel for this frame.
//   - Flat2D — the existing DisplayPass path: a viewer ImageHandle gets
//     tone-mapped + display-encoded into the viewport image.
//   - PointCloud3D — Phase B.5's PointCloudPass: the upstream deep payload
//     gets rasterised as a depth-tested per-sample point cloud, with the
//     orbit camera providing view/proj.
enum class ViewportMode : uint8_t {
    Flat2D = 0,
    PointCloud3D = 1,
};

struct ImGuiRendererCreateInfo {
    GLFWwindow* window;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t graphicsQueueFamily;
    VkQueue graphicsQueue;
    VkDescriptorPool descriptorPool;
    VkFormat colorFormat;
    uint32_t imageCount;
    uint32_t minImageCount;
    VmaAllocator vmaAllocator;
    // TODO: Add custom font path and size fields here when
    // the Loom UI design requires a specific typeface.
};

class ImGuiRenderer {
  public:
    ImGuiRenderer() = default;
    ~ImGuiRenderer();

    // Prevent copying — ImGui context is a singleton
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void init(const ImGuiRendererCreateInfo& info);
    void beginFrame();
    void endFrame(VkCommandBuffer cmd);
    void shutdown();

    // Establishes a fullscreen dockspace and generates the default layout
    // if no persistent state exists in imgui.ini. Optionally accepts a
    // pointer to the engine `Camera`: when supplied, the viewport mode
    // dropdown is drawn in the panel header and mouse drag / scroll over
    // the viewport region drive the camera's orbit / zoom. Pass `nullptr`
    // for the existing flat-only behaviour.
    void drawDockspace(core::Camera* orbitCamera = nullptr);

    ImVec2 getViewportSize() const { return m_viewportSize; }

    VkImage getViewportImage() const { return m_viewportImage; }
    VkImageView getViewportImageView() const { return m_viewportImageView; }

    ViewportMode getViewportMode() const { return m_viewportMode; }
    void setViewportMode(ViewportMode mode) { m_viewportMode = mode; }

    // Re-derive orbit state (yaw / pitch / radius) from the camera's
    // current pose. Call after externally repositioning the camera (e.g.
    // `Camera::frameToBounds`) so the orbit controller picks the new
    // pose up cleanly instead of snapping back to its prior orbit state
    // on the next mouse event.
    void resyncOrbitFromCamera(const core::Camera& camera);

  private:
    void createSampler();
    void recreateViewportTarget(uint32_t width, uint32_t height);

    // Guards against double-shutdown if the destructor and an explicit shutdown() call overlap
    bool m_initialized = false;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

    ImVec2 m_viewportSize = {0, 0};

    VkSampler m_viewportSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_viewportTextureId = VK_NULL_HANDLE;
    VkImage m_viewportImage = VK_NULL_HANDLE;
    VkImageView m_viewportImageView = VK_NULL_HANDLE;
    VmaAllocation m_viewportAllocation = VK_NULL_HANDLE;

    ViewportMode m_viewportMode = ViewportMode::Flat2D;

    // Orbit-camera state. Spherical coordinates around the camera target.
    // Initialised lazily on first mutation so unedited cameras keep their
    // explicit `setPosition` / `setTarget` configuration.
    void applyOrbitInput(core::Camera& camera);

    bool m_orbitInitialised = false;
    float m_orbitYaw = 0.0f;     // radians, around world +Y
    float m_orbitPitch = 0.0f;   // radians, around camera-right
    float m_orbitRadius = 3.0f;  // distance from target, meters
};

}  // namespace loom::ui
