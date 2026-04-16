#version 460

layout(location = 0) out vec2 outUV;

void main() {
    // Standard fullscreen triangle: covers NDC [-1,1] with one extra triangle
    // that clips outside. UVs range [0,2] but only [0,1] maps to the screen.
    vec2 positions[3] = vec2[](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
    vec2 uvs[3] = vec2[](vec2(0, 0), vec2(2, 0), vec2(0, 2));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outUV = uvs[gl_VertexIndex];
}
