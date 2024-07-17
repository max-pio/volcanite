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

const float PI = 3.1415926535897932384626433832795;
const float PI_2 = 1.57079632679489661923;
const float PI_4 = 0.785398163397448309616;

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

vec3 hsl2rgb(in vec3 c) {
    vec3 rgb = clamp( abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );
    return c.z + c.y * (rgb-0.5)*(1.0-abs(2.0*c.z-1.0));
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 integer2colorlabel(uint id, bool linear) {
    if(linear)
    return vec3(id%256, (id/256)%256, (id/65536)%256)/255.f;
    id *= 17;
    return hsv2rgb(vec3(float(id % 256) / 255.f, float((id/256)%128)/255.f + 0.5f, 0.375f + float((id/32768)%64)/255.f));
}

bool isCenterWorkItem() {
    return all(equal(gl_GlobalInvocationID, (gl_NumWorkGroups * gl_WorkGroupSize)/2));
}

bool isFirstWorkItem() {
    return all(equal(gl_GlobalInvocationID, uvec3(0u)));
}

#endif // UTIL_H
