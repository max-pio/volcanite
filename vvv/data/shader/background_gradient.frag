#version 450



layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout (std140, set = 0, binding = 0)
uniform gradient {
    vec4 colorTopLeft;
    vec4 colorBottomRight;
};

void main() {
    float l = length(uv)/length(vec2(1.f));
    outColor = l * colorTopLeft + (1.f - l) * colorBottomRight;
}