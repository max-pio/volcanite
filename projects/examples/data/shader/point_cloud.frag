#version 450
#extension GL_GOOGLE_include_directive : enable

const vec3 g_phong_ambientLightColor = vec3(0.2, 0.2, 0.2);
const vec3 g_phong_diffuseLightColor = vec3(0.5, 0.5, 0.5);
const vec3 g_phong_specularLightColor = vec3(0.5, 0.5, 0.5);
const vec3 g_phong_lightDirection = normalize(vec3(0, 1, 1));
const float g_phong_specularExponent = 2.0;

#include "shading.glsl"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec3 in_campos;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;

const float color_amount = 1.0f;
const float shading_amount = 1.0f;

void main() {
    vec3 albedo = vec3(0.5f) + 0.5f * in_position;
    albedo = mix(vec3(1), albedo, color_amount);
    vec3 normal = normalize(in_normal);

    vec3 color = phongShading(albedo, normal, normalize(in_campos - in_position));
    color = mix(vec3(1), color, shading_amount);

    outColor = vec4(color, 1);
    outNormal = vec4(vec3(0.5f) + 0.5f * normal, 1.0f);
}