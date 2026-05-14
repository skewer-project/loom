#include "core/ColorManagement.hpp"

#include <algorithm>
#include <cmath>

namespace loom::color {

namespace {

// IEC 61966-2-1 sRGB OETF (linear -> sRGB-encoded).
float sRGB_OETF(float linear) noexcept {
    if (linear <= 0.0031308f) return 12.92f * linear;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// Inverse sRGB OETF (sRGB-encoded -> linear).
float sRGB_InverseOETF(float encoded) noexcept {
    if (encoded <= 0.04045f) return encoded / 12.92f;
    return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

}  // namespace

float linearToDisplay(float v, DisplayTransform t) noexcept {
    // v1 contract: the caller is responsible for clamping HDR values to [0, 1]
    // via tone-mapping before encoding for display. We still clamp here to
    // guarantee the OETF receives in-domain input.
    v = std::clamp(v, 0.0f, 1.0f);
    switch (t) {
        case DisplayTransform::None:
            return v;
        case DisplayTransform::sRGB:
            return sRGB_OETF(v);
        case DisplayTransform::Rec709Gamma22:
            return std::pow(v, 1.0f / 2.2f);
    }
    return v;
}

float displayToLinear(float v, DisplayTransform t) noexcept {
    v = std::clamp(v, 0.0f, 1.0f);
    switch (t) {
        case DisplayTransform::None:
            return v;
        case DisplayTransform::sRGB:
            return sRGB_InverseOETF(v);
        case DisplayTransform::Rec709Gamma22:
            return std::pow(v, 2.2f);
    }
    return v;
}

DisplayTransform pickTransformForSwapchainFormat(uint32_t vkFormatValue) noexcept {
    // VkFormat values for the SRGB variants in the Vulkan core spec. Listed
    // explicitly rather than range-tested because the SRGB enumerants are not
    // a contiguous range. Reference: Vulkan 1.3 Specification, "Format
    // Definitions".
    switch (vkFormatValue) {
        case 15:  // VK_FORMAT_R8_SRGB
        case 22:  // VK_FORMAT_R8G8_SRGB
        case 29:  // VK_FORMAT_R8G8B8_SRGB
        case 36:  // VK_FORMAT_B8G8R8_SRGB
        case 43:  // VK_FORMAT_R8G8B8A8_SRGB
        case 50:  // VK_FORMAT_B8G8R8A8_SRGB
        case 57:  // VK_FORMAT_A8B8G8R8_SRGB_PACK32
            return DisplayTransform::None;
        default:
            return DisplayTransform::sRGB;
    }
}

}  // namespace loom::color
