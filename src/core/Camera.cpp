#include "core/Camera.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace loom::core {

void Camera::rebuildIfDirty() const {
    if (!m_dirty) return;

    m_view = glm::lookAtRH(m_position, m_target, m_up);
    // Vulkan clip space has +Y down and Z in [0, 1]. GLM's
    // perspectiveRH_ZO matches the Z range; we flip Y inside the
    // projection by negating row 1 column 1, avoiding any reliance on a
    // negative-height VkViewport (which causes inconsistent winding-order
    // semantics across drivers).
    m_proj = glm::perspectiveRH_ZO(m_fovY, m_aspect, m_near, m_far);
    m_proj[1][1] *= -1.0f;
    m_dirty = false;
}

const glm::mat4& Camera::viewMatrix() const {
    rebuildIfDirty();
    return m_view;
}

const glm::mat4& Camera::projectionMatrix() const {
    rebuildIfDirty();
    return m_proj;
}

glm::mat4 Camera::viewProj() const {
    rebuildIfDirty();
    return m_proj * m_view;
}

void Camera::frameToBounds(glm::vec3 center, float radius, float padding) noexcept {
    if (!(radius > 0.0f) || !std::isfinite(radius)) return;

    // Sphere-fit distance: at the camera's vertical half-FOV the sphere
    // exactly fills the viewport when distance = radius / sin(fov/2).
    // Padding > 1 leaves headroom around the silhouette so a quarter-turn
    // orbit stays inside the frame.
    const float halfFov = 0.5f * m_fovY;
    const float sinHalf = std::sin(halfFov);
    const float distance = padding * radius / std::max(sinHalf, 1.0e-4f);

    m_target = center;
    m_position = center + glm::vec3(0.0f, 0.0f, distance);
    m_up = glm::vec3(0.0f, 1.0f, 0.0f);

    // Widen clip planes to fit the bounding sphere with a comfortable
    // margin. Default near = 0.1 hides geometry close to the camera when
    // the scene sits a hundred meters out; default far = 100 clips the
    // back of a kilometer-scale scene. Both adapt to distance ± radius.
    const float nearPlane = std::max(distance - 2.0f * radius, 0.01f);
    const float farPlane = distance + 4.0f * radius;
    m_near = nearPlane;
    m_far = farPlane;

    m_dirty = true;
}

}  // namespace loom::core
