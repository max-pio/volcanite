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

vec2 subpixelOffset(const ivec2 pixel) {
    // TODO: code duplication in initialization of the random state
    const uint prev_sample_count = imageLoad(accuSampleCountIn, pixel).r;
    const vec2 u = randomVec3(pixel,((g_camera_still_frames / 256u) << 8u) ^ (prev_sample_count << 31u) ^ prev_sample_count).xy;

    // blackman harris filter with a width of 1.5 pixels
    return vec2(0.5f) + (g_blue_noise_enable ? sampleBlackmanHarris(u) : vec2(0.f));
}

ivec2 pixelOffsetInBlock() {
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
             + ivec2(mod(vec2(g_subsampling_pixel + pixelOffsetInBlock()), vec2(g_subsampling)));
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

// Two ping-pong accumulation buffers:
//   - [RGBA16f] RGBA radiance and opacity accumulated
//   - [R16u]    accumulated (valid) sample count
// One G-Buffer [RGB16u]:
//   - x component is 0xFF for pixels that did not receive any sample yet
//   - else: first two bits of x component are 11 (== 3u) if no surface was hit. normal is (0,0,0)
//   - else: first 3 bits contain normal, other bits contain depth, and label

/// return a value that can be stored in an RGB16 format to indicate an unset G-Buffer entry, for pixels that did not
/// receive any sample yet. these pixels should be omitted in the resolve pass.
uvec3 invalidGBufferRGB16() {
    // first two normal axis bits set to 1 and normal sign bit set to 1 = (************111) => not sampled
    // additionally: maximum depth, INVALID label
    return uvec3(0xFFFF, 0xFFFF, 0xFFFF);
}

// return a value that can be stored in an RGB16 format to indicate a G-Buffer for a pixel that received a sample during
// rendering but did not hit any visibile surface.
uvec3 noHitGBufferRGB16() {
    // first two normal axis bits set to 1 and normal sign bit set to 0 = (************011) => sampled, no surface hit
    // additionally: maximum depth, INVALID label
    return uvec3(65531u, 0xFFFF, 0xFFFF);
}

// returns true if this packed G-buffer entry is marking the pixel as not sampled
bool isInvalidGBufferRGB16(uvec3 g_buffer_packed) {
    return g_buffer_packed.x == 0xFFFF;
}

// returns true if this packed G-buffer entry is for a pixel that is rendered and hit a visible surface.
bool isSurfaceHitGBufferRGB16(uvec3 g_buffer_packed) {
    return g_buffer_packed.x != 65531u;
}

#ifndef VOLCANITE_FRAMEBUFFER_READONLY
/// pack the given attributes in a value that can be stored in an RG8 format
uvec3 packGBufferRGB16(uint label, vec3 normal, float normalized_depth) {

    // a note from the AMD developer performance guide: (https://gpuopen.com/learn/rdna-performance-guide/)
    // "put highly correlated bits in the Most Significant Bits (MSBs) and noisy data in the Least Significant Bits"
    uvec3 packed = uvec3(0u);

    // 3 bits for the axis-aligned normal, i.e. -/+ {(1,0,0) | (0,1,0) | (0,0,1)}
    // first two bits denote the axis: 00=x 01=y 10=z or a "not sampled" or "no-hit" G-Buffer sample with 11
    packed.x = clamp(uint(dot(abs(normal), vec3(0.f, 1.f, 2.f))), 0u, 2u);
    // third bit for the sign: 0 positive, 1 negative
    packed.x |= dot(normal, vec3(1.f, 1.f, 1.f)) < 0.f ? 4u : 0u;

    // 13 bits for depth
    packed.x |= uint(clamp(normalized_depth, 0.f, 0.9998f) * 8192.f) << 3;

    // 32 bits for the label at the first surface hit (implicitly encodes the albedo color as well)
    packed.y = label & 0xFFFFu;
    packed.z = label >> 16u;

    // TODO: could return packed RGB16 g-buffer as u16vec3 to save registers
    return packed;
}
#endif


/// unpack the given RGB16 G-Buffer value into attributes. Returns false if the G-Buffer did not receive a sample yet.
bool unpackGBufferRGB16(uvec3 g_buffer_packed, inout uint label, inout vec3 normal, inout float normalized_depth) {
    if (isInvalidGBufferRGB16(g_buffer_packed)) {
        return false;
    }

    // 3 bits for normal, if the first two bits are both set, it is undefined (no-hit ray)
    normal = vec3(0.f);
    if (isSurfaceHitGBufferRGB16(g_buffer_packed))
        normal[(g_buffer_packed.x & 0x3u)] = 1.f - 2.f * float((g_buffer_packed.x >> 2) & 0x1u);

    // 13 bits normalized depth between 0 and 1
    normalized_depth = float(g_buffer_packed.x >> 3) / 8192.f;

    // 32 bits for the label (split into y and z component)
    label = g_buffer_packed.y | (g_buffer_packed.z << 16u);

    return true;
}

// accumulation buffer:

bool isDepthValid(float depth) { return depth >= 0.f; }

#ifndef VOLCANITE_FRAMEBUFFER_READONLY
void writePixel(ivec2 pixel, vec4 new_rgba, float depth_valid, uvec3 g_buffer_packed) {

    // invalidate any nan samples
    if (any(isnan(new_rgba)) || any(isinf(new_rgba)) || isnan(depth_valid) || isinf(depth_valid)) {
        new_rgba = vec4(1.f, 0.f, 1.f, 1.f);
        depth_valid = -abs(depth_valid);
    }

    // if subsamplign is enabled, we only render one pixel per [g_subsampling]^2 block
    ivec2 subpixel;
    const ivec2 pixel_block_start = (pixel/g_subsampling)*g_subsampling;
    for (subpixel.y = 0; subpixel.y < g_subsampling; subpixel.y++) {
        for (subpixel.x = 0; subpixel.x < g_subsampling; subpixel.x++) {
            ivec2 opix = pixel_block_start + subpixel;

            vec4 accumulated_rgba_out;
            uint sample_count_out = 0u;

            // first frame: reset
            if (g_camera_still_frames == 0u) {
                // writing other pixel: initialize RGBA accumulation with 0 and invalid G-Buffer, set 0 SPP
                if (any(notEqual(opix, pixel))) {
                    accumulated_rgba_out = vec4(0.f);
                    sample_count_out = 0u;
                    imageStore(gBuffer, opix, uvec4(invalidGBufferRGB16(), 0u));
                }
                // writing our pixel: invalid samples (depth < 0) will be overwritten in another frame
                else {
                    accumulated_rgba_out = new_rgba;
                    sample_count_out = isDepthValid(depth_valid) ? 1u : 0u;
                    imageStore(gBuffer, opix, uvec4(g_buffer_packed, 0u));
                }
            }
            // later frames: accumulation
            else {
                const vec4 prev_rgba = imageLoad(accumulationIn, opix);
                const uint prev_sample_count = imageLoad(accuSampleCountIn, opix).r;

                // writing other pixel: just copy from previous to current frame
                if (any(notEqual(opix, pixel))) {
                    accumulated_rgba_out = prev_rgba;
                    sample_count_out = prev_sample_count;
                    // G-buffer remains unchanged
                }
                // writing our pixel, but invalid new sample (a ray hit a brick that was not yet decoded)
                else if (!isDepthValid(depth_valid)) {
                    if (prev_sample_count > 0u) {
                        // the accumulation buffer already contains a valid accumulation: skip this invalid sample
                        accumulated_rgba_out = prev_rgba;
                        // G-buffer remains unchanged
                    } else {
                        // the accumulation buffer is not set
                        accumulated_rgba_out = new_rgba;
                        imageStore(gBuffer, opix, uvec4(g_buffer_packed, 0u));
                    }
                    sample_count_out = prev_sample_count;
                }
                // writing our pixel with valid new sample: use previous pixel only if it already had valid samples
                else {
                    // iterative re-weighting
                    const float new_weight = 1.f / float(prev_sample_count + 1);
                    accumulated_rgba_out = new_rgba * new_weight + prev_rgba * (1.f - new_weight);
                    sample_count_out = prev_sample_count + 1u;
                    imageStore(gBuffer, opix, uvec4(g_buffer_packed, 0u));
                }
            }

            imageStore(accumulationOut, opix, accumulated_rgba_out);
            imageStore(accuSampleCountOut, opix, uvec4(sample_count_out));
            // note: in theory, the denoisingBuffer could already contain the .a=-1 marker for pixels that did not
            // receive a single rendered sample. but this would require to read the (possibly unchanged) G-buffer
            // which is only writte to in this shader.
            imageStore(denoisingBuffer[0], opix, accumulated_rgba_out);
        }
    }
}
#endif // VOLCANITE_FRAMEBUFFER_READONLY

#endif // VOLCANITE_FRAMEBUFFER_GLSL
