#ifndef VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
#define VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL

// For better readability in csgv_renderer.comp, most of the debug visualizations and functionality is encapsualted in
// functions in this header. As these functions use other definitions from csgv_renderer.comp it should be included
// right above its main() function.

#define ENALBE_CSGV_DEBUGGING

#include "csgv_materials.glsl"
#include "debug_colormaps.glsl"

// blend a cache visualization over the pixels color
void DEBUG_img_cache(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;

    const ivec2 viewport_size = imageSize(inpaintedOutColor);

    #if CACHE_MODE == CACHE_VOXELS
        // map the pixel to a cache cell [region]
        const int size = 4;
        const uint elems_per_pixel = (CACHE_UINT_SIZE / 2) / (viewport_size.x * viewport_size.y / size);

        const uint idx = elems_per_pixel * uint((pixel.x / size) + (pixel.y / size) * viewport_size.x);

        if (idx >= CACHE_UINT_SIZE / 2)
            return;

        // accumulate information for all of the pixel's cache cels
        uint entry_count = 0u;
        uint visible_count = 0u;
        vec3 label = vec3(0.f);
        for (uint i = idx; i < idx + elems_per_pixel; i++) {
            if (g_cache[i * 2] != INVALID) {
                entry_count++;
                if (isLabelVisible(g_cache[i * 2 + 1])) {
                    visible_count++;
                    label += colormap_turbo(float(g_cache[i * 2 + 1] % 96) / 96.f);
                } else {
                    label += vec3(0.8f, 0.6f, 0.6f);
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
            const int mode = 0;
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

#endif // VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
