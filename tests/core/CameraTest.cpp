#include <gtest/gtest.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include "core/Camera.hpp"

namespace core = loom::core;

namespace {

constexpr float kEps = 1e-5f;

bool matNear(const glm::mat4& a, const glm::mat4& b, float eps = kEps) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::abs(a[i][j] - b[i][j]) > eps) return false;
        }
    }
    return true;
}

}  // namespace

TEST(CameraTest, DefaultConstructedIsDirtyUntilFirstQuery) {
    core::Camera cam;
    EXPECT_TRUE(cam.isDirty());

    (void)cam.viewMatrix();
    EXPECT_FALSE(cam.isDirty());
}

TEST(CameraTest, SetPositionFlipsDirty) {
    core::Camera cam;
    (void)cam.viewMatrix();  // clears initial dirty
    EXPECT_FALSE(cam.isDirty());

    cam.setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(cam.isDirty());

    (void)cam.projectionMatrix();
    EXPECT_FALSE(cam.isDirty());
}

TEST(CameraTest, ViewMatrixMatchesLookAtRH) {
    core::Camera cam;
    cam.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    cam.setUp(glm::vec3(0.0f, 1.0f, 0.0f));

    // Hand-computed reference for a camera at (0,0,5) looking at the origin
    // along -Z with world-up +Y in a right-handed system.
    glm::mat4 expected =
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    EXPECT_TRUE(matNear(cam.viewMatrix(), expected));
}

TEST(CameraTest, ViewMatrixTransformsCameraOriginToZero) {
    // The view matrix should map the camera position to the origin in
    // camera space (eye is at (0,0,0) post-transform).
    core::Camera cam;
    const glm::vec3 pos(2.0f, -3.0f, 4.0f);
    cam.setPosition(pos);
    cam.setTarget(glm::vec3(0.0f));

    glm::vec4 cameraSpace = cam.viewMatrix() * glm::vec4(pos, 1.0f);
    EXPECT_NEAR(cameraSpace.x, 0.0f, kEps);
    EXPECT_NEAR(cameraSpace.y, 0.0f, kEps);
    EXPECT_NEAR(cameraSpace.z, 0.0f, kEps);
    EXPECT_NEAR(cameraSpace.w, 1.0f, kEps);
}

TEST(CameraTest, ProjectionMatrixMapsViewportZTo01) {
    // Vulkan clip space: a point at the near plane lands at NDC.z = 0; the
    // far plane at NDC.z = 1. Sanity-check both endpoints for the default
    // 0.1..100 clip range.
    core::Camera cam;
    cam.setFovY(glm::radians(60.0f));
    cam.setAspect(1.0f);
    cam.setClipPlanes(0.1f, 100.0f);

    auto project = [&](float zCameraSpace) {
        glm::vec4 p = cam.projectionMatrix() * glm::vec4(0.0f, 0.0f, zCameraSpace, 1.0f);
        return p.z / p.w;
    };

    // RH convention: looking down -Z, near and far planes are at negative z.
    EXPECT_NEAR(project(-0.1f), 0.0f, 1e-3f);
    EXPECT_NEAR(project(-100.0f), 1.0f, 1e-3f);
}

TEST(CameraTest, ProjectionFlipsYForVulkanClipSpace) {
    // Vulkan's clip-space Y axis points down. We pre-flip Y inside the
    // projection so a world +Y maps to clip -Y. Pin this contract.
    core::Camera cam;
    cam.setFovY(glm::radians(60.0f));
    cam.setAspect(1.0f);
    cam.setClipPlanes(0.1f, 100.0f);

    glm::vec4 p = cam.projectionMatrix() * glm::vec4(0.0f, 1.0f, -1.0f, 1.0f);
    // After Y flip, a +Y world point should produce a -Y clip-space point.
    EXPECT_LT(p.y, 0.0f);
}

TEST(CameraTest, ViewProjIsProjTimesView) {
    core::Camera cam;
    cam.setPosition(glm::vec3(5.0f, 4.0f, 3.0f));
    cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    cam.setFovY(glm::radians(45.0f));
    cam.setAspect(2.0f);

    glm::mat4 manual = cam.projectionMatrix() * cam.viewMatrix();
    EXPECT_TRUE(matNear(cam.viewProj(), manual));
}

TEST(CameraTest, ChangingAspectDoesNotInvalidateView) {
    // Aspect ratio only affects the projection. View is camera-pose only.
    // Pinning this so a future refactor that conflates the two breaks the
    // test rather than silently re-uploading the view matrix every viewport
    // resize.
    core::Camera cam;
    cam.setPosition(glm::vec3(1.0f, 0.0f, 0.0f));
    cam.setTarget(glm::vec3(0.0f));
    glm::mat4 viewBefore = cam.viewMatrix();

    cam.setAspect(3.0f);
    glm::mat4 viewAfter = cam.viewMatrix();

    EXPECT_TRUE(matNear(viewBefore, viewAfter));
}

TEST(CameraTest, FrameToBoundsRecentersTargetAndPositionsOnPositiveZ) {
    core::Camera cam;
    cam.setFovY(glm::radians(60.0f));
    const glm::vec3 center(10.0f, -5.0f, -13.0f);
    cam.frameToBounds(center, /*radius=*/2.0f);

    EXPECT_EQ(cam.target(), center);
    EXPECT_FLOAT_EQ(cam.position().x, center.x);
    EXPECT_FLOAT_EQ(cam.position().y, center.y);
    EXPECT_GT(cam.position().z, center.z);
}

TEST(CameraTest, FrameToBoundsDistanceScalesWithRadius) {
    // Double the radius → double the framing distance for the same FOV /
    // padding. Pin the linear relationship so a future refactor that
    // conflates padding and distance breaks the test.
    core::Camera cam;
    cam.setFovY(glm::radians(60.0f));

    cam.frameToBounds(glm::vec3(0.0f), 1.0f);
    const float dist1 = cam.position().z;

    cam.frameToBounds(glm::vec3(0.0f), 2.0f);
    const float dist2 = cam.position().z;

    EXPECT_NEAR(dist2, 2.0f * dist1, 1e-4f);
}

TEST(CameraTest, FrameToBoundsWidensClipPlanes) {
    // A scene at world Z = -50 with radius 5 should fall inside the post-
    // framing near / far planes — the original (0.1, 100) defaults would
    // clip everything.
    core::Camera cam;
    cam.frameToBounds(glm::vec3(0.0f, 0.0f, -50.0f), 5.0f);

    EXPECT_LT(cam.nearPlane(), cam.farPlane());
    // Camera sits at center + (0, 0, distance), so distance = position.z - center.z.
    const float distance = cam.position().z - cam.target().z;
    EXPECT_LE(cam.nearPlane(), distance - 5.0f + 1e-3f);  // near pulls inside the sphere
    EXPECT_GE(cam.farPlane(), distance + 5.0f);           // far past the sphere
}

TEST(CameraTest, FrameToBoundsIgnoresZeroOrNegativeRadius) {
    core::Camera cam;
    const glm::vec3 origPos = cam.position();
    const glm::vec3 origTgt = cam.target();

    cam.frameToBounds(glm::vec3(100.0f), 0.0f);
    EXPECT_EQ(cam.position(), origPos);
    EXPECT_EQ(cam.target(), origTgt);

    cam.frameToBounds(glm::vec3(100.0f), -3.0f);
    EXPECT_EQ(cam.position(), origPos);
    EXPECT_EQ(cam.target(), origTgt);
}
