#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace loom::core {

// Right-handed, Y-up, meters perspective camera.
//
// Coordinate convention is fixed by docs/CONVENTIONS.md §19 — same as Vulkan
// / glTF / Blender. Files declaring a different convention via the
// `loom/coordSystem` EXR header attribute are transformed at the loader
// boundary; the engine's internal math always sees RH/Y-up/meters.
//
// All setters mark the camera dirty; `viewMatrix()` / `projectionMatrix()`
// rebuild lazily on the next call. This keeps the orbit-controller (Phase
// B.6) cheap when only one of (position, target, fovY, aspect) changes per
// frame.
//
// The class is single-threaded as of this branch (CONVENTIONS §13). The UI
// thread owns the camera; eval / display pull `const Camera*` through
// `EvaluationContext`.
class Camera {
  public:
    Camera() = default;

    [[nodiscard]] const glm::vec3& position() const noexcept { return m_position; }
    [[nodiscard]] const glm::vec3& target() const noexcept { return m_target; }
    [[nodiscard]] const glm::vec3& up() const noexcept { return m_up; }
    [[nodiscard]] float fovY() const noexcept { return m_fovY; }
    [[nodiscard]] float nearPlane() const noexcept { return m_near; }
    [[nodiscard]] float farPlane() const noexcept { return m_far; }
    [[nodiscard]] float aspect() const noexcept { return m_aspect; }

    void setPosition(glm::vec3 p) noexcept {
        m_position = p;
        m_dirty = true;
    }
    void setTarget(glm::vec3 t) noexcept {
        m_target = t;
        m_dirty = true;
    }
    void setUp(glm::vec3 u) noexcept {
        m_up = u;
        m_dirty = true;
    }
    void setFovY(float radians) noexcept {
        m_fovY = radians;
        m_dirty = true;
    }
    void setClipPlanes(float nearPlane, float farPlane) noexcept {
        m_near = nearPlane;
        m_far = farPlane;
        m_dirty = true;
    }
    void setAspect(float a) noexcept {
        m_aspect = a;
        m_dirty = true;
    }

    // True when any setter has fired since the last `viewMatrix` /
    // `projectionMatrix` recompute. Callers that mirror camera state into a
    // uniform buffer can use `isDirty()` to skip the upload on no-change
    // frames — but the matrices themselves are always valid (the rebuild
    // runs lazily inside the accessor on the first read after a change).
    [[nodiscard]] bool isDirty() const noexcept { return m_dirty; }

    // RH view matrix (camera-to-world inverse). Recomputed lazily on dirty.
    [[nodiscard]] const glm::mat4& viewMatrix() const;

    // RH perspective projection matrix. Recomputed lazily on dirty. Vulkan
    // clip space has +Y down and +Z in [0, 1]; we flip Y in the projection
    // so a +Y-up world maps correctly to the swapchain without a
    // VkViewport.height negation.
    [[nodiscard]] const glm::mat4& projectionMatrix() const;

    // Convenience: viewProj() = projectionMatrix() * viewMatrix().
    // Both halves rebuild on dirty; the product is not cached separately —
    // the cost of one mat4 multiply per frame is below profiling noise.
    [[nodiscard]] glm::mat4 viewProj() const;

  private:
    void rebuildIfDirty() const;

    glm::vec3 m_position{0.0f, 0.0f, 3.0f};
    glm::vec3 m_target{0.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    float m_fovY = 1.0471975512f;  // 60° in radians
    float m_near = 0.1f;
    float m_far = 100.0f;
    float m_aspect = 16.0f / 9.0f;

    mutable bool m_dirty = true;
    mutable glm::mat4 m_view{1.0f};
    mutable glm::mat4 m_proj{1.0f};
};

}  // namespace loom::core
