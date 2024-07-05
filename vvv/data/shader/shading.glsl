//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

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