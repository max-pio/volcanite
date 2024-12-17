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

// TODO: control which buffers/images are read- and/or writeonly with defines

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict EncodingRef
{
    uint buf[];
};

// TODO: use push constants for camera parameters and uniform buffers for things that change rarely

layout(std140, set=0, binding=0) uniform segmented_volume_info {
    uvec3 g_vol_dim;                // xyz dimension of the original volume
    vec3 g_voxel_size;              // relative size of a single voxel (can be greater than 1 in any dim)
    ivec3 g_vol_translation;        // translates the volume by the given voxel count
    vec3 g_physical_vol_dim;        // physical volume size: g_vol_dim * g_voxel_size
    vec3 g_normalized_volume_size;  // world space size of the volume (usually ~1m^3 with the largest dim being 1)
    uint g_vol_max_label;           // maximum label in the segmented volume
    uvec3 g_brick_count;            // number of bricks in each xyz dimension for the encoded volume
    uint g_frame;                   // current frame of the rendering
//
    uint g_max_inv_lod;             // max. inv LOD that we would decode / traverse
    uint g_cache_capacity;          // number of base elements that can be held in cache at the same time
    uint g_cache_base_element_uints;// size in uints of an atomic cache memory region that stores 2x2x2=8 output voxels
    uint g_cache_indices_per_uint;  // number of output element indices that are stored in one uint in the cache
    uint g_cache_palette_idx_bits;  // size of one index of one output element in the cache measured in bits
    uint g_free_stack_capacity;     // number of max. stack elements in each LoD of the free_block_stack
    uint g_request_buffer_capacity; // the size of the request buffer
    uint g_detail_buffer_dirty;     // 0 if we can read from the detail buffer, 1 if the detail buffer is dirty
    uint g_brick_idx_to_enc_vector; // dividing the brick index by this number yields its encoding vector index
    uvec2 g_detail_buffer_address;
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

layout(std430, binding = 3) buffer restrict brick_cache_infos
{
// for each block 4 entries:
// req_inv_lod: <  lod_count is "req. inv. LoD and is visible"
//              == lod_count is "potentially visible"
//              >  lod_count is "guaranteed to be invisible" after transfer function check
// cur_inv_lod: INVALID brick is not decoded
//              otherwise currently decoded LoD
// cache_index: INVALID brick is not decoded
//              otherwise the cache index where each cache element is (base_element_size) uints large to fit 2x2x2=8 output voxels
// req_slot:    INVALID nothing to do
//              otherwise the unique request index in [0, total_number_of_requests_in_this_frame_for_this_lod)
// TODO: change g_brick_info to array of structs
    uint g_brick_info[];
};
#define BRICK_INFO_REQ_INV_LOD 0
#define BRICK_INFO_CUR_INV_LOD 1
#define BRICK_INFO_CACHE_INDEX 2
#define BRICK_INFO_REQ_SLOT 3

layout(std430, binding = 4) buffer restrict assign_info
{
// for (LOD_COUNT-1) LoDs, 3 entries:
// - new_blocks_start:    start of region in cache for new elements (written by provision and read by assign)
// - new_blocks_count:    number of newly allocated elements in cache
// - req_counter:         to get request indices starting from 0 per frame (written by request and read by provision and assign)
// (- potential fourth: max. index that will be able to grab an element from the freeBlockStack)
// followed by one uint which is the g_cache_top counter pointing to the next free base_element index in g_cache
// TODO: change g_assign_info to array of structs
    uint g_assign_info[];
};
#define ASSIGN_NEW_BLOCK_START 0
#define ASSIGN_NEW_BLOCK_COUNT 1
#define ASSIGN_REQUESTED_BLOCKS 2
#define ASSIGN_ELEMS_PER_LOD 3      // how many elements per LoD are in the assign_info_ssbo

layout(std430, binding = 5) buffer restrict free_block_stacks
{
// (LOD_COUNT-1) stacks storing up to g_free_stack_capacity elements, followed by lod_count stack_top counters in reverse
// [g_free_stack_capacity elements for L1, ... g_free_stack_capacity elements for L(N-1), L1_top, ... L(N-1)_top]
    uint g_free_block_stacks[];
};

layout(std430, binding = 6) buffer restrict brick_cache
{
// contains g_cache_capacity base elements made up by (base_element_size) uints to fit 2x2x2=8 output voxels.
// the g_brick_info[].CACHE_INDEX points to a base element from which on it is decoded into N
// base elements, where N depends on the LoD that this is decoded to. The higher the inv. lod
// the higher is N because more base elements are needed to store the finer brick resolution.
    uint g_cache[];
};

#ifdef SEPARATE_DETAIL
layout(std430, binding = 7) buffer restrict readonly detail_starts
{
    uint g_detail_starts[];  // start points of each detail level in g_detail. Ends with dummy entry one after g_detail
};
layout(std430, binding = 8) buffer restrict readonly detail
{
    uint g_detail[];      // encoding of all bricks where each element contains only the finest LoD. we use it to check palettes
};
layout(std430, binding = 9) buffer restrict detail_requests
{
    uint g_detail_requests[];  // contains 1D brick IDs for which the detail is requested from the CPU
};
#endif

layout (std140, binding = 10) uniform render_info {
    mat4 g_model_to_world_space;
    mat4 g_world_to_model_space;
    mat3 g_model_to_world_space_dir;
    mat3 g_world_to_model_space_dir;
    float g_world_to_model_space_scaling;
    mat4 g_world_to_projection_space;
    mat4 g_projection_to_world_space;
    mat3 g_projection_to_world_space_dir;
    mat4 g_projection_to_view_space;
    mat4 g_view_to_world_space;
    mat4 g_world_to_view_space;
    mat4 g_view_to_projection_space;
    mat3 g_pixel_to_ray_direction_world_space;
    vec3 g_camera_position_world_space;
    vec4 g_background_color_a;
    vec4 g_background_color_b;
    int g_max_active_material;
    float g_voxels_per_pixel_per_dist;
    float g_lod_bias;
    bool g_tonemap_enable;
    bool g_global_illumination_enable;
    bool g_envmap_enable;
    float g_shadow_pathtracing_ratio;
    vec3 g_light_direction;
    float g_light_intensity;
    int g_subsampling;
    int g_max_path_length;
    int g_maxSteps;
    bool g_blue_noise_enable;
    float g_factor_ambient;
    vec4 g_bboxMin;
    vec4 g_bboxMax;
    uint g_camera_still_frames;
    ivec2 g_subsampling_pixel;
    float g_random_seed;
    bool g_debug_envmap;
    bool g_debug_normals;
    bool g_debug_model_space;
    bool g_debug_brick_cache;
    bool g_debug_lod;
    bool g_debug_step_count;
    uint g_swapchain_index;     // index of this frame in the multiframe swapchain buffer lists
    bool g_denoise;
    bool g_bilateral_enable;
    bool g_svgf_enable;
    float g_difference_depth_denoising;
    float g_spatial_sigma;
    float g_depth_sigma;
    float g_illumination_sigma;
    float g_denoise_fade_sigma;
    bool g_denoise_fade_enable;
    int g_denoise_filter_kernel_size;
    bool g_dof_enable;
    float g_aperture;
    float g_image_plane;
    float g_f_stop;
    float g_focal_length;
    float g_focal_distance;
};

//layout (binding = 11, rgba8) uniform restrict image2D outColor;
#define BACKGROUND_DEPTH 3.402823466e+38
#define INVALID_DEPTH -3.402823466e+38
//layout (binding = 12, rgba32f) uniform restrict image2D outDepth;
layout (binding = 13, rgba16f) uniform restrict readonly image2D accumulationIn;
layout (binding = 14, rgba16f) uniform restrict image2D accumulationOut;
layout (binding = 21, r16ui) uniform restrict readonly uimage2D accuSampleCountIn;
layout (binding = 22, r16ui) uniform restrict uimage2D accuSampleCountOut;

layout (binding = 20, rg8ui) uniform restrict uimage2D gBuffer;
layout(binding = 23, rgba32f) uniform image2D denoisingBuffer[2];
layout (binding = 15, rgba8) uniform restrict writeonly image2D inpaintedOutColor;


layout(std430, binding = 16) buffer restrict writeonly gpu_stats
{
    uint gpu_blocks_decoded[6];
    uint gpu_blocks_in_cache[6];
    uint gpu_cache_size;
    uint gpu_raymarch_samples;
    uint gpu_bbox_hits;
};

layout(std430, binding = 17) buffer restrict readonly attributes
{
    float g_attributes[];      // multi-variate attributes, back to back in memory with [labelCount] elements per attribute
};

layout(std430, binding = 18) buffer restrict readonly materials
{
    GPUSegmentedVolumeMaterial g_materials[];
};

layout(binding = 19) uniform sampler1D s_transferFunctions[SEGMENTED_VOLUME_MATERIAL_COUNT];


layout(push_constant) uniform restrict readonly PushConstants
{
    uint denoising_iteration;   // denoising iteration variable for ping pong svgf-buffer
    float denoising_heuristic;  // heuristic for fade because image converges
} pc;