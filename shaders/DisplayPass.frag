#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, rgba32f) uniform readonly image2D bindlessImages[];

// Layout matches loom::color::DisplayParams and the C++ PushConstants struct
// in DisplayPass.hpp. Extending this requires updating all three in lockstep.
layout(push_constant) uniform PushConstants {
    uint  inputSlotIndex;
    uint  width;
    uint  height;
    uint  toneMapMode;        // 0 = Linear, 1 = Reinhard, 2 = ACES (Narkowicz)
    uint  displayTransform;   // 0 = None, 1 = sRGB OETF, 2 = Rec.709 Gamma 2.2
    float exposure;
    float _pad0;
    float _pad1;
}
pc;

vec3 reinhardTonemap(vec3 hdr) { return hdr / (hdr + vec3(1.0)); }

vec3 acesTonemap(vec3 x) {
    // Narkowicz 2015 ACES approximation.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// IEC 61966-2-1 sRGB OETF (linear -> sRGB-encoded).
float srgbOETF(float linear) {
    return mix(12.92 * linear,
               1.055 * pow(linear, 1.0 / 2.4) - 0.055,
               step(0.0031308, linear));
}

vec3 applyDisplayTransform(vec3 c) {
    if (pc.displayTransform == 1u) {
        return vec3(srgbOETF(c.r), srgbOETF(c.g), srgbOETF(c.b));
    }
    if (pc.displayTransform == 2u) {
        return pow(c, vec3(1.0 / 2.2));
    }
    // displayTransform == 0 (None): identity — correct when the swapchain is
    // *_SRGB and the hardware applies the OETF on write.
    return c;
}

void main() {
    ivec2 texel = ivec2(clamp(inUV * vec2(pc.width, pc.height),
                              vec2(0.0),
                              vec2(pc.width - 1, pc.height - 1)));
    vec4 hdr = imageLoad(bindlessImages[pc.inputSlotIndex], texel);

    vec3 color = hdr.rgb * pc.exposure;

    if (pc.toneMapMode == 1u)      color = reinhardTonemap(color);
    else if (pc.toneMapMode == 2u) color = acesTonemap(color);
    // mode 0 = linear passthrough, no curve applied.

    color = clamp(color, 0.0, 1.0);
    color = applyDisplayTransform(color);

    outColor = vec4(color, 1.0);
}
