// PointCloud — fragment shader. Trivial: forwards the per-vertex colour to
// the attachment. The vertex shader supplies premultiplied RGBA; we output
// it as-is so the v1 `Opaque` blend mode behaves correctly (samples are
// already alpha-pre-multiplied per docs/CONVENTIONS.md §19).

#version 460

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}
