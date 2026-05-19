#include <gtest/gtest.h>

#include <glm/vec3.hpp>

#include "core/AABB.hpp"

namespace core = loom::core;

TEST(AABBTest, DefaultIsInvalidWithZeroRadius) {
    core::AABB box;
    EXPECT_FALSE(box.valid);
    EXPECT_EQ(box.radius(), 0.0f);
}

TEST(AABBTest, ExpandOnceCollapsesMinEqualsMax) {
    core::AABB box;
    box.expand(glm::vec3(3.0f, -1.0f, 2.0f));
    EXPECT_TRUE(box.valid);
    EXPECT_EQ(box.min, box.max);
    EXPECT_EQ(box.center(), glm::vec3(3.0f, -1.0f, 2.0f));
    EXPECT_EQ(box.extent(), glm::vec3(0.0f));
    EXPECT_EQ(box.radius(), 0.0f);
}

TEST(AABBTest, ExpandGrowsMonotonically) {
    core::AABB box;
    box.expand(glm::vec3(0.0f, 0.0f, 0.0f));
    box.expand(glm::vec3(2.0f, 4.0f, 6.0f));
    box.expand(glm::vec3(-1.0f, 2.0f, 3.0f));

    EXPECT_EQ(box.min, glm::vec3(-1.0f, 0.0f, 0.0f));
    EXPECT_EQ(box.max, glm::vec3(2.0f, 4.0f, 6.0f));
    EXPECT_EQ(box.center(), glm::vec3(0.5f, 2.0f, 3.0f));
}

TEST(AABBTest, RadiusIsHalfDiagonalLength) {
    // Unit cube → diagonal sqrt(3), half-diagonal sqrt(3)/2.
    core::AABB box;
    box.expand(glm::vec3(0.0f));
    box.expand(glm::vec3(1.0f));
    EXPECT_NEAR(box.radius(), 0.5f * 1.7320508f, 1e-5f);
}
