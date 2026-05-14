#include <gtest/gtest.h>

#include <cmath>

#include "core/ColorManagement.hpp"

using loom::color::displayToLinear;
using loom::color::DisplayTransform;
using loom::color::linearToDisplay;
using loom::color::pickTransformForSwapchainFormat;

namespace {

constexpr float kEpsilon = 1e-4f;

bool approxEqual(float a, float b, float eps = kEpsilon) noexcept {
    return std::fabs(a - b) <= eps;
}

}  // namespace

TEST(ColorManagementTest, NoneIsIdentity) {
    for (float v : {0.0f, 0.18f, 0.5f, 0.999f, 1.0f}) {
        EXPECT_FLOAT_EQ(linearToDisplay(v, DisplayTransform::None), v);
        EXPECT_FLOAT_EQ(displayToLinear(v, DisplayTransform::None), v);
    }
}

TEST(ColorManagementTest, LinearToSRGBRoundTrip) {
    for (float v : {0.0f, 0.04f, 0.18f, 0.5f, 0.99f, 1.0f}) {
        const float encoded = linearToDisplay(v, DisplayTransform::sRGB);
        const float decoded = displayToLinear(encoded, DisplayTransform::sRGB);
        EXPECT_TRUE(approxEqual(decoded, v))
            << "round-trip failed: v=" << v << " encoded=" << encoded << " decoded=" << decoded;
    }
}

TEST(ColorManagementTest, SRGBKnownValues) {
    // 0.18 linear is the canonical mid-gray reference (≈ 0.46 sRGB-encoded).
    EXPECT_NEAR(linearToDisplay(0.18f, DisplayTransform::sRGB), 0.4613f, 1e-3f);
    // 0.0 and 1.0 are fixed points.
    EXPECT_FLOAT_EQ(linearToDisplay(0.0f, DisplayTransform::sRGB), 0.0f);
    EXPECT_NEAR(linearToDisplay(1.0f, DisplayTransform::sRGB), 1.0f, 1e-3f);
}

TEST(ColorManagementTest, Rec709Gamma22RoundTrip) {
    for (float v : {0.05f, 0.18f, 0.5f, 0.9f}) {
        const float encoded = linearToDisplay(v, DisplayTransform::Rec709Gamma22);
        const float decoded = displayToLinear(encoded, DisplayTransform::Rec709Gamma22);
        EXPECT_TRUE(approxEqual(decoded, v)) << "round-trip failed: v=" << v;
    }
}

TEST(ColorManagementTest, SRGBClampingInputDomain) {
    // Out-of-domain inputs (HDR, negative) clamp to [0, 1] before encoding.
    EXPECT_FLOAT_EQ(linearToDisplay(2.0f, DisplayTransform::sRGB),
                    linearToDisplay(1.0f, DisplayTransform::sRGB));
    EXPECT_FLOAT_EQ(linearToDisplay(-0.5f, DisplayTransform::sRGB),
                    linearToDisplay(0.0f, DisplayTransform::sRGB));
}

TEST(ColorManagementTest, PickTransformForSwapchainFormat) {
    // VkFormat values from Vulkan 1.3 spec, "Format Definitions".
    constexpr uint32_t VK_FORMAT_B8G8R8A8_SRGB = 50;
    constexpr uint32_t VK_FORMAT_R8G8B8A8_SRGB = 43;
    constexpr uint32_t VK_FORMAT_B8G8R8A8_UNORM = 44;
    constexpr uint32_t VK_FORMAT_R8G8B8A8_UNORM = 37;

    EXPECT_EQ(pickTransformForSwapchainFormat(VK_FORMAT_B8G8R8A8_SRGB), DisplayTransform::None);
    EXPECT_EQ(pickTransformForSwapchainFormat(VK_FORMAT_R8G8B8A8_SRGB), DisplayTransform::None);
    EXPECT_EQ(pickTransformForSwapchainFormat(VK_FORMAT_B8G8R8A8_UNORM), DisplayTransform::sRGB);
    EXPECT_EQ(pickTransformForSwapchainFormat(VK_FORMAT_R8G8B8A8_UNORM), DisplayTransform::sRGB);
}
