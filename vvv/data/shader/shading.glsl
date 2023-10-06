#ifndef SHADING_H
#define SHADING_H

#include "util.glsl"

vec3 phongShading(vec3 colorIn, vec3 normal, vec3 rayDir) {
    vec3 color = vec3(0);
    color += g_phong_ambientLightColor.rgb * colorIn;//ambient light
    color += g_phong_diffuseLightColor.rgb * colorIn * abs(dot(normal, g_phong_lightDirection.rgb));//diffuse
    vec3 reflect = 2 * dot(g_phong_lightDirection.rgb, normal) * normal - g_phong_lightDirection.rgb;
    float exponent = round(g_phong_specularExponent);
    color += g_phong_specularLightColor.rgb * (((exponent + 2) / (2 * PI)) * pow(clamp(dot(reflect, rayDir), 0.0f, 1.0f), exponent));
    return color;
}

#endif /* SHADING_H */