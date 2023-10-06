#version 460
#extension GL_GOOGLE_include_directive : enable

layout (local_size_x = 8, local_size_y = 8) in;

layout (set = 0, binding = 0, rgba8) uniform restrict writeonly image2D IMAGE_out;
layout (set = 0, binding = 1) uniform options {
    vec3      iResolution;           // viewport resolution (in pixels)
    float     iTime;                 // shader playback time (in seconds)
    float     iTimeDelta;            // render time (in seconds)
    int       iFrame;                // shader playback frame
    vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
};

void mainImage(out vec4 fragColor, in vec2 fragCoord);

void main() {
    vec4 fragColor;
    mainImage(fragColor, vec2(gl_GlobalInvocationID.x, iResolution.y - gl_GlobalInvocationID.y - 1));
    imageStore(IMAGE_out, ivec2(gl_GlobalInvocationID.xy), fragColor);
}

