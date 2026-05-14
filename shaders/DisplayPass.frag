#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, rgba32f) uniform readonly image2D bindlessImages[];

layout(push_constant) uniform PushConstants {
    uint inputSlotIndex;
    uint width;
    uint height;
    uint toneMapMode;  // 0 = Linear, 1 = Reinhard, 2 = ACES (Narkowicz)
}
pc;

vec3 reinhardTonemap(vec3 hdr) { return hdr / (hdr + vec3(1.0)); }

vec3 acesTonemap(vec3 x) {
    // Narkowicz 2015 ACES approximation.
    // Other ACES approximations (Hill, full RRT/ODT) produce different results.
    // This version is standard in real-time graphics.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Map UV to integer texel. Clamp to [0, dim-1] to prevent out-of-bounds
    // access at UV = 1.0, where uv * dim == dim (out of range).
    ivec2 texel = ivec2(clamp(inUV * vec2(pc.width, pc.height), vec2(0.0), vec2(pc.width - 1, pc.height - 1)));
    vec4 hdr = imageLoad(bindlessImages[pc.inputSlotIndex], texel);

    vec3 color = hdr.rgb;
    if (pc.toneMapMode == 1)
        color = reinhardTonemap(color);
    else if (pc.toneMapMode == 2)
        color = acesTonemap(color);
    // mode 0 = linear passthrough, no curve applied

    // Gamma correction: apply ONLY if the destination swapchain/render target
    // is a UNORM format (e.g., VK_FORMAT_B8G8R8A8_UNORM).
    // If the format is a _SRGB variant (e.g., VK_FORMAT_B8G8R8A8_SRGB),
    // hardware applies gamma automatically on write — do NOT apply pow() here
    // or the output will be double-corrected and appear washed out.
    color = pow(color, vec3(1.0 / 2.2));  // Remove if destination is _SRGB

    outColor = vec4(color, 1.0);
}
