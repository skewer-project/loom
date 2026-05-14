#pragma once

#include <cstdint>

// Loom color management.
//
// All internal compositing math is linear scene-referred float. Nodes consume
// linear, produce linear. The display transform — the OETF that encodes linear
// values for the swapchain's color space — is applied exclusively in
// DisplayPass.
//
// This file is the abstraction surface. v1 is a hand-rolled implementation
// (sRGB OETF, Rec.709 gamma 2.2). Future PRs may swap the backend to
// OpenColorIO without touching call sites.

namespace loom::color {

// Color space tags. v1 production only uses Linear scene-referred values
// internally; the other entries exist so call sites can be expressive.
enum class Space : uint8_t {
    Linear = 0,  // Scene-referred linear (working space)
    sRGB = 1,    // sRGB encoded display values
    Rec709 = 2,  // Rec.709 (gamma 2.2 approximation)
};

// Display OETF (opto-electrical transfer function). Selects the curve applied
// to tone-mapped linear values when writing to the swapchain.
enum class DisplayTransform : uint32_t {
    None = 0,           // Identity. Correct when the swapchain is *_SRGB
                        // (hardware applies sRGB encoding on write).
    sRGB = 1,           // Apply sRGB OETF. Correct when the swapchain is *_UNORM
                        // and the display expects sRGB-encoded values.
    Rec709Gamma22 = 2,  // Apply pow(v, 1/2.2). Correct for displays calibrated
                        // to Rec.709 with the gamma 2.2 approximation.
};

// Push-constant payload describing the current display transform. Layout
// matches DisplayPass.frag's PushConstants block.
struct DisplayParams {
    uint32_t transform;  // cast from DisplayTransform
    float exposure;      // multiplier applied before tone-mapping. v1: 1.0
    float _pad0;
    float _pad1;
};

// CPU-side conversions (1D, primarily for parameter UI and tests). GPU-side
// equivalents live in DisplayPass.frag.

// Encode a single linear scene-referred channel value for display.
float linearToDisplay(float v, DisplayTransform t) noexcept;

// Inverse of linearToDisplay. Decode a display-encoded value to linear.
float displayToLinear(float v, DisplayTransform t) noexcept;

// Pick the correct display transform for a swapchain image format. Returns
// DisplayTransform::None when the swapchain format includes hardware sRGB
// encoding (any *_SRGB format), DisplayTransform::sRGB otherwise.
//
// The argument is a VkFormat value passed as a raw integer to keep this header
// free of Vulkan dependencies — color/ is part of headless core/. The caller
// (typically main.cpp or Swapchain) converts via static_cast<uint32_t>.
DisplayTransform pickTransformForSwapchainFormat(uint32_t vkFormatValue) noexcept;

}  // namespace loom::color
