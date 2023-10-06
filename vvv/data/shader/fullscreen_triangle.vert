#version 450

vec2 positions[3] = vec2[](
vec2(-1.f, -1.f),
vec2(-1.f,  3.f),
vec2( 3.f, -1.f)
);

vec2 uvs[3] = vec2[](
vec2(0.f, 0.f),
vec2(0.f, 2.f),
vec2(2.f, 0.f)
);

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    uv = uvs[gl_VertexIndex];
}