// PointCloud — per-sample vertex shader.
//
// Draws `totalSamples` points, one per deep sample. Each vertex pulls:
//   - its sample-index → pixel-index via `sampleToPixel[gl_VertexIndex]`
//   - its sample bytes (Z + RGBA half) from `samples[gl_VertexIndex * 4]`
//
// World position synthesis: v1 has no `world_pos` channel (NVS extension
// lands in Phase D.1). We derive a synthetic position by mapping pixel
// (px, py) → centered-unit-square XY and using Z as depth. The camera
// orbits around this height-field. When NVS `world_pos.{x,y,z}` is
// available, the Phase D shader reads it directly.
//
// Output color: the per-sample premultiplied RGBA radiance, untouched.

#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 1) readonly buffer BindlessUintBuffer {
    uint data[];
} bindlessBuffers[];

layout(set = 1, binding = 0) uniform CameraUbo {
    mat4 viewProj;
} camera;

layout(push_constant) uniform PushConstants {
    uint samplesSlot;
    uint sampleToPixelSlot;
    uint width;
    uint height;
    float zScale;
    float pointSize;
    uint strideU;       // sample stride in uints (4 = half RGBA, 6 = float RGBA)
    uint rgbaIsFloat;   // 0 = packed half pairs, 1 = four Float32 scalars
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    uint sIdx = uint(gl_VertexIndex);

    uint pixelIdx = bindlessBuffers[pc.sampleToPixelSlot].data[sIdx];
    uint px = pixelIdx % pc.width;
    uint py = pixelIdx / pc.width;

    uint baseElement = sIdx * pc.strideU;
    // Alphabetical reader order: A B G R then Z ZBack. Z sits at uint
    // offset 2 for the half-RGBA layout and at offset 4 for float-RGBA.
    uint zUintIndex = (pc.rgbaIsFloat == 0u) ? 2u : 4u;
    float z = uintBitsToFloat(bindlessBuffers[pc.samplesSlot].data[baseElement + zUintIndex]);

    vec4 rgba;
    if (pc.rgbaIsFloat == 0u) {
        uint abPacked = bindlessBuffers[pc.samplesSlot].data[baseElement + 0u];
        uint grPacked = bindlessBuffers[pc.samplesSlot].data[baseElement + 1u];
        vec2 ab = unpackHalf2x16(abPacked);
        vec2 gr = unpackHalf2x16(grPacked);
        rgba = vec4(gr.y, gr.x, ab.y, ab.x);  // R, G, B, A
    } else {
        float A = uintBitsToFloat(bindlessBuffers[pc.samplesSlot].data[baseElement + 0u]);
        float B = uintBitsToFloat(bindlessBuffers[pc.samplesSlot].data[baseElement + 1u]);
        float G = uintBitsToFloat(bindlessBuffers[pc.samplesSlot].data[baseElement + 2u]);
        float R = uintBitsToFloat(bindlessBuffers[pc.samplesSlot].data[baseElement + 3u]);
        rgba = vec4(R, G, B, A);
    }

    // Map (px, py) into centered [-1, 1] in XY. +Y flipped to match the
    // RH/Y-up world convention (§19) — pixel row 0 is the top of the image,
    // which corresponds to +Y world.
    float fx = ((float(px) + 0.5) / float(pc.width)) * 2.0 - 1.0;
    float fy = 1.0 - ((float(py) + 0.5) / float(pc.height)) * 2.0;

    vec3 worldPos = vec3(fx, fy, -z * pc.zScale);

    gl_Position = camera.viewProj * vec4(worldPos, 1.0);
    gl_PointSize = pc.pointSize;
    vColor = rgba;
}
