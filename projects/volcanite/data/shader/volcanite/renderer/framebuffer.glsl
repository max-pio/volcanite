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

#ifndef VOLCANITE_FRAMEBUFFER_GLSL
#define VOLCANITE_FRAMEBUFFER_GLSL

#include "random.glsl"
#include "morton.glsl"


// Work Item to Pixel Mapping / Subsampling ----------------------------------------------------------------------------

ivec2 pixelBlueNoiseOffset() {
    int blueNoiseIdx = g_blue_noise_enable ? int(blueNoise32x32(ivec2(gl_GlobalInvocationID.xy)) * float(g_subsampling * g_subsampling)) : 0;
    return ivec2(morton2Di2p(blueNoiseIdx));
}

ivec2 pixelFromInvocationID() {
    // We send out one ray per block of [g_subsampling]*[g_subsampling] pixels
    if (g_subsampling <= 1) {
        return ivec2(gl_GlobalInvocationID.xy);
    } else {
        // offset the subsampling pixel with some blue noise
        // g_subsampling_pixel is actually just morton_idx2pos(bitfieldReverse(idx % g_subsampling * g_subsampling))
        return ivec2(gl_GlobalInvocationID.xy * g_subsampling)
             + ivec2(mod(g_subsampling_pixel + pixelBlueNoiseOffset(), ivec2(g_subsampling)));

//        return ivec2(gl_GlobalInvocationID.xy * g_subsampling) + g_subsampling_pixel;
    }
}

// returns the number of samples that this subpixel in the [g_subsampling]^2 block received *after*
// g_camera_still_frames were rendered.
//uint pixelSampleCount(ivec2 subpixel) {
//    // NOTE: THIS IS NOT CORRECT + THE ACTUAL NUMBER OF ACCUMULATED SAMPLES WILL VARY BECAUSE OF INVALID SAMPLES
//    uint pixel_block_size = (g_subsampling * g_subsampling);
//
//    uint guaranteed_samples = g_camera_still_frames / pixel_block_size;
//    // r samples were already rendered within this pixel block
//    uint r = g_camera_still_frames - guaranteed_samples * pixel_block_size;
//    // if the render-index of the sub-block position of pixel is smaller or equal to the current render-index,
//    // this pixel already received a sample
//    uint possible_sample = uint(morton2Dp2i(subpixel % g_subsampling) <= (bitfieldReverse(r) >> (33 - findMSB(pixel_block_size))));
//    return guaranteed_samples + possible_sample;
//}

// G-Buffer and Accumulation Buffer ------------------------------------------------------------------------------------

/// return a value that can be stored in an RG8 format to indicate an invalid G-Buffer entry
uvec2 invalidGBufferRG8() {
    // first two bits 11 is invalid. set everything to 1.
    // we could use other bits to store if this is a background / no hit sample
    return uvec2(0xFF, 0xFF);
}

/// pack the given attributes in a value that can be stored in an RG8 format
uvec2 packGBufferRG8(uint label, vec3 normal, float normalized_depth) {

    // a note from the AMD developer performance guide: (https://gpuopen.com/learn/rdna-performance-guide/)
    // "put highly correlated bits in the Most Significant Bits (MSBs) and noisy data in the Least Significant Bits"
    uvec2 packed = uvec2(0u);

    // 3 bits for -/+ {(1,0,0) | (0,1,0) | (0,0,1)} axis-aligned normal
    // first two bits denote the axis: 00=x 01=y 10=z or an invalid G-Buffer sample with 11
    packed.x |= clamp(uint(dot(abs(normal), vec3(0.f, 1.f, 2.f))), 0u, 2u);
    // third bit is the sign: 0 positive, 1 negative
    packed.x |= dot(normal, vec3(1.f, 1.f, 1.f)) < 0.f ? 4u : 0u;

    // 5 bits for label hash
    packed.x |= ((label ^ (label >> 16)) % 32) << 3;

    // 8 bits for depth
    packed.y = uint(clamp(normalized_depth, 0.f, 1.f) * 255.f);

    return packed;
}

/// unpack the given RG8 G-Buffer value into attributes. Returns true if the G-Buffer sample was valid.
bool unpackGBufferRG8(uvec2 g_buffer_packed, out uint label, out vec3 normal, out float normalized_depth) {
    // first two bits 11 is an invalid buffer entry
    if ((g_buffer_packed.x & 0x3u) == 3u)
    return false;

    // 3 bits for normal
    normal = vec3(0.f);
    normal[(g_buffer_packed.x & 0x3u)] = 1.f - 2.f * float((g_buffer_packed.x >> 2) & 0x1u);

    // 5 bits for label hash. This is not bijective, so we just return the hash
    label = (g_buffer_packed.x >> 3u) & 0x1Fu;

    // 8 bits for normalized depth
    normalized_depth = float(g_buffer_packed.y) / 255.f;

    return true;
}

bool isDepthValid(float depth) { return depth >= 0.f; }

void writePixel(ivec2 pixel, vec4 color, float depth_valid, uvec2 g_buffer_packed) {

    // invalidate any nan samples
    if (any(isnan(color)) || any(isinf(color)) || isnan(depth_valid) || isinf(depth_valid)) {
        color = vec4(1.f, 0.f, 1.f, 1.f);
        depth_valid = -abs(depth_valid);
    }

    // if subsamplign is enabled, we only render one pixel per [g_subsampling]^2 block
    ivec2 subpixel;
    for (subpixel.y = 0; subpixel.y < g_subsampling; subpixel.y++) {
        for (subpixel.x = 0; subpixel.x < g_subsampling; subpixel.x++) {
            ivec2 opix = (pixel/g_subsampling)*g_subsampling + subpixel;
            if (g_camera_still_frames == 0u) {
                // writing other pixel: initialize with 0 and invalid G-Buffer
                if (any(notEqual(opix, pixel))) {
                    imageStore(feedbackOut, opix, vec4(0.f));
                    imageStore(gBuffer, opix, uvec4(invalidGBufferRG8(), 0u, 0u));
                }
                // writing our pixel: invalid samples (depth < 0) will be overwritten in another frame (set .a < 0)
                else {
                    imageStore(feedbackOut, opix, vec4(color.rgb, isDepthValid(depth_valid) ? 1.f :  -1.f));
                    imageStore(gBuffer, opix, uvec4(g_buffer_packed, 0u, 0u));
                }
            }
            else {
                vec4 prev_color = imageLoad(feedbackIn, opix);
                // writing other pixel: just copy from previous to current frame
                if (any(notEqual(opix, pixel))) {
                    imageStore(feedbackOut, opix, prev_color);
                    // gBuffer remains unchanged
                }
                // writing our pixel, but invalid new sample (from not yet decoded brick)
                else if (!isDepthValid(depth_valid)) {
                    imageStore(feedbackOut, opix, prev_color.a > 0.f ? prev_color : vec4(color.rgb, -1.f));
                    imageStore(gBuffer, opix, uvec4(g_buffer_packed, 0u, 0u));
                }
                // writing our pixel with valid new sample: use previous pixel only if it already had valid samples
                else {
                    imageStore(feedbackOut, opix, vec4(color.rgb, 1.f) + (prev_color.a > 0.f ? prev_color : vec4(0.f)));
                    imageStore(gBuffer, opix, uvec4(g_buffer_packed, 0u, 0u));
                }
            }
        }
    }
}

#endif // VOLCANITE_FRAMEBUFFER_GLSL
