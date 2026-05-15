#include "core/Camera.hpp"

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

}  // namespace loom::core
