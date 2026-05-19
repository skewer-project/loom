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

// How mouse gestures over the viewport panel are interpreted.
//   - Pan2D — drag pans the displayed image; scroll wheel zooms (cursor-
//     anchored). Suitable for any 2D viewer flow.
//   - Orbit3D — drag yaw/pitch the active CameraNode; scroll wheel
//     adjusts the orbit radius. Suitable when the viewer shows the output
//     of a 3D renderer (`PointCloudRenderNode`, future splat passes).
//
// Note: this enum no longer chooses *which renderer runs*; that's a graph-
// wiring decision now (per CONVENTIONS §21). It only routes input
// gestures. A user who chooses Orbit3D against a flat 2D output gets
// harmless no-ops (no camera node in the chain to mutate); a user who
// chooses Pan2D against a 3D render pans the rendered framebuffer pixels.
enum class ViewportInputMode : uint8_t {
    Pan2D = 0,
    Orbit3D = 1,
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

    ViewportInputMode getViewportInputMode() const { return m_viewportInputMode; }
    void setViewportInputMode(ViewportInputMode mode) { m_viewportInputMode = mode; }

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

    ViewportInputMode m_viewportInputMode = ViewportInputMode::Pan2D;

    // Orbit-camera state. Spherical coordinates around the camera target.
    // Initialised lazily on first mutation so unedited cameras keep their
    // explicit `setPosition` / `setTarget` configuration.
    void applyOrbitInput(core::Camera& camera);

    bool m_orbitInitialised = false;
    float m_orbitYaw = 0.0f;     // radians, around world +Y
    float m_orbitPitch = 0.0f;   // radians, around camera-right
    float m_orbitRadius = 3.0f;  // distance from target, meters

    // Flat-2D pan / zoom state, applied via ImGui::Image custom UV
    // coords. `m_view2DCenter` is the UV coordinate at the viewport
    // center (default (0.5, 0.5) = image center); `m_view2DZoom` is
    // multiplicative (1.0 = fit, >1 = zoomed in showing less of the
    // image, <1 = zoomed out with letterbox-grey borders). State
    // persists across mode toggles so the user's framing is preserved
    // when bouncing between Flat 2D and PointCloud 3D.
    void applyView2DInput();
    ImVec2 m_view2DCenter = {0.5f, 0.5f};
    float m_view2DZoom = 1.0f;
};

}  // namespace loom::ui
