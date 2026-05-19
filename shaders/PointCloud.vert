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
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    uint sIdx = uint(gl_VertexIndex);

    uint pixelIdx = bindlessBuffers[pc.sampleToPixelSlot].data[sIdx];
    uint px = pixelIdx % pc.width;
    uint py = pixelIdx / pc.width;

    uint baseElement = sIdx * 4u;  // stride = 16 bytes = 4 uints
    uint zBits     = bindlessBuffers[pc.samplesSlot].data[baseElement + 0u];
    uint rgPacked  = bindlessBuffers[pc.samplesSlot].data[baseElement + 2u];
    uint baPacked  = bindlessBuffers[pc.samplesSlot].data[baseElement + 3u];

    float z   = uintBitsToFloat(zBits);
    vec2  rg  = unpackHalf2x16(rgPacked);
    vec2  ba  = unpackHalf2x16(baPacked);

    // Map (px, py) into centered [-1, 1] in XY. +Y flipped to match the
    // RH/Y-up world convention (§19) — pixel row 0 is the top of the image,
    // which corresponds to +Y world.
    float fx = ((float(px) + 0.5) / float(pc.width)) * 2.0 - 1.0;
    float fy = 1.0 - ((float(py) + 0.5) / float(pc.height)) * 2.0;

    vec3 worldPos = vec3(fx, fy, -z * pc.zScale);

    gl_Position = camera.viewProj * vec4(worldPos, 1.0);
    gl_PointSize = pc.pointSize;
    vColor = vec4(rg.x, rg.y, ba.x, ba.y);
}
