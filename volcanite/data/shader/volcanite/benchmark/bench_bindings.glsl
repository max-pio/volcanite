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

#include "cpp_glsl_include/csgv_constants.incl"

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict EncodingRef
{
    uint buf[];
};

layout(push_constant) uniform restrict PushConstants
{
    uint brick_idx_offset;
    uint target_inv_lod;
} pc;

// TODO: most of these could be compile time constants
layout(std140, set=0, binding=0) uniform segmented_volume_info {
    uvec3 g_vol_dim;                // xyz dimension of the original volume
    uvec3 g_brick_count;            // number of bricks in each xyz dimension for the encoded volume
    uint g_brick_idx_count;         // number of brick indicies (brick_count.x * .y * .z)
//
    uint g_max_inv_lod;             // max. inv LOD that we would decode / traverse
    uint g_cache_base_element_uints;// size in uints of an atomic cache memory region that stores 2x2x2=8 output voxels
    uint g_cache_uints_per_brick;   // size in uints of a cache memory region that stores all output voxels of a brick
    uint g_cache_indices_per_uint;  // number of output element indices that are stored in one uint in the cache
    uint g_cache_palette_idx_bits;  // size of one index of one output element in the cache measured in bits
    uint g_brick_idx_to_enc_vector; // dividing the brick index by this number yields its encoding vector index
    uvec2 g_detail_buffer_address;
    uint g_detail_buffer_dirty;
};


layout(std430, binding = 1) buffer restrict readonly brick_starts
{
    uint g_brick_starts[];  // start points of each brick in its resepctive array in g_encoding_buffer_addresses.
};

layout(std430, binding = 2) buffer restrict readonly encoding_buffer_addresses
{
// Encoding buffer addresses of all bricks where each brick contains all its LODs, except the finest one if detail
// separation is enabled. The full palette is always included in the encoding arrays. We store the list of 64 bit device
// addresses as uvec2 for protability. Dividing a 1D brick index by g_brick_idx_to_enc_vector yields its encoding array.
    uvec2 g_encoding_buffer_addresses[];
};

#ifdef SEPARATE_DETAIL
layout(std430, binding = 3) buffer restrict readonly detail_starts
{
    uint g_detail_starts[];  // start points of each detail level in g_detail. Ends with dummy entry one after g_detail
};
layout(std430, binding = 4) buffer restrict readonly detail
{
    uint g_detail[];      // encoding of all bricks where each element contains only the finest LoD. we use it to check palettes
};
#endif

layout(std430, binding = 5) buffer restrict brick_cache
{
    uint g_cache[];
};
