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

#ifndef UTIL_H
#define UTIL_H

#ifdef NDEBUG
    #define assert(X, S)
    #define assertf(X, S, P)
#else
    #define assert(X, S) if(!(X)) debugPrintfEXT(S)
    #define assertf(X, S, P) if(!(X)) debugPrintfEXT(S, P)
#endif

#ifndef PI
    #define PI 3.14159265359f
#endif
#ifndef TWO_PI
    #define TWO_PI 6.28318530718f
#endif
#ifndef ONE_OVER_PI
    #define ONE_OVER_PI 0.3183098861837907f
#endif
#ifndef ONE_OVER_TWO_PI
    #define ONE_OVER_TWO_PI 0.1591549430918953f
#endif

/// Check whether a dispatched thread is out of bounds.
///
/// This happens when the screen size is not a integer multiple of the workgroup size in a compute dispatch, or if the
/// screen size is uneven in a fragment dispatch causing half of the pixel quad to be out of bounds.
bool isHelperLane(ivec2 invocationIdx, ivec2 targetSize) {
    return invocationIdx.x >= targetSize.x || invocationIdx.y >= targetSize.y;
}

bool isHelperLane(ivec3 invocationIdx, ivec3 targetSize) {
    return invocationIdx.x >= targetSize.x || invocationIdx.y >= targetSize.y || invocationIdx.z >= targetSize.z;
}

bool isHelperLane(uvec2 invocationIdx, uvec2 targetSize) {
    return invocationIdx.x >= targetSize.x || invocationIdx.y >= targetSize.y;
}

bool isHelperLane(uvec3 invocationIdx, uvec3 targetSize) {
    return invocationIdx.x >= targetSize.x || invocationIdx.y >= targetSize.y || invocationIdx.z >= targetSize.z;
}

bool isHelperLane(int invocationIdx, int targetSize) {
    return invocationIdx.x >= targetSize.x;
}

bool isHelperLane(uint invocationIdx, uint targetSize) {
    return invocationIdx.x >= targetSize.x;
}

// color converters released by Sam Hocevar into the public domain (CC0)
// see: https://www.shadertoy.com/view/tstcDX
vec3 hsl2rgb(in vec3 c) {
    vec3 rgb = clamp( abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );
    return c.z + c.y * (rgb-0.5)*(1.0-abs(2.0*c.z-1.0));
}
vec3 rgb2hsv(vec3 c) {
    const vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    const float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c)  {
    const vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}


float luminance(vec3 v) {
    return 0.2126 * v.x + 0.7152 * v.y + 0.0722 * v.z;
}

vec3 rgb2srgb(vec3 x, float gamma) {
    // simple mapping:
    // return pow(x, 1.f / vec3(2.4f));
    #pragma unroll
    for (int i = 0; i < 3; i++) {
        if (x[i] < 0.0031308f)
        x[i] *= 12.92f;
        else
        x[i] = 1.055f * pow(x[i], 1.0f/gamma) - 0.055f;
    }
    return x;
}

vec3 tonemap_ACES(vec3 x)  {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), vec3(0.f), vec3(1.f));
}

vec3 integer2colorlabel(uint id, bool linear) {
    if(!linear) {
        uvec4 hash_base = (uvec4(id, id << 8, id << 16, id << 24) * uvec4(137u, 97u, 31u, 7u)) >> 8u;
        id = hash_base.x ^ hash_base.y ^ hash_base.z ^ hash_base.w + 117u;
    }
    return hsv2rgb(vec3(id%256, 128 + ((id/256)%256) /4, 128 + ((id/65536)%256) / 2) / 255.f);
}

bool isCenterWorkItem() {
    return all(equal(gl_GlobalInvocationID, (gl_NumWorkGroups * gl_WorkGroupSize)/2));
}

bool isFirstWorkItem() {
    return all(equal(gl_GlobalInvocationID, uvec3(0u)));
}

int argmin(vec3 v) {
    if (v.y < v.x) {
        if (v.y < v.z)
            return 1;
    } else {
        if (v.z < v.x)
            return 2;
    }
    return 0;
}

int argmax(vec3 v) {
    if (v.y > v.x) {
        if (v.y > v.z)
            return 1;
    } else {
        if (v.z > v.x)
            return 2;
    }
    return 0;
}


float map(float v, float v_min, float v_max, float new_min, float new_max) {
    return (v - v_min) / (v_max - v_min) * (new_max - new_min) + new_min;
}

#endif // UTIL_H
