#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, rgba32f) uniform readonly image2D bindlessImages[];

// Layout matches the C++ PushConstants struct in DisplayPass.hpp. Extending
// this requires updating both in lockstep. The viewer's HDR source image may
// have a different resolution and aspect ratio than the destination viewport
// (e.g. a 1024² deep EXR composited onto a 1280×720 panel) — `width / height`
// are the destination viewport extent; `srcWidth / srcHeight` are the source
// image's native extent. The fragment shader aspect-fits the source into the
// viewport with a black letterbox.
layout(push_constant) uniform PushConstants {
    uint  inputSlotIndex;
    uint  width;             // destination viewport width
    uint  height;            // destination viewport height
    uint  srcWidth;          // source HDR image width
    uint  srcHeight;         // source HDR image height
    uint  toneMapMode;       // 0 = Linear, 1 = Reinhard, 2 = ACES (Narkowicz)
    uint  displayTransform;  // 0 = None, 1 = sRGB OETF, 2 = Rec.709 Gamma 2.2
    float exposure;
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
    // Aspect-fit math. Compute the half-padding (in 0..1 viewport coords)
    // along the axis where the source is "smaller". The other axis fits the
    // viewport edge-to-edge.
    float srcAspect = float(pc.srcWidth) / float(pc.srcHeight);
    float vpAspect  = float(pc.width)    / float(pc.height);
    vec2 letterbox = vec2(0.0);
    if (srcAspect > vpAspect) {
        // Source is wider than viewport → letterbox top + bottom.
        float h = vpAspect / srcAspect;
        letterbox.y = (1.0 - h) * 0.5;
    } else {
        // Source is taller / square → letterbox left + right.
        float w = srcAspect / vpAspect;
        letterbox.x = (1.0 - w) * 0.5;
    }

    // Remap viewport UV → source UV by stripping the letterbox margins.
    vec2 srcUV = (inUV - letterbox) / (vec2(1.0) - 2.0 * letterbox);
    if (any(lessThan(srcUV, vec2(0.0))) || any(greaterThanEqual(srcUV, vec2(1.0)))) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Sample the source at its native resolution. The clamp guards against
    // float rounding pushing the texel one past the last valid index.
    ivec2 texel = ivec2(srcUV * vec2(pc.srcWidth, pc.srcHeight));
    texel = clamp(texel, ivec2(0), ivec2(int(pc.srcWidth) - 1, int(pc.srcHeight) - 1));
    vec4 hdr = imageLoad(bindlessImages[pc.inputSlotIndex], texel);

    vec3 color = hdr.rgb * pc.exposure;

    if (pc.toneMapMode == 1u)      color = reinhardTonemap(color);
    else if (pc.toneMapMode == 2u) color = acesTonemap(color);
    // mode 0 = linear passthrough, no curve applied.

    color = clamp(color, 0.0, 1.0);
    color = applyDisplayTransform(color);

    outColor = vec4(color, 1.0);
}
