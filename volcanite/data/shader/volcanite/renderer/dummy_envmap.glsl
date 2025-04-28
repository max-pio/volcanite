//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
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

#ifndef VOLCANITE_DUMMY_ENVMAP_GLSL
#define VOLCANITE_DUMMY_ENVMAP_GLSL

#include "random.glsl"

vec3 dummy_envmap(vec3 dir) {
    float cloud_density = clamp(dir.y * 3.f + 0.6f, 0.f, 1.f);

    // compute angles for noise lookup, normalized to [0, 1)
    float axz = atan(dir.z, dir.x) / (2.f * 3.1415f) + 0.5f;
    float ay = acos(dir.y) / (3.1415f);

    vec3 light_color = vec3(2.f, 1.2f, 0.f);
    vec3 base_color = vec3(0.4f, 0.6f, 1.f);

    vec3 c = vec3(0.f);
    c += mix(vec3(perlinNoise(abs(vec2(axz, ay)), 8)), vec3(1.f), dir.y * dir.y * dir.y);
    c = mix(c, base_color, ay);
    c = mix(c, light_color, max(dir.x * dir.x * dir.x * max(dir.y + 0.2f, 0.f), 0.f));
    c += vec3(0.3f);

    // TODO: use a proper HDR environment map and tone mapping
    return c * 1.4f
}

/// The background is a tilted interpolation between two colors g_background_color_a and g_background_color_b
/// or displaying the environment map.
vec4 get_background_color(vec2 fragCoord, vec3 rayDirection) {
    float bgWeight = (fragCoord.x*2.f + (1.f-fragCoord.y)) / 3.f;
    bgWeight *= bgWeight;
    vec4 bgColor = g_background_color_a * (1.f - bgWeight) + g_background_color_b * bgWeight;
    bgColor.rgb *= bgColor.a;
    return bgColor;
}

#endif // VOLCANITE_DUMMY_ENVMAP_GLSL
