#ifndef VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
#define VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL

// For better readability in csgv_*_resolve.comp, most of the debug visualizations and functionality is encapsualted in
// functions in this header. As these functions use other definitions from csgv_*_resolve.comp it should be included
// right above its main() function.

#define ENALBE_CSGV_DEBUGGING

#include "volcanite/renderer/csgv_materials.glsl"
#include "debug_colormaps.glsl"
#include "volcanite/renderer/framebuffer.glsl"
#include "pcg_hash.glsl"

// blend a cache visualization over the pixels color
void DEBUG_img_cache(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
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
    const ivec2 split_point = viewport_size / 2;



    if (!sampled) {
        // not sampled
        color = vec4(1.f, 0.f, float(g_frame % 120) / 120.f, 1.f);
    } else if (!isSurfaceHitGBufferRGB16(packed_g_buffer)) {
        // no surface hit
        color = vec4(0.f, 0.f, 0.f, 1.f);
    } else if (pixel.x < split_point.x && pixel.y < split_point.y) {
        // albedo
        color = vec4(getAlbedoOfLabel(label), 1.f);
    } else if (pixel.x < split_point.x && pixel.y >= split_point.y) {
        // normal
        color = vec4(-min(normal, vec3(0.f)) * 0.5f + max(normal, vec3(0.f)), 1.f);
    } else if (pixel.x >= split_point.x && pixel.y < split_point.y) {
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


#endif // VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
