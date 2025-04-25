#ifndef VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
#define VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL

// For better readability in csgv_*_resolve.comp, most of the debug visualizations and functionality is encapsualted in
// functions in this header. As these functions use other definitions from csgv_*_resolve.comp it should be included
// right above its main() function.

#include "volcanite/renderer/csgv_materials.glsl"
#include "debug_colormaps.glsl"
#include "volcanite/renderer/framebuffer.glsl"
#include "pcg_hash.glsl"

#include "volcanite/bit_vector.glsl"

/// Draws rectangles for each level-of-detail: if the volume fits into one of these rectangles it means that the
/// corresponding LoD will be selected (depends on how many voxels of the finest LoD fit into one pixel).
/// Returns true if this thread should terminate afterwards as the pixel color was drawn.
bool DEBUG_img_lod_rectangles(ivec2 pixel, inout vec4 color, const bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return false;

    for (uint lod = 0; lod < LOD_COUNT; lod++) {
        if (any(greaterThanEqual(abs(ivec2(pixel) - ivec2(imageSize(accumulationOut).xy)/2), ivec2(g_vol_dim.x / (2u << lod) - 2u)))) {
            if (all(lessThanEqual(abs(ivec2(pixel) - ivec2(imageSize(accumulationOut).xy)/2), ivec2(g_vol_dim.x / (2u << lod) + 2u)))) {
                color = vec4(colormap_viridis(float(LOD_COUNT - 1u - lod)/float(LOD_COUNT - 1u)), 1.f);
                return true;
            }
        }
    }
#endif
    return false;
}

bool isPixelInAABB(const ivec2 pixel, const ivec2 origin, const ivec2 size, const ivec2 border_size) {
    // returns true if the pixel is inside the area [origin, origin + size). If border_size > 0, the method only
    // returns true if the pixel is on the (inner) border of the given size
    return all(greaterThanEqual(pixel, origin)) && all(lessThan(pixel, origin + size))
        && (min(border_size.x, border_size.y) <= 0u || (any(lessThan(pixel, origin + border_size))
            || any(greaterThanEqual(pixel, origin + size - border_size))));
}

// blend a cache visualization over the pixel's color
void DEBUG_img_cache_array(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    const ivec2 viewport_size = imageSize(inpaintedOutColor);

    #if CACHE_MODE == CACHE_BRICKS
        const int bar_height = 20;

        // cache top pointer
        if (pixel.y < bar_height) {
            uint cache_top = g_assign_info[ASSIGN_CACHE_TOP_IDX];
            if (pixel.x <= int(float(cache_top) * float(viewport_size.x) / float(g_cache_capacity))) {
                color = vec4(0.8f, 0.2f, 0.f, 1.f);
            }

            // cache_top counts in base elements. one base element is 8 x 4 Byte. Draw one tick every 128 MB:
            if (pixel.x % int(float((128u / 32u) * 1024u * 1024u) * float(viewport_size.x) / float(g_cache_capacity)) == 0u) {
                color = vec4(0.f, 0.f, 0.f, 1.f);
            }
        }

        // cache stacks
        // * 4u becasue the free stack is probably not used completely
        const float free_stack_scaling = float(viewport_size.x) / float(g_free_stack_capacity) * 4u;

        for (int inv_lod = 1; inv_lod < LOD_COUNT; inv_lod++) {
            if (pixel.y >= bar_height * (inv_lod) && pixel.y < bar_height * (inv_lod + 1)) {
                const uint number_of_requested_blocks = gpu_stats.bricks_requested_L1_to_7[(inv_lod - 1u)];
                const uint number_of_free_blocks = gpu_stats.bricks_on_freestack_L1_to_7[(inv_lod - 1u)];
                color = vec4(0.f, 0.f, 0.f, 1.f);
                if (pixel.x < int(float(number_of_requested_blocks) * free_stack_scaling)) {
                    color.r = 0.8f;
                }
                if (pixel.x < int(float(number_of_free_blocks) * free_stack_scaling)) {
                    color.b = 0.8f;
                }
            }
        }


    #elif CACHE_MODE == CACHE_VOXELS
        // map the pixel to a cache cell [region]
        const int size = 4;
        const uint elems_per_pixel = max(CACHE_UVEC2_SIZE / (viewport_size.x * viewport_size.y / size), 1u);

        const uint idx = elems_per_pixel * uint((pixel.x / size) + (pixel.y / size) * viewport_size.x);

        if (idx >= CACHE_UVEC2_SIZE)
            return;

        // accumulate information for all of the pixel's cache cels
        uint entry_count = 0u;
        uint visible_count = 0u;
        vec3 label = vec3(0.f);
        for (uint i = idx; i < idx + elems_per_pixel; i++) {
            const uvec2 key_and_label = unpack32(g_cache[i]);
            if (i < CACHE_UVEC2_SIZE && key_and_label.x != INVALID) {
                entry_count++;
                if (isLabelVisible(key_and_label.y)) {
                    visible_count++;
                    label += colormap_viridis(float(key_and_label.y % 96) / 96.f);
                } else {
                    label += mix(vec3(1.f, 0.f, 1.f), vec3(0.8f), (0.5f * sin(float(g_frame) / 8.f) + 0.5f));
                }
            }
        }

        // display the rendering in grayscale in the background
        color = vec4(vec3(dot(color.rgb, vec3(1.f / 3.f))), color.a);

        // present the cache state as colored output
        vec3 display;
        if (entry_count == 0) {
            display = vec3(1.f);
        } else {
            const int mode = 2;
            switch (mode) {
                // label
                case 0:
                display = label / float(entry_count);
                break;
                // occupied entries
                case 1:
                display = colormap_viridis(float(entry_count) / float(elems_per_pixel));
                break;
                // proportion of invisible labels
                case 2:
                display = colormap_viridis(float(visible_count) / float(entry_count));
                break;
            }
        }

        // blend colored cache vis with background
        const float alpha = 0.8f;
        color = vec4((1.f - alpha) * color.rgb + alpha * display, 1.f);
    #endif

#endif
}

// show for all bricks if the are decoded / invisible / requested but not decoded
void DEBUG_img_brick_info(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    #if CACHE_MODE == CACHE_BRICKS
        const ivec2 viewport_size = imageSize(inpaintedOutColor);

        // display the rendering in grayscale in the background
        color = vec4(vec3(dot(color.rgb, vec3(1.f / 3.f))), color.a);

        // visualize the brick infos array
        const int size = clamp((viewport_size.x * viewport_size.y) / int(g_brick_idx_count), 1, 8);
        const uint elems_per_pixel = max(g_brick_idx_count / (viewport_size.x * viewport_size.y / size), 1u);
        const uint brick_idx = elems_per_pixel * uint((pixel.x / size) + (pixel.y / size) * viewport_size.x);

        if (brick_idx >= g_brick_idx_count)
            return;

        const uint brick_info_pos = brick_idx * 4u;
        const uint req_inv_lod = g_brick_info[brick_info_pos + BRICK_INFO_REQ_INV_LOD];
        const uint cur_inv_lod = g_brick_info[brick_info_pos + BRICK_INFO_CUR_INV_LOD];

        // ------------------
        // Visibility and decoding state
        vec3 display = vec3(0.f);
        // marked invisible
        if (req_inv_lod > LOD_COUNT)
            display = vec3(1.f);
        // not requested and not in cache
        else if (cur_inv_lod == INVALID && req_inv_lod == LOD_COUNT)
            display = vec3(0.f, 0.f, 0.4f);
        // requested but not in cache
        else if (cur_inv_lod == INVALID && req_inv_lod < LOD_COUNT)
            display = vec3(0.8f, 0.f, 0.f);
        // still in cache but no longer requested
        else if (cur_inv_lod != INVALID && req_inv_lod >= LOD_COUNT)
            display = vec3(0.f, 0.4f, 0.1f);
        // in cache and requested
        else if (cur_inv_lod != INVALID && req_inv_lod < LOD_COUNT)
            display = vec3(0.f, 1.f, 0.f);


        // TODO: include requested and current LODs in brick info debug visualization?

        // blend colored cache vis with background
        const float alpha = 0.8f;
        color = vec4((1.f - alpha) * color.rgb + alpha * display, 1.f);
    #endif
#endif
}


// visualize the pixel with the minimum number of valid samples so far, and the area of pixles that may request new
// bricks along with its representative pixel
void DEBUG_img_request_limit(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    const ivec2 viewport_size = imageSize(inpaintedOutColor);

    #if CACHE_MODE == CACHE_BRICKS
        // display the rendering in grayscale in the background
        color = vec4(vec3(dot(color.rgb, vec3(1.f / 3.f))), color.a);

        // marker the area of pixels that can request bricks, and the pixel with the minimum sample count:
        // TODO: brick request limitation could become its own debug visualization
        // mark a border around the active brick request area if brick request limitation is active
        if (g_req_limit_area_size > 0u
            && isPixelInAABB(pixel, g_req_limit_area_pos, ivec2(g_req_limit_area_size), ivec2(0))) {
            color.r = 0.f;
            color.b = 0.f;
        }
        if (g_req_limit_area_size > 0u && any(equal(g_req_limit_area_pixel, pixel))) {
            color.rgb = vec3(0.f);
        }
        if((isPixelInAABB(pixel, g_min_spp_pixel - ivec2(8), ivec2(17), ivec2(3)) || all(equal(pixel, g_min_spp_pixel)))) {
            color = vec4(0.8f, 0.f, 0.1f, 1.f);
        }
    #endif

#endif
}

// blend a visualization of the empty space bit vector over the pixel's color
void DEBUG_img_empty_space_bv(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    const ivec2 viewport_size = imageSize(inpaintedOutColor);

    // map the pixel to a cache cell [region]
    const int size = 4;
    const uvec3 empty_space_dim = g_vol_dim / g_empty_space_block_dim;
    const uint empty_space_set_count = empty_space_dim.x * empty_space_dim.y * empty_space_dim.z;
    const uint elems_per_pixel = max(empty_space_set_count / (viewport_size.x * viewport_size.y / size), 1u);
    const uint idx = elems_per_pixel * uint((pixel.x / size) + (pixel.y / size) * viewport_size.x);

    if (idx >= empty_space_set_count)
        return;

    BitVectorRef empty_space_bv = BitVectorRef(g_empty_space_bv_address);

    // accumulate information for all of the pixel's empty space bit vector cells
    uint empty_count = 0u;
    for (uint i = idx; i < idx + elems_per_pixel; i++) {
        if (i > empty_space_set_count)
            break;

        if (BV_ACCESS(empty_space_bv.words, i) > 0u) {
            empty_count++;
        }
    }

    // display the rendering in grayscale in the background
    color = vec4(vec3(dot(color.rgb, vec3(1.f / 3.f))), color.a);
    // the redder, the more invisible
    vec3 display = vec3(1.f, 0.f, 0.f);

    // blend colored cache vis with background
    const float alpha = float(empty_count) / float(elems_per_pixel);
    color = vec4((1.f - alpha) * color.rgb + alpha * display, 1.f);

#endif
}

void DEBUG_img_spp(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    const uint sample_count_block = imageLoad(accuSampleCountOut, pixel).r;
    if (sample_count_block == 0u)
        color = vec4(0.f, 0.f, 0.f, 1.f);
    else {
        const uint maximum_possible_spp = (g_target_accum_frames == 0u ? g_camera_still_frames : g_target_accum_frames)
                                            / (g_subsampling * g_subsampling);
        color = vec4(colormap_viridis(float(sample_count_block) / float(maximum_possible_spp)), 1.f);
    }
#endif
}


void DEBUG_img_no_postprocess(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    // debug input buffer (left) vs. upsampled and denoised (right) output
    if (pixel.x < g_cursor_pixel_pos.x) {
        color = imageLoad(accumulationOut, pixel);
    }
#endif
}

void DEBUG_img_g_buffer(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    float normalized_depth;
    vec3 normal;
    uint label;
    uvec3 packed_g_buffer = imageLoad(gBuffer, pixel).rgb;
    bool sampled = unpackGBufferRGB16(packed_g_buffer, label, normal, normalized_depth);

    const ivec2 viewport_size = imageSize(inpaintedOutColor);

    if (!sampled) {
        // not sampled
        color = vec4(1.f, 0.f, float(g_frame % 120) / 120.f, 1.f);
    } else if (!isSurfaceHitGBufferRGB16(packed_g_buffer)) {
        // no surface hit
        color = vec4(0.f, 0.f, 0.f, 1.f);
    } else if (pixel.x < g_cursor_pixel_pos.x && pixel.y < g_cursor_pixel_pos.y) {
        // albedo
        color = vec4(getAlbedoOfLabel(label), 1.f);
    } else if (pixel.x < g_cursor_pixel_pos.x && pixel.y >= g_cursor_pixel_pos.y) {
        // normal
        color = vec4(-min(normal, vec3(0.f)) * 0.5f + max(normal, vec3(0.f)), 1.f);
    } else if (pixel.x >= g_cursor_pixel_pos.x && pixel.y < g_cursor_pixel_pos.y) {
        // depth
        color = vec4(vec3(normalized_depth), 1.f);
    } else {
        // label
        if (label == INVALID) {
            color = vec4(1.f, 0.f, 1.f, 1.f);
        } else {
            uint hash = hash_pcg2d(uvec2(label)).x;
            color = vec4(colormap_turbo(float(hash & 4093u) / 4093.f), 1.f);
        }
    }
#endif
}


void DEBUG_img_envmap(vec2 fragCoord, vec3 rayDirection, inout vec4 bgColor, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    bgColor = vec4(g_light_intensity * dummy_envmap(rayDirection), 1.f);
#endif
}

#endif // VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
