#ifndef VOLCANITE_CSGV_DEBUG_GLSL
#define VOLCANITE_CSGV_DEBUG_GLSL

// For better readability in csgv_renderer.comp, most of the debug visualizations and functionality is encapsualted in
// functions in this header. As these functions use other definitions from csgv_renderer.comp it should be included
// right above its main() function.


/// Visualizes model space coordinates.
/// Returns true if this thread should terminate afterwards as the pixel color was drawn.
bool DEBUG_vis_model_space(Ray ray, float t_0, ivec2 pixel, const bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return false;
    const vec3 pos = ray.origin + t_0 * ray.direction;
    const float depth = length((g_model_to_world_space * vec4(pos, 1.f)).xyz - g_camera_position_world_space) - (length(g_camera_position_world_space) - 0.8660254037f);
    const vec3 borders = vec3(greaterThan(fract(pos), vec3(0.0f))) * vec3(lessThan(fract(pos - vec3(0.0001f)), vec3(0.2f)));
    const uvec3 voxel = uvec3(floor(pos));
    vec3 color = vec3(voxel) / vec3(g_vol_dim);
    // hightlight borders of voxels that are close to the camera
    if (borders.x + borders.y + borders.z > 0.f)
    color *= 1.f - 1.f / (t_0 / g_world_to_model_space_scaling * 64.f);

    // prevent any post-processing blur by giving each voxel another label
    writePixel(pixel, vec4(color, 1.f), t_0, packGBufferRGB16(voxel.x ^ (voxel.y << 10u) ^ (voxel.z << 20u),
    vec3(0.f), depth));
    return true;
#else
    return false;
#endif
}

void DEBUG_vis_lod(RayMarchState state, inout vec4 surface_albedo_opacity, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    uint lod = get_inv_lod(state.voxel);
    if (lod != INVALID) {
        surface_albedo_opacity.rgb = (state.out_color.rgb + 3.f * colormap_viridis(float(lod)/float(LOD_COUNT - 1u))) / 4.f;
    }
#endif
}

/// Called when an empty brick is skipped during ray marching to update the color in the state.
void DEBUG_vis_empty_space_brick(inout RayMarchState state, const bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    const float empty_space_opacity = 128.f / float(g_vol_dim.x);
    state.out_color.rgb += state.throughput * vec3(state.throughput.r, 0.f, 1.f - state.throughput.r) * empty_space_opacity;
    state.throughput *= 1.f - empty_space_opacity;
    state.out_color.a = max(state.out_color.a, 1.f - (state.throughput.r + state.throughput.g + state.throughput.b) / 3.f);
#endif
}

/// Called when an invisible voxel is skipped during ray marching to update the color in the state.
void DEBUG_vis_empty_space_voxel(inout RayMarchState state, const bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    const float empty_space_opacity = 32.f / float(g_vol_dim.x);
    state.out_color.rgb += state.throughput * vec3(1.f - state.throughput.r, state.throughput.r, 0.f) * empty_space_opacity;
    state.throughput *= 1.f - empty_space_opacity;
    state.out_color.a = max(state.out_color.a, 1.f - (state.throughput.r + state.throughput.g + state.throughput.b) / 3.f);
#endif
}

/// Updates the state's color to display the brick id at the current location.
void DEBUG_vis_brick_idx(inout RayMarchState state, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    uvec3 brick = uvec3(state.voxel) / BRICK_SIZE;
    uint brick_idx = brick_pos2idx(brick, g_brick_count);
    state.out_color = vec4(integer2colorlabel(brick_idx, false), 1.f);
    state.throughput = vec3(0.f);
#endif
}

/// Updates the state's color with a virids map showing the decoded LOD in the brick cache at the current position,
/// or to red if the brick is not decoded.
void DEBUG_vis_brick_cache(inout RayMarchState state, ivec2 pixel, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    uvec3 brick = uvec3(state.voxel) / BRICK_SIZE;
    uvec3 brick_voxel = uvec3(state.voxel) - (brick * BRICK_SIZE);
    uint brick_info_pos = brick_pos2idx(brick, g_brick_count) * 4u;

    uint cache_start = g_brick_info[brick_info_pos + BRICK_INFO_CACHE_INDEX];
    if (cache_start == INVALID || g_brick_info[brick_info_pos + BRICK_INFO_CUR_INV_LOD] >= LOD_COUNT) {
        state.out_color = vec4(1.f, 0.f, 0.f, 1.f);
        state.throughput = vec3(0.f);
    } else if (g_brick_info[brick_info_pos + BRICK_INFO_CUR_INV_LOD] < LOD_COUNT) { // should always be true here
        uint lod = g_brick_info[brick_info_pos + BRICK_INFO_CUR_INV_LOD];
        state.out_color.rgb = colormap_viridis(float(lod - 1u)/float(LOD_COUNT - 2u));
        if (lod == 0u)
            state.out_color.rgb = vec3(1.f, 0.f, 0.f);
        state.out_color.a = 1.f;
        state.throughput = vec3(0.25f);
    }
#endif
}

/// Updates the state's color to display the shading normal at the current location.
void DEBUG_vis_normals(inout RayMarchState state, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    state.out_color = vec4(-min(state.normal, vec3(0.f)) * 0.5f + max(state.normal, vec3(0.f)), 1.f);
    state.throughput = vec3(0.f);
#endif
}


// INTERNAL ------------------------------------------------------------------------------------------------------------

/// If the state's label is invalid, updates the state color.
void DEBUG_vis_invalid_label(inout RayMarchState state) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (state.label == INVALID){
        state.out_color = vec4(1.f, 0.f, 1.f, 1.f);
        state.throughput = vec3(0.f);
        state.depth_valid = 0.f;
        assert(!isCenterWorkItem(), "get_volume_label returned INVALID. Possible corrupt data in brick cache.");
    }
#endif
}


/// Checks some state variables and ray origin / direction for inf or nan values. returns false if any are found.
void DEBUG_check_state_and_ray(inout RayMarchState state, const Ray ray, int line) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (any(isnan(state.voxel + state.pos_in_voxel)) || any(isinf(state.voxel + state.pos_in_voxel))
    || (any(isnan(state.pos_in_voxel)) || any(isinf(state.pos_in_voxel)))
    || (any(isnan(ray.origin)) || any(isinf(ray.origin)))
    ||  (any(isnan(ray.direction)) || any(isinf(ray.direction)))) {
        debugPrintfEXT("nan/inf in line %i, step %i   state.pos: %v3f | sub_voxel_pos: %v3f | state.t: %f | state.inv_ray_dir: %v3f | ray %v3f -> %v3f",
        line, state.step, state.voxel + state.pos_in_voxel, state.pos_in_voxel,
        length((state.voxel + state.pos_in_voxel) - ray.origin),
        state.inv_ray_dir, ray.origin, ray.direction);

        state.out_color = vec4(1.f, 0.f, 1.f, 1.f);
        state.depth_valid = 0.f;
        state.throughput = vec3(0.f);
    }
#endif
}

/// Counts the total number of ray marching steps and the total number of view rays that hit the volume's bounding box.
void DEBUG_count_bbox_hits(bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
    atomicAdd(gpu_stats.bbox_hits, 1u);
#endif
}

#endif // VOLCANITE_CSGV_DEBUG_GLSL
