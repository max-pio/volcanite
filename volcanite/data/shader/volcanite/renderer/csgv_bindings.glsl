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

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict UVec4ArrayRef
{
    uvec4 buf[];
};

// static information that does not change as long as the segmentation volume or cache parameters are not udpated
// all of these could become compile time constants if shaders are created after buffer and volume construction
layout(std140, set=0, binding=0) uniform segmented_volume_info {
    uvec3 g_vol_dim;                // xyz dimension of the original volume
    // uint g_vol_max_label;        // unused: maximum label in the segmented volume
    uvec3 g_brick_count;            // number of bricks in each xyz dimension for the encoded volume
    uint g_brick_idx_count;         // number of brick indicies (brick_count.x * .y * .z)
// cache management
    uint g_free_stack_capacity;     // number of max. stack elements in each LoD of the free_block_stack
    uint g_cache_capacity;          // number of base elements that can be held in cache at the same time
    uint g_cache_base_element_uints;// size in uints of an atomic cache memory region that stores 2x2x2=8 output voxels
    uint g_cache_indices_per_uint;  // number of output element indices that are stored in one uint in the cache
    uint g_cache_palette_idx_bits;  // size of one index of one output element in the cache measured in bits
// encoding and detail encoding buffer management
    uint g_brick_idx_to_enc_vector; // dividing the brick index by this number yields its encoding vector index
    uvec2 g_cache_buffer_address;
    uvec2 g_empty_space_bv_address; // empty space bit vector: 0 = voxel set potentially visible, 1 = no visible labels
    uint g_empty_space_block_dim;   // a block of [g_empty_space_block_dim]^3 voxels is grouped into an empty space set
    uint g_empty_space_set_size;    // how many voxels are grouped into one bit (= g_empty_space_block_dim^3)
    uvec3 g_empty_space_dot_map;    // dot(voxel / es_block_size, es_dot_map) yields the 1D empty space index of voxel
    uvec2 g_detail_buffer_address;
    uint g_request_buffer_capacity; // the size of the request buffer for brick detail encodings
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
// req_inv_lod: <  lod_count: "brick is requested in this inv. LoD and visible"
//              == lod_count: "brick is potentially visible"
//              >  lod_count: "brick is guaranteed to be invisible" after transfer function check
// cur_inv_lod: INVALID: brick is not decoded
//              otherwise: currently decoded LoD
// cache_index: INVALID: brick is not decoded
//              otherwise: the cache index where each cache element is (base_element_size) uints large to fit 2x2x2=8 output voxels
// req_slot:    INVALID: nothing to do
//              otherwise: the unique request index in [0, total_number_of_requests_in_this_frame_for_this_lod)
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
#define ASSIGN_NEW_BRICK_COUNT 1
#define ASSIGN_REQUESTED_BRICKS 2
// how many elements per LoD are in the assign_info_ssbo
#define ASSIGN_ELEMS_PER_LOD 3
// lookup index in assign_info where the cache top pointer is stored
#define ASSIGN_CACHE_TOP_IDX ((LOD_COUNT - 1u) * ASSIGN_ELEMS_PER_LOD)

layout(std430, binding = 5) buffer restrict free_block_stacks
{
// (LOD_COUNT-1) stacks storing up to g_free_stack_capacity elements, followed by lod_count stack_top counters in reverse
// [g_free_stack_capacity elements for L1, ... g_free_stack_capacity elements for L(N-1), L1_top, ... L(N-1)_top]
    uint g_free_block_stacks[];
};

layout(std430, binding = 6) buffer restrict brick_cache
{
#if CACHE_MODE == CACHE_VOXELS
    // contains CACHE_UVEC2_SIZE elements as pack64(voxel_id_key, voxel_label).
    // a voxel_id_key of INVALID denotes an empty cache cell
    uint64_t g_cache[];
#else
    // contains g_cache_capacity base elements made up by (base_element_size) uints to fit 2x2x2=8 output voxels.
    // the g_brick_info[].CACHE_INDEX points to a base element from which on it is decoded into N
    // base elements, where N depends on the LoD that this is decoded to. The higher the inv. lod
    // the higher is N because more base elements are needed to store the finer brick resolution.
    uint g_cache[];
#endif
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
    uint g_detail_requests[];  // contains 1D brick IDs for which the detail is requested from the CPU,
                               // last element (at location g_request_buffer_capacity) is the atomic insert idx counter
};
#endif

layout (std140, binding = 10) uniform render_info {
// frame indices and seeds
    uint g_frame;                   // current frame index since the renderer was initialized
    uint g_camera_still_frames;     // current frame index within the current render accumulation loop
    uint g_target_accum_frames;     // how many frames will be rendered in this render loop
    float g_cache_fill_rate;        // value in [0, 1] indicating how many base elements of the cache are occupied
    int g_req_limit_area_size;      // value <= 0: no request limitation. otherwise: pixel area that can request bricks
    ivec2 g_req_limit_area_pos;     // start position of the area of pixels that can request bricks
    ivec2 g_req_limit_area_pixel;   // pixel with the (previously) min. spp within the area. used to track progress in
    uint g_req_limit_spp_delta;     // pixels with at most (min_spp + spp_delta) valid samples can request bricks
    uvec2 g_min_max_spp;            // minimum and maximum (valid) samples per pixel accumualted before this frame
    ivec2 g_min_spp_pixel;          // coordinate of one of the pixels that received the minimum number of samples so far
    uint g_swapchain_index;         // index of this frame in the multiframe swapchain buffer lists
    int g_subsampling;              // border length of the subsampling pixel block in which one sample is rendered
    ivec2 g_subsampling_pixel;      // local coordinate of the currently rendered pixel in the subsampling pixel block
    uint g_random_seed;
// shading
    float g_factor_ambient;
    float g_light_intensity;
    bool g_global_illumination_enable;
    float g_shadow_pathtracing_ratio;
    vec3 g_light_direction;
    bool g_envmap_enable;
    int g_max_path_length;
// materials
    int g_max_active_material;
// volume transformations
    vec3 g_voxel_size;              // relative size of a single voxel (can be greater than 1 in any dim)
    vec3 g_physical_vol_dim;        // physical volume size: g_vol_dim * g_voxel_size
    vec3 g_normalized_volume_size;  // world space size of the volume (usually ~1m^3 with the largest dim being 1)
    mat4 g_model_to_world_space;
    mat4 g_world_to_model_space;
    mat3 g_model_to_world_space_dir;
    mat3 g_world_to_model_space_dir;
    float g_world_to_model_space_scaling;
    ivec4 g_bboxMin;
    ivec4 g_bboxMax;
// general render config
    uint g_detail_buffer_dirty;     // 0 if we can read from the detail buffer, 1 if the detail buffer is dirty
    float g_lod_bias;               // bias for the LOD into which bricks are decoded
    uint g_max_inv_lod;             // maximum inverse LOD that will be decoded for any brick
    int g_max_request_path_length;  // paths up to this length invalidate when hitting undecoded bricks and request them
    int g_maxSteps;                 // maximum number of ray marching steps for each pixel
    bool g_blue_noise_enable;       // if view rays and other shading properties are jittered with ablue noise pattern
};

layout (std140, binding = 11) uniform camera_info {
    mat4 g_world_to_projection_space;
    mat4 g_projection_to_world_space;
    mat4 g_view_to_projection_space;
    mat4 g_projection_to_view_space;
    mat4 g_world_to_view_space;
    mat4 g_view_to_world_space;
    mat3 g_pixel_to_ray_direction_world_space;
    vec3 g_camera_position_world_space;
    float g_voxels_per_pixel_per_dist;
};

layout (std140, binding = 12) uniform resolve_info {
    vec4 g_background_color_a;
    vec4 g_background_color_b;
    bool g_tonemap_enable;
    float g_exposure;
    float g_brightness;
    float g_contrast;
    float g_gamma;
// denoising
    bool g_atrous_enabled;
    int g_denoise_filter_kernel_size;
    bool g_denoising_enabled;
    float g_depth_sigma;
    bool g_denoise_fade_enable;
    float g_denoise_fade_sigma;
// interaction and debugging
    ivec2 g_cursor_pixel_pos;   // screen space mouse position in frame buffer pixels
    uint g_debug_vis_flags;     // bit mask to enable different debug visualizations (requires ENALBE_CSGV_DEBUGGING)
};

#define BACKGROUND_DEPTH 3.402823466e+38
#define INVALID_DEPTH -3.402823466e+38

layout (binding = 13, rgba16f) uniform restrict readonly image2D accumulationIn;
layout (binding = 14, rgba16f) uniform restrict image2D accumulationOut;
layout (binding = 21, r16ui) uniform restrict readonly uimage2D accuSampleCountIn;
layout (binding = 22, r16ui) uniform restrict uimage2D accuSampleCountOut;

layout (binding = 20, rg8ui) uniform restrict uimage2D gBuffer;
layout(binding = 23, rgba32f) uniform image2D denoisingBuffer[2];
layout (binding = 15, rgba8) uniform restrict writeonly image2D inpaintedOutColor;


layout(std430, binding = 16) buffer restrict csgv_gpu_stats // writeonly
{
    GPUStats gpu_stats;
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


layout(push_constant) uniform PushConstants
{
    uint denoising_iteration;   // denoising iteration variable for indexing the ping pong buffers
    uint last_denoising_iteration;
} pc;
